// Cortex Filter's page on Melodee's screen (melodee_screen.h, ui/page.h).
//
// CUBE, the main display: on the left the filter cube in 3D, swaying slowly while sound runs through it, with the
// point the filter is at (a second one for the right channel when Spread splits them), its trail, the path the LFO
// will take, lines dropped from it to the cube's back walls, and the eight corners lit by how much of each the filter
// takes. On the right the filter's response in 3D: in TIME view the last moments of it, newest in front, receding
// into the past; in MORPH view the whole Morph axis at the current Freq and Transform, the response now lit where it
// sits in it. Over the cube: the knobs' and modulation's sum on each axis before the filter scales it (F, M, and T, or
// D when Transform drives the distortion), and each side's input, distortion and output meters. The header names the
// filter heard and the sequence's place. The wheel loads the next filter (with a sequence running, the sequence's
// next filter), Push changes the view, K1-K3 turn Morph, Freq and Transform.
//
// FILTER edits the filter Filter picks (filters.h), and copies or restores it. SEQ programs the sequence Sequence
// picks: the wheel moves along its steps, Push edits one (the wheel then picks a filter, a command, Insert or
// Delete; Minus / Plus jump; Push again sets it), and at the end of the list Add Step and Restore. TRIG holds the
// sequencer's clocks and triggers. The other groups list the knobs, the cube small beside them. Every response drawn
// is worked out from the same numbers the sound uses (morph.h).
#pragma once

#include "engine.h"
#include "filters.h"
#include "morph.h"
#include "params.h"
#include "ui/page.h"
#include "ui/wordmark.h"

#include <atomic>
#include <cctype>
#include <chrono>

namespace cortex
{
// What the audio thread tells the page after each block: where each channel's filter is, the envelope, the LFO's
// place, the rate, the filter heard and the sequencer's place, the axes' sums, the meters (the loudest since the page
// last took them), and that it ran.
struct Feed
{
    std::atomic<float> l[3] {}, r[3] {};
    std::atomic<float> env { 0.0f }, rate { 48000.0f };
    std::atomic<double> lfo { 0.0 };
    std::atomic<bool> split { false };
    std::atomic<int> slot { 1 }, sequence { 0 }, step { 0 }, mode { 0 }, fade { unityFade };
    std::atomic<uint32_t> triggers { 0 };
    std::atomic<bool> drives { false };
    std::atomic<float> control[3] {};
    std::atomic<float> meters[6] {}; // input L/R, distortion L/R, output L/R
    std::atomic<uint32_t> blocks { 0 };
};

// What the page asks of the sound: Inc, Dec and Reset pressed, and the wheel through a sequence's filters. Each is
// stamped (the steady clock's milliseconds) when the page gives it, and the audio thread takes those given since its
// last block at its next one, dropping any given more than keepMs before: a press made while the sound was not
// running is not fired later. A ring that only the page's thread writes and only the audio thread reads and clears
// (on stop and reset; on deactivate too, when nothing is processing).
struct Commands
{
    static constexpr int wheelKind = 3; // kinds 0-2 are Inc, Dec and Reset
    static constexpr int64_t keepMs = 500;
    static int64_t nowMs()
    {
        return std::chrono::duration_cast<std::chrono::milliseconds> (std::chrono::steady_clock::now().time_since_epoch()).count();
    }

    void press (int which) { put (which, 1); }
    void wheel (int steps) { put (wheelKind, steps); }
    uint32_t count() const { return given.load (std::memory_order_relaxed); } // every one the page gave
    // The audio thread's: whether any wait; each still fresh to `use` (kind, amount) in the order given, the rest
    // dropped.
    bool waiting() const { return head.load (std::memory_order_relaxed) != tail.load (std::memory_order_acquire); }
    template <typename Use>
    void take (int64_t now, Use&& use)
    {
        const uint32_t end = tail.load (std::memory_order_acquire);
        for (uint32_t at = head.load (std::memory_order_relaxed); at != end; ++at)
        {
            const Command& c = ring[at % size];
            if (now - c.ms <= keepMs)
                use (c.kind, c.amount);
        }
        head.store (end, std::memory_order_release);
    }
    void clear() { head.store (tail.load (std::memory_order_acquire), std::memory_order_release); }

private:
    struct Command
    {
        int kind, amount;
        int64_t ms;
    };
    static constexpr uint32_t size = 64;
    Command ring[size] {};
    std::atomic<uint32_t> head { 0 }, tail { 0 }, given { 0 };
    void put (int kind, int amount)
    {
        given.fetch_add (1, std::memory_order_relaxed);
        const uint32_t at = tail.load (std::memory_order_relaxed);
        if (at - head.load (std::memory_order_acquire) >= size)
            return; // full: the sound has taken none of the last 64 (it is not running); this one is dropped
        ring[at % size] = { kind, amount, nowMs() };
        tail.store (at + 1, std::memory_order_release);
    }
};

// The cube in perspective: a point of it (each axis 0..1) on the screen, and its depth (larger is farther).
struct Camera
{
    float cosYaw, sinYaw, cosPitch, sinPitch, dist, focal, ox, oy;
    Camera (float yaw, float pitch, float distance, float focalLength, float x, float y)
        : cosYaw (std::cos (yaw)), sinYaw (std::sin (yaw)), cosPitch (std::cos (pitch)), sinPitch (std::sin (pitch)),
          dist (distance), focal (focalLength), ox (x), oy (y)
    {
    }
    void at (float x, float y, float z, float& sx, float& sy, float& depth) const
    {
        const float u = 2.0f * x - 1.0f, v = 2.0f * y - 1.0f, w = 2.0f * z - 1.0f;
        const float x1 = u * cosYaw - w * sinYaw, z1 = u * sinYaw + w * cosYaw;
        const float y2 = v * cosPitch + z1 * sinPitch, z2 = z1 * cosPitch - v * sinPitch;
        const float d = z2 + dist;
        sx = ox + focal * x1 / d, sy = oy - focal * y2 / d, depth = z2;
    }
    void at (float x, float y, float z, int& sx, int& sy) const
    {
        float fx, fy, d;
        at (x, y, z, fx, fy, d);
        sx = (int) std::lround (fx), sy = (int) std::lround (fy);
    }
};

// A cube's filter at a point, Null (-1) being no filter at all.
inline void designCube (int cube, const Position& p, float emphasis, float rate, Design& d)
{
    if (cube < 0)
        d = Design {}, d.bands = 0, d.wet = 1.0f, d.dryGain = 0.0f;
    else
        design (cubeTable (cube), p, emphasis, rate, d);
}

class Screen final : public hdui::Page
{
public:
    // The page's own buttons on FILTER (not parameters).
    static constexpr uint32_t copyButton = 1000, restoreButton = 1001;
    static constexpr int cubeGroup = 0, filterGroup = 1, seqGroup = 2, trigGroup = 3;

    Screen (const std::atomic<float>* values, Feed& feed, Store& store, Commands& commands)
        : values (values), feed (feed), store (store), commands (commands)
    {
        colours = { wordmark::background, 0xff0c1120, 0xff141c33, 0xff243050, 0xffe6eeff, 0xff8d9bc2, 0xff384464, 0xff3ee0ff, 0xff03121a };
        for (int i = 0; i < cols; ++i)
            columnHz[i] = 20.0f * std::pow (1000.0f, (float) i / (float) (cols - 1));
        static_assert (sizeof (filterRows) / sizeof (filterRows[0]) == filterCount, "the FILTER rows");
        for (int i = 0; i < filterCount; ++i)
            filterFields[i] = filterRows[i];
        groupList[0] = { "CUBE", nullptr, 0 }, groupList[1] = { "FILTER", filterFields, filterCount }, groupList[2] = { "SEQ", nullptr, 0 };
        groupList[3] = { "TRIG", trig, 9 }, groupList[4] = { "KNOBS", knobsRows, 5 }, groupList[5] = { "LFO", lfo, 7 };
        groupList[6] = { "ENV", envelope, 6 }, groupList[7] = { "OUT", out, 4 };
    }

    // The graphs move by themselves: sound ran through the filter lately, or MORPH view's landscape is still catching
    // up with a change (it follows a few slices a frame, even while the sound sleeps).
    bool animating (const hdui::Host&) const override { return live || (groupIndex() == cubeGroup && viewMorph && morphStale() > 0); }
    // A moving picture's next frame is due (about 30 a second).
    bool due (uint64_t nowMs) const { return nowMs - lastHost.nowMs >= frameMs; }

    bool hostChanged (const hdui::Host& h) const
    {
        return lastTitleFrame < 0 || h.accent != lastHost.accent || h.shift != lastHost.shift || h.playing != lastHost.playing
               || ! std::equal (h.pot, h.pot + 3, lastHost.pot) || ! std::equal (h.engaged, h.engaged + 3, lastHost.engaged);
    }

    bool bodyAtRest (const hdui::Host& h) const { return ! newBlocks() && ! animating (h); }
    // The sound ran a block the page has not shown yet.
    bool newBlocks() const { return feed.blocks.load (std::memory_order_acquire) != seenBlocks; }

    // The page is awake for awakeMs from the last time it was woken (it opened, an input, a value or a knob changed,
    // sound ran; cortex.cpp). The title moves while it is awake or anything else moves (the graphs, the song playing;
    // the LFO and the sequencer move only while sound runs); otherwise the title rests on its rest frame and the page
    // asks for no paints, to save the battery.
    void wake (uint64_t nowMs) { awakeUntilMs = nowMs + awakeMs; }
    bool moving (const hdui::Host& h) const { return animating (h) || h.playing || h.nowMs < awakeUntilMs; }
    int titleFrame (const hdui::Host& h) const { return moving (h) ? wordmark::frameAt (h.nowMs) : wordmark::restFrame; }

    bool paintTitleOnly (const hdui::Host& h, uint32_t* pixels, int stride)
    {
        const int frame = titleFrame (h);
        if (frame == lastTitleFrame)
            return false;
        wordmark::drawMotion (pixels, stride, frame);
        lastTitleFrame = frame;
        return true;
    }

    // The audio thread's news and the animation's step, before each paint.
    void update (uint64_t nowMs)
    {
        const uint32_t blocks = feed.blocks.load (std::memory_order_acquire);
        if (blocks != seenBlocks)
            seenBlocks = blocks, lastBlockMs = nowMs, heard = true;
        live = heard && nowMs - lastBlockMs < liveMs;
        if (lastUpdateMs != 0 && live)
            swayMs += std::min<uint64_t> (nowMs - lastUpdateMs, 100);
        const uint64_t since = lastUpdateMs == 0 ? 0 : std::min<uint64_t> (nowMs - lastUpdateMs, 200);
        lastUpdateMs = nowMs;
        rate = std::max (8000.0f, feed.rate.load (std::memory_order_relaxed));
        if (rate != columnRate) // each column's tan(pi f / rate), for the responses
        {
            for (int i = 0; i < cols; ++i)
                columnT[i] = tanOf (columnHz[i], rate);
            columnRate = rate;
        }
        // The sequencer: a trigger lights the header for a moment.
        const uint32_t fired = feed.triggers.load (std::memory_order_relaxed);
        if (fired != seenTriggers)
            seenTriggers = fired, flashUntilMs = nowMs + flashMs;
        // The meters: the loudest since the last paint, falling back 30 dB a second.
        for (int m = 0; m < 6; ++m)
        {
            const float peak = feed.meters[m].exchange (0.0f, std::memory_order_relaxed);
            const float fall = std::pow (10.0f, -1.5f * (float) since / 1000.0f);
            meter[m] = live ? std::max (peak, meter[m] * fall) : 0.0f;
        }

        // Where the filter is: as the sound has it while it runs; otherwise from the knobs and the filter heard (the
        // sound is asleep and hears no knob: this is where it will start).
        slotNow = feed.slot.load (std::memory_order_relaxed);
        if (live)
        {
            pointL = { feed.l[0].load (std::memory_order_relaxed), feed.l[1].load (std::memory_order_relaxed), feed.l[2].load (std::memory_order_relaxed) };
            pointR = { feed.r[0].load (std::memory_order_relaxed), feed.r[1].load (std::memory_order_relaxed), feed.r[2].load (std::memory_order_relaxed) };
            splitNow = feed.split.load (std::memory_order_relaxed);
            for (int a = 0; a < 3; ++a)
                controlNow[a] = feed.control[a].load (std::memory_order_relaxed);
            drives = feed.drives.load (std::memory_order_relaxed);
        }
        else
            restingPoints();

        // The trail and the waterfall: a step each frame while live, and whenever the point or the filter moved.
        const int cube = store.cubeOf (slotNow);
        const float emphasis = value (ParamId::emphasis), level = levelOf (slotNow);
        const bool moved = pointL != trail[trailHead] || (splitNow && pointR != shownRight) || splitNow != shownSplit || cube != shownCube
                           || emphasis != shownEmphasis || rate != shownRate || level != shownLevel;
        if (moved || (live && nowMs - lastSliceMs >= sliceMs))
        {
            if (moved || trailCount == 0)
                trailHead = (trailHead + 1) % trailLength, trail[trailHead] = pointL, trailCount = std::min (trailCount + 1, trailLength);
            Design d;
            designCube (cube, pointL, emphasis, rate, d);
            sliceHead = (sliceHead + 1) % slices;
            for (int i = 0; i < cols; ++i)
                sliceDb[sliceHead][i] = responseDbAt (d, columnT[i]) + level;
            sliceCount = std::min (sliceCount + 1, slices);
            if (splitNow)
            {
                designCube (cube, pointR, emphasis, rate, d);
                const float right = store.used (slotNow) ? store.field (slotNow, fieldGainRight) : 0.0f;
                for (int i = 0; i < cols; ++i)
                    rightDb[i] = responseDbAt (d, columnT[i]) + right;
            }
            lastSliceMs = nowMs, shownCube = cube, shownEmphasis = emphasis, shownRate = rate, shownRight = pointR, shownSplit = splitNow;
            shownLevel = level;
        }
        relabel();
    }
    void drew (const hdui::Host& h)
    {
        lastHost = h;
        lastTitleFrame = titleFrame (h);
    }

    bool morphView() const { return viewMorph; }
    // MORPH view's slices not yet worked out for the cube and point drawn last (for the tests).
    int morphStale() const
    {
        int stale = 0;
        for (const Position& at : morphAt)
            stale += morphCube != store.cubeOf (slotNow) || at.y != pointL.y || at.z != pointL.z;
        return stale;
    }
    int seqRow() const { return seqCursor; }
    bool seqEditing() const { return editingStep; }

protected:
    int groupCount() const override { return (int) (sizeof (groupList) / sizeof (groupList[0])); }
    const hdui::Group& groupAt (int i) const override { return groupList[i]; }
    void drawTitle (hdui::Raster& r, const hdui::Host& h) const override
    {
        for (int y = 0; y < wordmark::height; ++y)
            std::copy_n (wordmark::fixed.data() + y * wordmark::width, wordmark::width, r.px + y * r.stride);
        wordmark::drawMotion (r.px, r.stride, titleFrame (h));
    }
    bool tallCustom() const override { return true; }
    bool customEditing() const override { return groupIndex() == seqGroup && editingStep; }

    float value (uint32_t id) const { return values[paramIndex (id)].load (std::memory_order_relaxed); }
    int editSlot() const { return (int) value (ParamId::filter); }
    int sequenceNow() const { return (int) value (ParamId::sequence); }

    void read (uint32_t id, hdui::Value& out) const override
    {
        if (id == copyButton || id == restoreButton)
        {
            out.kind = hdui::Kind::button;
            if (id == copyButton)
            {
                const int to = store.emptyAfter (editSlot());
                if (to == 0)
                    std::snprintf (out.text, sizeof (out.text), "BANK FULL");
                else
                    std::snprintf (out.text, sizeof (out.text), "TO F%02d", to);
            }
            else
                std::snprintf (out.text, sizeof (out.text), "%s", restoreArmed ? "PUSH AGAIN" : "FACTORY");
            return;
        }
        const int index = paramIndex (id);
        if (index < 0)
            return;
        const Param& p = paramTable[index];
        out.value = value (id);
        out.min = p.min, out.max = p.max, out.def = p.def;
        out.kind = p.kind == Kind::continuous ? hdui::Kind::continuous : (p.kind == Kind::integer ? hdui::Kind::stepped : hdui::Kind::choice);
        out.bipolar = p.min < 0.0f && (p.unit == Unit::bipolar || p.unit == Unit::offset || p.unit == Unit::times);
        if (id >= ParamId::trigInc && id <= ParamId::trigReset)
        {
            out.kind = hdui::Kind::button;
            std::snprintf (out.text, sizeof (out.text), "FIRE");
            return;
        }
        if (id == ParamId::filter)
        {
            std::snprintf (out.text, sizeof (out.text), "F%02d %s", (int) out.value, store.name ((int) out.value));
            upper (out.text);
            return;
        }
        formatParam (p, out.value, out.text, sizeof (out.text));
        if (p.unit == Unit::choice)
            upper (out.text);
    }

    // FILTER's buttons: Copy (the filter into the next empty one, which Filter then picks) and Restore (the factory's
    // filter back, or an empty one: two pushes); the rest are the sequencer's triggers, for the plug-in to fire.
    void pressed (uint32_t id, hdui::Edits& out) override
    {
        const int slot = editSlot();
        if (id == copyButton)
        {
            restoreArmed = false;
            const int to = store.emptyAfter (slot);
            if (to != 0)
                store.copy (slot, to), out.set (ParamId::filter, (float) to);
            return;
        }
        if (id == restoreButton)
        {
            if (! restoreArmed)
                return void (restoreArmed = true);
            restoreArmed = false;
            store.factorySlot (slot);
            out.set (ParamId::filter, (float) slot); // the edit filter's fields show it again
            return;
        }
        restoreArmed = false;
        out.press (id);
    }

    bool customInput (int group, hdui::Key key, int amount, bool, hdui::Edits& out) override
    {
        restoreArmed = false;
        if (group == seqGroup)
            return seqInput (key, amount, out);
        // CUBE: the wheel loads the next filter in use (a sequence's next filter while one runs), Push the view.
        if (key == hdui::Key::wheel)
        {
            if (store.length (sequenceNow()) > 0)
                commands.wheel (amount);
            else
            {
                const int now = editSlot(), next = store.usedFrom (now, amount);
                if (next != now)
                    out.set (ParamId::filter, (float) next);
            }
            return true;
        }
        if (key == hdui::Key::push)
            return viewMorph = ! viewMorph, true;
        return false;
    }

    void customKnobs (int group, uint32_t ids[3]) const override
    {
        if (group == seqGroup)
            ids[0] = ParamId::sequence, ids[1] = ParamId::incClock, ids[2] = ParamId::resetClock;
        else
            ids[0] = ParamId::morph, ids[1] = ParamId::freq, ids[2] = ParamId::transform;
    }
    // Short names for the strip under CUBE and SEQ.
    const char* customLabel (uint32_t id) const override
    {
        switch (id)
        {
            case ParamId::morph: return "MORPH";
            case ParamId::freq: return "FREQ";
            case ParamId::transform: return "XFORM";
            case ParamId::sequence: return "SEQ";
            case ParamId::incClock: return "INC";
            case ParamId::resetClock: return "RESET";
            default: return "";
        }
    }

    void legend (const char* lines[6]) const override
    {
        if (groupIndex() == cubeGroup)
            lines[0] = "WHEEL FILTER", lines[1] = "PUSH   VIEW", lines[2] = "-/+  GROUP", lines[3] = "K1 MORPH", lines[4] = "K2 FREQ",
            lines[5] = "K3 TRANSFORM";
        else if (groupIndex() == seqGroup)
            lines[0] = editingStep ? "WHEEL CHOOSE" : "WHEEL  STEP", lines[1] = editingStep ? "PUSH    SET" : "PUSH   EDIT",
            lines[2] = editingStep ? "-/+   JUMP" : "-/+  GROUP", lines[3] = "", lines[4] = "K1 SEQUENCE", lines[5] = "K2 INC K3 RESET";
        else
            Page::legend (lines);
    }

    // The header's right: the filter heard, and the sequence's place (lit for a moment by each trigger).
    void drawStatus (hdui::Raster& r, const hdui::Host& h, int right) override
    {
        char text[48];
        std::snprintf (text, sizeof (text), "F%02d %s", slotNow, store.name (slotNow));
        upper (text);
        text[18] = 0;
        r.text (right - hdui::textWidth (text), headerY + 1, text, colours.text);
        sequenceStatus (text, sizeof (text), 18);
        const bool flash = h.nowMs < flashUntilMs;
        r.text (right - hdui::textWidth (text), headerY + 13, text, flash ? yellow : (live ? accent (h) : colours.muted));
    }

    // Beside a list: the cube, small, and the response under it (the filter heard, which the header names); beside
    // TRIG the sequence's steps.
    void drawPanel (hdui::Raster& r, const hdui::Host& h, int x, int y, int w, int height) override
    {
        if (groupIndex() == trigGroup)
            return drawSteps (r, h, x, y, w);
        drawCube (r, h, Camera (yaw(), pitch(), 4.6f, 128.0f, (float) (x + w / 2), (float) (y + 36)), false);
        const int top = y + height - 26;
        r.box (x, top - 2, w, 1, colours.rule);
        const int zeroY = top + 25 - (int) std::lround (-dbLow / (dbHigh - dbLow) * 25.0f);
        r.line (x, zeroY, x + w - 1, zeroY, colours.dim, 1);
        drawCurve (r, sliceDb[sliceHead], x, top, w, 26, accent (h));
    }

    void drawCustom (hdui::Raster& r, const hdui::Host& h, int x, int y, int w, int height) override
    {
        if (groupIndex() == seqGroup)
            return drawSequence (r, h, x, y, w, height);
        drawScopeRim (r, x + 78, y + 85);
        drawCube (r, h, Camera (yaw(), pitch(), 4.6f, 184.0f, (float) (x + 80), (float) (y + height / 2 + 2)), true);
        drawWaterfall (r, h, x + 158, y + height - 12);
        drawReadouts (r, h, x + 4, y + 1);
    }

private:
    const std::atomic<float>* values;
    Feed& feed;
    Store& store;
    Commands& commands;

    static constexpr int cols = 112, slices = 18, trailLength = 24;
    static constexpr uint64_t frameMs = 33, sliceMs = 40, liveMs = 400, flashMs = 150, awakeMs = 4000;
    static constexpr float sliceDx = 2.4f, sliceDy = -3.8f, dbLow = -36.0f, dbHigh = 18.0f, sliceHeight = 64.0f;
    static constexpr hdui::Ink magenta = 0xffd040ff, deep = 0xff22183f, grid = 0xff101828, path = 0xff6a2c8c, edgeNear = 0xff8fb6e8,
                               edgeFar = 0xff27324f, white = 0xfff4f8ff, yellow = 0xffffd23e, green = 0xff3ee07a, red = 0xffff4a4a;

    float columnHz[cols] {};
    double columnT[cols] {};
    float columnRate = 0.0f;
    float sliceDb[slices][cols] {};
    float rightDb[cols] {};
    int sliceHead = 0, sliceCount = 0;
    Position trail[trailLength] {};
    int trailHead = 0, trailCount = 0;
    Position pointL, pointR;
    bool splitNow = false, live = false, heard = false, viewMorph = false, drives = false;
    float controlNow[3] {}, meter[6] {};
    uint32_t seenBlocks = 0, seenTriggers = 0;
    hdui::Host lastHost {};
    int lastTitleFrame = -1;
    uint64_t lastBlockMs = 0, lastUpdateMs = 0, lastSliceMs = 0, swayMs = 0, flashUntilMs = 0, awakeUntilMs = 0;
    float rate = 48000.0f;
    int slotNow = 1, shownCube = -2;
    float shownEmphasis = 0.0f, shownRate = 0.0f, shownLevel = 0.0f;
    Position shownRight;
    bool shownSplit = false;
    // MORPH view's landscape, kept while its cube, Freq, Transform and Emphasis stay.
    static constexpr int morphSlices = 12;
    float morphDb[morphSlices][cols] {};
    Position morphAt[morphSlices] {}; // the Freq and Transform each slice was worked out at
    int morphNext = 0, morphCube = -2;
    float morphEmphasis = 0.0f, morphRate = 0.0f;
    // SEQ: the row the cursor is on (the steps, then Add Step and Restore), a step being edited and the choice
    // the wheel is on, the sequence they belong to; Restore's first push.
    int seqCursor = 0, seqShown = -1, choice = 0, choiceCount = 0;
    int choices[slotCount + 3 + fadeScaleCount + 2] {};
    bool editingStep = false, restoreArmed = false, seqRestoreArmed = false;
    static constexpr int choiceInsert = 250, choiceDelete = 251;

    static void upper (char* text)
    {
        for (char* c = text; *c != 0; ++c)
            *c = (char) std::toupper ((unsigned char) *c);
    }

    // A filter's level on the left (the response drawn is what is heard).
    float levelOf (int slot) const { return store.used (slot) ? store.field (slot, fieldGainLeft) : 0.0f; }

    // The sequence's place in at most `room` characters: "S05 ACID STEPS 03", "S07 BAR GATE -H-", "SEQ OFF".
    void sequenceStatus (char* text, size_t size, int room) const
    {
        const int q = sequenceNow();
        if (q == 0 || store.length (q) == 0)
        {
            std::snprintf (text, size, q == 0 ? "SEQ OFF" : "S%02d EMPTY", q);
            return;
        }
        char place[16];
        const bool running = feed.sequence.load (std::memory_order_relaxed) == q;
        const int mode = feed.mode.load (std::memory_order_relaxed), fade = feed.fade.load (std::memory_order_relaxed);
        if (! running)
            std::snprintf (place, sizeof (place), "--");
        else if (mode == (int) Sequencer::Mode::halted)
            std::snprintf (place, sizeof (place), "-H-");
        else if (mode == (int) Sequencer::Mode::paused)
            std::snprintf (place, sizeof (place), "-P-");
        else
            std::snprintf (place, sizeof (place), "%02d", feed.step.load (std::memory_order_relaxed) + 1);
        if (running && fade != unityFade)
            std::snprintf (place + std::strlen (place), sizeof (place) - std::strlen (place), " %s", fadeScaleNames[fade]);
        char name[32];
        std::snprintf (name, sizeof (name), "%s", sequenceChoices[q]);
        upper (name);
        const int nameRoom = std::max (0, room - 4 - 1 - (int) std::strlen (place));
        std::snprintf (text, size, "S%02d %.*s %s", q, nameRoom, name, place);
    }

    // A slow sway while sound runs: the cube turns a little either way and nods, so its depth reads.
    float yaw() const { return -0.55f + 0.2f * std::sin ((float) swayMs * (2.0f * pi / 9000.0f)); }
    float pitch() const { return 0.36f + 0.06f * std::sin ((float) swayMs * (2.0f * pi / 14000.0f)); }

    // The filter heard's offsets and gains, as the sound has them settled.
    struct Scaling
    {
        float gain[3], offset[3];
        bool drives;
    };
    Scaling scalingOf (int slot) const
    {
        Scaling s;
        for (int a = 0; a < 3; ++a)
            s.gain[a] = store.field (slot, fieldMorphGain + a) / 10.0f, s.offset[a] = store.field (slot, fieldMorphOffset + a) / 100.0f;
        s.drives = store.used (slot) && store.field (slot, fieldTransformDist) >= 0.5f;
        return s;
    }
    // Knobs and modulation (`v`) to a point, as the sound does it.
    static void place (const Scaling& s, const float v[3], float p[3])
    {
        for (int a = 0; a < 3; ++a)
            p[a] = a == 2 && s.drives ? std::clamp (0.5f + 0.5f * s.offset[2], 0.0f, 1.0f) : axisPoint (v[a], s.gain[a], s.offset[a]);
    }

    // Where the filter will be, from the knobs: the point the sound would start from now.
    void restingPoints()
    {
        const Shape shape = (Shape) (int) value (ParamId::lfoShape);
        const double placeNow = feed.lfo.load (std::memory_order_relaxed);
        const float offset = value (ParamId::lfoPhase);
        const uint32_t knobs[3] = { ParamId::morph, ParamId::freq, ParamId::transform };
        const uint32_t depths[3] = { ParamId::lfoMorph, ParamId::lfoFreq, ParamId::lfoTransform };
        float v[3], p[3];
        for (uint32_t a = 0; a < 3; ++a)
        {
            v[a] = value (knobs[a]) + lfoSwing * value (depths[a]) * lfoValue (shape, placeNow + (double) (offset * (float) a), a);
            controlNow[a] = std::clamp (2.0f * v[a] - 1.0f, -1.0f, 1.0f);
        }
        const Scaling s = scalingOf (slotNow);
        drives = s.drives;
        place (s, v, p);
        pointL = pointR = { p[0], p[1], p[2] };
        const float spread = value (ParamId::spread);
        splitNow = spread > 0.0f;
        if (splitNow)
        {
            const float half = 0.5f * spreadWidth * spread, centre = std::clamp (p[0], half, 1.0f - half);
            pointL.x = centre - half, pointR.x = centre + half;
        }
    }

    // The LFO's path through the cube over one cycle, from the knobs (none for the random shapes, which have none).
    int orbit (Position* out, int count) const
    {
        const Shape shape = (Shape) (int) value (ParamId::lfoShape);
        const uint32_t knobs[3] = { ParamId::morph, ParamId::freq, ParamId::transform };
        const uint32_t depths[3] = { ParamId::lfoMorph, ParamId::lfoFreq, ParamId::lfoTransform };
        const uint32_t envs[3] = { ParamId::envMorph, ParamId::envFreq, ParamId::envTransform };
        if (shape == Shape::sampleHold || shape == Shape::drift)
            return 0;
        bool any = false;
        for (uint32_t d : depths)
            any |= value (d) != 0.0f;
        if (! any)
            return 0;
        const float offset = value (ParamId::lfoPhase), env = feed.env.load (std::memory_order_relaxed);
        const Scaling s = scalingOf (slotNow);
        for (int i = 0; i < count; ++i)
        {
            float v[3], p[3];
            for (uint32_t a = 0; a < 3; ++a)
                v[a] = value (knobs[a]) + lfoSwing * value (depths[a]) * lfoValue (shape, (double) i / count + (double) (offset * (float) a), a)
                       + envSwing * value (envs[a]) * (live ? env : 0.0f);
            place (s, v, p);
            out[i] = { p[0], p[1], p[2] };
        }
        return count;
    }

    static float depthShade (float depth) { return std::clamp ((depth + 1.6f) / 3.2f, 0.0f, 1.0f); }

    // Opposite quadrants of the fixed scope bezel. The open quadrants give the axis labels room.
    static void drawScopeRim (hdui::Raster& r, int cx, int cy)
    {
        constexpr int arc[][2] = { { -73, -5 }, { -69, -19 }, { -59, -33 }, { -44, -44 }, { -27, -51 } };
        for (int side : { -1, 1 })
        {
            for (int i = 1; i < 5; ++i)
                r.line (cx + side * arc[i - 1][0], cy + side * arc[i - 1][1],
                        cx + side * arc[i][0], cy + side * arc[i][1], 0xff26223f);
            r.line (cx - side * 62, cy - side * 35, cx - side * 58, cy - side * 32, 0xff514667);
            r.line (cx - side * 45, cy - side * 47, cx - side * 43, cy - side * 43, 0xff6f7153);
        }
    }

    void drawCube (hdui::Raster& r, const hdui::Host& h, const Camera& cam, bool big)
    {
        const hdui::Ink hi = accent (h);
        // The back walls: the face of each axis farther from the eye.
        auto depthOf = [&] (float x, float y, float z) { float sx, sy, d; cam.at (x, y, z, sx, sy, d); return d; };
        const float backX = depthOf (0, 0.5f, 0.5f) > depthOf (1, 0.5f, 0.5f) ? 0.0f : 1.0f;
        const float backY = depthOf (0.5f, 0, 0.5f) > depthOf (0.5f, 1, 0.5f) ? 0.0f : 1.0f;
        const float backZ = depthOf (0.5f, 0.5f, 0) > depthOf (0.5f, 0.5f, 1) ? 0.0f : 1.0f;
        auto seg = [&] (Position a, Position b, hdui::Ink c, int dash = 0) {
            int x0, y0, x1, y1;
            cam.at (a.x, a.y, a.z, x0, y0);
            cam.at (b.x, b.y, b.z, x1, y1);
            r.line (x0, y0, x1, y1, c, dash);
        };
        if (big)
            for (float t : { 0.25f, 0.5f, 0.75f })
            {
                seg ({ backX, t, 0 }, { backX, t, 1 }, grid), seg ({ backX, 0, t }, { backX, 1, t }, grid);
                seg ({ 0, backY, t }, { 1, backY, t }, grid), seg ({ t, backY, 0 }, { t, backY, 1 }, grid);
                seg ({ 0, t, backZ }, { 1, t, backZ }, grid), seg ({ t, 0, backZ }, { t, 1, backZ }, grid);
            }

        // The LFO's path, dotted.
        Position ring[48];
        const int n = orbit (ring, 48);
        for (int i = 0; i < n; ++i)
            seg (ring[i], ring[(i + 1) % n], path, big ? 2 : 1);

        // The edges, brighter nearer (dim for a Null filter: no cube).
        const bool null = store.cubeOf (slotNow) < 0;
        static constexpr uint8_t edges[12][2] = { { 0, 1 }, { 2, 3 }, { 4, 5 }, { 6, 7 }, { 0, 2 }, { 1, 3 }, { 4, 6 }, { 5, 7 }, { 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 } };
        auto corner = [] (int c) { return Position { (float) (c & 1), (float) ((c >> 1) & 1), (float) ((c >> 2) & 1) }; };
        for (const auto& e : edges)
        {
            const Position a = corner (e[0]), b = corner (e[1]);
            const float d = depthOf (0.5f * (a.x + b.x), 0.5f * (a.y + b.y), 0.5f * (a.z + b.z));
            seg (a, b, null ? edgeFar : hdui::mix (edgeNear, edgeFar, (int) (depthShade (d) * 256.0f)), null ? 3 : 0);
        }

        // The corners, lit by how much of each the filter takes now.
        float w[8];
        cornerWeights (pointL, w);
        for (int c = 0; c < 8; ++c)
        {
            int sx, sy;
            const Position p = corner (c);
            cam.at (p.x, p.y, p.z, sx, sy);
            const float lit = null ? 0.0f : std::sqrt (w[c]);
            r.disc (sx, sy, big ? 1 + (int) (lit * 3.0f) : 1, hdui::mix (colours.dim, hi, (int) (lit * 256.0f)));
        }

        // Lines dropped to the back walls, and where they land.
        if (big)
            for (const Position& foot : { Position { backX, pointL.y, pointL.z }, Position { pointL.x, backY, pointL.z }, Position { pointL.x, pointL.y, backZ } })
            {
                seg (pointL, foot, hdui::mix (colours.bg, hi, 120), 2);
                int sx, sy;
                cam.at (foot.x, foot.y, foot.z, sx, sy);
                r.box (sx - 1, sy, 3, 1, hdui::mix (colours.bg, hi, 170)), r.box (sx, sy - 1, 1, 3, hdui::mix (colours.bg, hi, 170));
            }

        // The trail, fading.
        if (big)
            for (int i = 1; i < trailCount; ++i)
            {
                const Position& a = trail[(trailHead - i + 1 + trailLength) % trailLength];
                const Position& b = trail[(trailHead - i + trailLength) % trailLength];
                seg (b, a, hdui::mix (colours.bg, hi, 220 - 200 * i / trailLength));
            }

        // The point (the right channel's beside it when split), glowing.
        auto glow = [&] (const Position& p, hdui::Ink c) {
            int sx, sy;
            cam.at (p.x, p.y, p.z, sx, sy);
            if (big)
                r.disc (sx, sy, 8, c, 40), r.disc (sx, sy, 5, c, 110);
            r.disc (sx, sy, big ? 3 : 2, c);
            r.disc (sx, sy, big ? 1 : 0, white);
        };
        if (splitNow)
            glow (pointR, magenta);
        glow (pointL, hi);

        // The axes' names, by the corner where all three start.
        if (big)
        {
            int ax, ay, bx, by, cx, cy;
            cam.at (0.5f, 0, 0, ax, ay);
            cam.at (0, 0.5f, 0, bx, by);
            cam.at (1, 0, 0.5f, cx, cy);
            r.text (ax - 17, ay + 5, "MORPH", colours.muted);
            r.text (std::max (2, bx - 34), by - 6, "FREQ", colours.muted);
            r.text (std::min (cx + 6, 150 - hdui::textWidth ("XFORM")), cy + 2, "XFORM", drives ? colours.dim : colours.muted);
        }
    }

    // Above the cube, as the module's display has them: F, M and T (or D) as bars from the middle (the knobs and
    // modulation summed, before the filter scales them), then each side's input, distortion and output meters (left
    // over right).
    void drawReadouts (hdui::Raster& r, const hdui::Host& h, int x, int y)
    {
        const hdui::Ink hi = accent (h);
        const char* names[3] = { "F", "M", drives ? "D" : "T" };
        const int axisOf[3] = { 1, 0, 2 }, barW = 35;
        for (int k = 0; k < 3; ++k)
        {
            const int left = x + k * 52, bar = left + 10, mid = bar + barW / 2;
            r.text (left, y, names[k], k == 2 && drives ? red : colours.muted);
            r.box (bar, y + 6, barW, 1, colours.rule);
            r.box (mid, y + 3, 1, 7, colours.dim);
            const int at = mid + (int) std::lround (controlNow[axisOf[k]] * (float) (barW / 2));
            r.box (std::min (mid, at), y + 4, std::abs (at - mid) + 1, 5, k == 2 && drives ? red : hi);
        }
        const char* meters[3] = { "IN", "DS", "OUT" };
        for (int k = 0; k < 3; ++k)
        {
            const int left = x + k * 52, bar = left + (k == 2 ? 23 : 17), w = 52 - (bar - left) - 6, top = y + 12;
            r.text (left, top, meters[k], colours.muted);
            for (int c = 0; c < 2; ++c)
            {
                const float v = meter[2 * k + c];
                // Input and output: -48 dBFS to +6 (green, yellow from -6, red past 0). Distortion: how hard the
                // saturator is driven, its knee (1) a third of the way (red from there).
                float unit;
                if (k == 1)
                    unit = std::clamp (v / 3.0f, 0.0f, 1.0f);
                else
                    unit = v <= 0.0f ? 0.0f : std::clamp ((20.0f * std::log10 (v) + 48.0f) / 54.0f, 0.0f, 1.0f);
                const int by = top + 3 + c * 4;
                r.box (bar, by, w, 3, colours.rule);
                const int n = (int) std::lround (unit * (float) w);
                for (int i = 0; i < n; ++i)
                {
                    const float u = (float) (i + 1) / (float) w;
                    const hdui::Ink ink = k == 1 ? (u > 1.0f / 3.0f ? red : yellow) : (u > 48.0f / 54.0f ? red : (u > 42.0f / 54.0f ? yellow : green));
                    r.box (bar + i, by, 1, 3, ink);
                }
            }
        }
    }

    // A response as one line: dbLow at the floor, dbHigh at the top.
    void drawCurve (hdui::Raster& r, const float* db, int x, int y, int w, int height, hdui::Ink c) const
    {
        int prev = -1;
        for (int i = 0; i < w; ++i)
        {
            const float v = db[i * (cols - 1) / std::max (1, w - 1)];
            const int top = y + height - 1 - (int) std::lround (std::clamp ((v - dbLow) / (dbHigh - dbLow), 0.0f, 1.0f) * (float) (height - 1));
            if (prev >= 0)
                r.box (x + i, std::min (prev, top), 1, std::abs (prev - top) + 1, c);
            prev = top;
        }
    }

    hdui::Ink sliceInk (float t, hdui::Ink front) const
    {
        return t < 0.5f ? hdui::mix (front, magenta, (int) (t * 2.0f * 256.0f)) : hdui::mix (magenta, deep, (int) ((t - 0.5f) * 2.0f * 256.0f));
    }

    // One slice of the waterfall at depth `t` (0 front, 1 back). Slices are drawn front to back, each only where it
    // rises above everything nearer: `horizon` holds the highest row drawn in each column so far. So each pixel is
    // written once, and what is behind a slice stays hidden under it.
    void drawSlice (hdui::Raster& r, int* horizon, const float* db, int x0, int base, float t, hdui::Ink line, hdui::Ink fill) const
    {
        const int ox = x0 + (int) std::lround (t * (slices - 1) * sliceDx), oy = base + (int) std::lround (t * (slices - 1) * sliceDy);
        int prev = -1;
        for (int i = 0; i < cols; ++i)
        {
            const float v = std::clamp ((db[i] - dbLow) / (dbHigh - dbLow), 0.0f, 1.04f);
            const int top = std::max (0, oy - (int) std::lround (v * sliceHeight)), x = ox + i;
            if (x < 0 || x >= hdui::width)
                continue;
            int& h = horizon[x];
            uint32_t* column = r.px + x;
            for (int y = top + 1, floor = std::min ({ h, oy + 1, hdui::height }); y < floor; ++y) // under the line
                column[y * r.stride] = fill;
            const int a = prev >= 0 ? std::min (prev, top) : top, b = prev >= 0 ? std::max (prev, top) : top;
            for (int y = a, end = std::min ({ b + 1, h, hdui::height }); y < end; ++y) // the line, from the last column's
                column[y * r.stride] = line;
            h = std::min (h, a);
            prev = top;
        }
    }

    void drawWaterfall (hdui::Raster& r, const hdui::Host& h, int x0, int base)
    {
        const hdui::Ink hi = accent (h);
        const hdui::Ink bg = colours.bg;
        // Three frequency rails on the floor, in the same projection as the slices.
        // Draw first so every response occludes them, including a flat or nearly silent filter.
        static constexpr int ticks[] = { 26, 63, 100 }; // 100 Hz, 1 kHz, 10 kHz on 112 log-spaced columns
        const int rearX = x0 + (int) std::lround ((slices - 1) * sliceDx);
        const int rearY = base + (int) std::lround ((slices - 1) * sliceDy);
        r.box (rearX, rearY, cols, 1, grid);
        for (int tick : ticks)
            r.line (x0 + tick, base, rearX + tick, rearY, 0xff202039);
        r.line (x0 + cols - 1, base, rearX + cols - 1, rearY, 0xff36314f);
        int horizon[hdui::width];
        std::fill (horizon, horizon + hdui::width, hdui::height);
        if (viewMorph)
        {
            // MORPH: the Morph axis from 0 (front) to 1 (back), the response now lit where it is.
            // The landscape follows Freq and Transform a few slices a frame (all of it within four frames), so a
            // moving point costs a few responses a frame, not twelve; a new cube, Emphasis or rate redraws it at once.
            const int cube = store.cubeOf (slotNow);
            const float emphasis = value (ParamId::emphasis), level = levelOf (slotNow);
            const bool reshaped = cube != morphCube || emphasis != morphEmphasis || rate != morphRate;
            const int start = morphNext;
            for (int k = 0, budget = reshaped ? morphSlices : 3; k < morphSlices && budget > 0; ++k)
            {
                const int s = (start + k) % morphSlices;
                if (! reshaped && morphAt[s].y == pointL.y && morphAt[s].z == pointL.z)
                    continue;
                Design d;
                designCube (cube, { (float) s / (morphSlices - 1), pointL.y, pointL.z }, emphasis, rate, d);
                for (int i = 0; i < cols; ++i)
                    morphDb[s][i] = responseDbAt (d, columnT[i]) + level;
                morphAt[s] = { 0.0f, pointL.y, pointL.z };
                morphNext = (s + 1) % morphSlices;
                --budget;
            }
            morphCube = cube, morphEmphasis = emphasis, morphRate = rate;
            const float now = pointL.x;
            bool nowDrawn = false;
            for (int s = 0; s < morphSlices; ++s) // front to back, the response now where it sits among them
            {
                const float t = (float) s / (morphSlices - 1);
                if (! nowDrawn && now <= t)
                {
                    drawSlice (r, horizon, sliceDb[sliceHead], x0, base, now, white, hdui::mix (bg, hi, 70));
                    nowDrawn = true;
                }
                const hdui::Ink ink = sliceInk (0.25f + 0.75f * t, hdui::mix (hi, colours.muted, 128));
                drawSlice (r, horizon, morphDb[s], x0, base, t, ink, hdui::mix (bg, ink, 22));
            }
        }
        else
        {
            // TIME: newest in front.
            for (int age = 0; age < sliceCount; ++age)
            {
                const float t = (float) age / (slices - 1);
                const hdui::Ink ink = age == 0 ? hi : sliceInk (t, hi);
                drawSlice (r, horizon, sliceDb[(sliceHead - age + slices) % slices], x0, base, t, ink, hdui::mix (bg, ink, age == 0 ? 40 : 22));
            }
            if (splitNow)
                drawSliceLine (r, rightDb, x0, base, magenta);
        }
        // The three offset traces echo the waterfall next to its view name.
        const int keyY = base - 136;
        for (int s = 0; s < 3; ++s)
        {
            const int x = x0 + 2 + 2 * s, y = keyY + 8 - 3 * s;
            const hdui::Ink ink = s == 0 ? (viewMorph ? white : hi) : (s == 1 ? magenta : edgeFar);
            r.line (x, y, x + 2, y - 2, ink);
            r.box (x + 2, y - 2, 5, 1, ink);
        }
        r.text (x0 + 17, keyY, viewMorph ? "MORPH ->" : "TIME", colours.muted);
        // The front's floor, its 0 dB and its frequencies.
        r.box (x0, base + 1, cols, 1, colours.rule);
        const int zero = base - (int) std::lround ((0.0f - dbLow) / (dbHigh - dbLow) * sliceHeight);
        for (int i = 0; i < cols; i += 4)
            r.dot (x0 + i, zero, hdui::mix (bg, colours.text, 90));
        static constexpr const char* labels[] = { "100", "1K", "10K" };
        for (int i = 0; i < 3; ++i)
        {
            const int at = x0 + ticks[i];
            r.box (at, base + 1, 1, 3, colours.muted);
            r.text (at - hdui::textWidth (labels[i]) / 2, base + 3, labels[i], colours.muted);
        }
    }

    // The right channel's response, a line over the front slice.
    void drawSliceLine (hdui::Raster& r, const float* db, int x0, int base, hdui::Ink c) const
    {
        for (int i = 0; i < cols; i += 2)
        {
            const float v = std::clamp ((db[i] - dbLow) / (dbHigh - dbLow), 0.0f, 1.04f);
            r.box (x0 + i, base - (int) std::lround (v * sliceHeight), 1, 1, c);
        }
    }

    // --- SEQ ------------------------------------------------------------------------------------------------------

    // What a step may become: every filter in use, the commands, then Insert and Delete.
    void buildChoices (int current)
    {
        choiceCount = 0;
        for (int s = 1; s <= slotCount; ++s)
            if (store.used (s))
                choices[choiceCount++] = s;
        choices[choiceCount++] = stepPause, choices[choiceCount++] = stepHalt, choices[choiceCount++] = stepBumper;
        for (int k = 0; k < fadeScaleCount; ++k)
            choices[choiceCount++] = stepFade + k;
        choices[choiceCount++] = choiceInsert, choices[choiceCount++] = choiceDelete;
        choice = 0;
        for (int i = 0; i < choiceCount; ++i)
            if (choices[i] == current)
                choice = i;
    }

    void choiceText (int code, char* text, size_t size) const
    {
        if (code == choiceInsert)
            std::snprintf (text, size, "INSERT STEP");
        else if (code == choiceDelete)
            std::snprintf (text, size, "DELETE STEP");
        else
            stepText (store, code, text, size), upper (text);
    }

    // The sequence's rows: its steps, then Add Step, then Restore.
    bool seqInput (hdui::Key key, int amount, hdui::Edits&)
    {
        const int q = sequenceNow();
        if (q != seqShown)
            seqShown = q, seqCursor = 0, editingStep = false, seqRestoreArmed = false;
        if (q == 0)
            return key != hdui::Key::nudge;
        const int length = store.length (q), rows = length + 2;
        seqCursor = std::clamp (seqCursor, 0, rows - 1);
        if (editingStep)
        {
            if (key == hdui::Key::wheel)
                choice = std::clamp (choice + amount, 0, choiceCount - 1);
            else if (key == hdui::Key::nudge)
                choice = std::clamp (choice + 8 * (amount < 0 ? -1 : 1), 0, choiceCount - 1);
            else if (key == hdui::Key::push)
            {
                const int code = choices[choice];
                if (code == choiceInsert)
                    store.insertStep (q, seqCursor, stepPause);
                else if (code == choiceDelete)
                    store.deleteStep (q, seqCursor), seqCursor = std::min (seqCursor, store.length (q));
                else
                    store.setStep (q, seqCursor, code);
                editingStep = false;
            }
            return true;
        }
        if (key == hdui::Key::wheel)
        {
            seqRestoreArmed = false;
            seqCursor = std::clamp (seqCursor + amount, 0, rows - 1);
            return true;
        }
        if (key != hdui::Key::push)
            return false;
        if (seqCursor < length)
        {
            seqRestoreArmed = false;
            buildChoices (store.step (q, seqCursor));
            editingStep = true;
        }
        else if (seqCursor == length) // Add Step: the filter Filter picks, at the end
        {
            seqRestoreArmed = false;
            if (store.insertStep (q, length, editSlot()))
                seqCursor = length + 1;
        }
        else if (! seqRestoreArmed)
            seqRestoreArmed = true;
        else
        {
            seqRestoreArmed = false;
            store.factorySequence (q);
            seqCursor = std::min (seqCursor, store.length (q) + 1);
        }
        return true;
    }

    void drawSequence (hdui::Raster& r, const hdui::Host& h, int x, int y, int w, int height)
    {
        const hdui::Ink hi = accent (h);
        const int q = sequenceNow();
        if (q != seqShown)
            seqShown = q, seqCursor = 0, editingStep = false, seqRestoreArmed = false;
        if (q == 0)
        {
            const char* a = "SEQUENCER OFF";
            const char* b = "TURN K1 TO PICK A SEQUENCE";
            r.text (x + (w - hdui::textWidth (a, 64, 2)) / 2, y + height / 2 - 24, a, colours.text, 64, 2);
            r.text (x + (w - hdui::textWidth (b)) / 2, y + height / 2 + 8, b, colours.muted);
            return;
        }
        const int length = store.length (q), rows = length + 2;
        seqCursor = std::clamp (seqCursor, 0, rows - 1);
        // The header: the sequence, its length, where it is.
        char text[64], name[32];
        std::snprintf (name, sizeof (name), "%s", sequenceChoices[q]);
        upper (name);
        std::snprintf (text, sizeof (text), "S%02d %s", q, name);
        const bool flash = h.nowMs < flashUntilMs;
        r.text (x + 6, y + 2, text, flash ? yellow : hi);
        const bool running = feed.sequence.load (std::memory_order_relaxed) == q;
        const int playing = running ? feed.step.load (std::memory_order_relaxed) : -1, mode = feed.mode.load (std::memory_order_relaxed);
        std::snprintf (text, sizeof (text), "%d/%d STEPS", length, maxSteps);
        if (running && feed.fade.load (std::memory_order_relaxed) != unityFade)
            std::snprintf (text + std::strlen (text), sizeof (text) - std::strlen (text), "  %s", fadeScaleNames[feed.fade.load (std::memory_order_relaxed)]);
        r.text (x + w - 6 - hdui::textWidth (text), y + 2, text, colours.muted);
        r.box (x, y + 14, w, 1, colours.rule);

        // Seven rows round the cursor, its own large.
        const int shown = 7, top = std::clamp (seqCursor - 3, 0, std::max (0, rows - shown));
        int at = y + 18;
        for (int row = top; row < std::min (rows, top + shown); ++row)
        {
            const bool here = row == seqCursor;
            const int rowH = here ? 28 : 13, scale = here ? 2 : 1;
            if (here)
            {
                r.box (x, at, w, rowH, colours.card);
                r.box (x, at, 3, rowH, hi);
            }
            const int ty = at + (here ? 2 : 1);
            if (row < length)
            {
                const int code = store.step (q, row);
                std::snprintf (text, sizeof (text), "%02d", row + 1);
                r.text (x + 8, at + (here ? 8 : 1), text, row == playing ? hi : colours.dim);
                if (row == playing)
                    r.text (x + 24, at + (here ? 8 : 1), mode == 0 ? ">" : (mode == (int) Sequencer::Mode::paused ? "P" : "H"), flash ? yellow : hi);
                const bool edit = here && editingStep;
                choiceText (edit ? choices[choice] : code, text, sizeof (text));
                const int room = (w - 40) / (hdui::font::glyphWidth * scale);
                const hdui::Ink ink = edit ? hi : (here ? colours.text : (isFilterStep (code) ? colours.muted : colours.dim));
                const int end = r.text (x + 34, ty, text, ink, room, scale);
                if (edit)
                    r.frame (x + 30, at + 1, std::min (end - x - 26, w - 32), rowH - 2, hi);
            }
            else if (row == length)
            {
                const char* add = length >= maxSteps ? "SEQUENCE FULL" : "+ ADD STEP";
                r.text (x + 34, ty, add, here ? colours.text : colours.dim, 32, scale);
            }
            else
                r.text (x + 34, ty, seqRestoreArmed ? "PUSH AGAIN" : "RESTORE", here ? (seqRestoreArmed ? red : colours.text) : colours.dim, 32, scale);
            at += rowH;
        }
        // Under the list, what a push does here.
        const char* hint = editingStep ? "WHEEL CHOOSE  -/+ JUMP  PUSH SET" : (seqCursor < length ? "PUSH TO EDIT THIS STEP"
                                                                              : (seqCursor == length ? "PUSH ADDS THE FILTER ON FILTER"
                                                                                                     : "PUSH TWICE: FACTORY SEQUENCE"));
        r.text (x + (w - hdui::textWidth (hint)) / 2, y + height - 14, hint, colours.dim);
    }

    // Beside TRIG: the sequence's steps as squares (filters lit, commands dim), the one it is at marked, and its place.
    void drawSteps (hdui::Raster& r, const hdui::Host& h, int x, int y, int w)
    {
        const hdui::Ink hi = accent (h);
        const int q = sequenceNow();
        char text[40];
        sequenceStatus (text, sizeof (text), (w - 2) / hdui::font::glyphWidth);
        r.text (x, y + 2, text, h.nowMs < flashUntilMs ? yellow : hi);
        if (q == 0)
            return;
        const int length = store.length (q);
        const bool running = feed.sequence.load (std::memory_order_relaxed) == q;
        const int playing = running ? feed.step.load (std::memory_order_relaxed) : -1;
        const int size = 13, gap = 2, perRow = std::max (1, (w + gap) / (size + gap));
        for (int i = 0; i < length; ++i)
        {
            const int sx = x + (i % perRow) * (size + gap), sy = y + 18 + (i / perRow) * (size + gap);
            const int code = store.step (q, i);
            const hdui::Ink ink = i == playing ? hi : (isFilterStep (code) ? colours.muted : colours.rule);
            if (isFilterStep (code))
                r.box (sx, sy, size, size, i == playing ? hi : colours.card);
            r.frame (sx, sy, size, size, ink);
            if (! isFilterStep (code))
            {
                const char* mark = code == stepPause ? "P" : (code == stepHalt ? "H" : (code == stepBumper ? "|" : "x"));
                r.text (sx + 3, sy + 1, mark, ink);
            }
        }
    }

    // The groups. CUBE and SEQ draw themselves; the rest list the parameters.
    static constexpr int filterCount = 17;
    static constexpr hdui::Field filterRows[filterCount] = {
        { ParamId::filter, "FILTER" }, { ParamId::editCube, "CUBE" }, { ParamId::morphOffset, "MORPH OFS" }, { ParamId::freqOffset, "FREQ OFS" },
        { ParamId::transformOffset, "XFORM OFS" }, { ParamId::morphGain, "MORPH GAIN" }, { ParamId::freqGain, "FREQ GAIN" },
        { ParamId::transformGain, "XFORM GAIN" }, { ParamId::enterFade, "ENTER FADE" }, { ParamId::exitFade, "EXIT FADE" },
        { ParamId::gainLeft, "L GAIN" }, { ParamId::gainRight, "R GAIN" }, { ParamId::distLeft, "L DIST" }, { ParamId::distRight, "R DIST" },
        { ParamId::transformDist, "XFORM>DIST" }, { copyButton, "COPY" }, { restoreButton, "RESTORE" },
    };
    hdui::Field filterFields[filterCount] {};
    static constexpr hdui::Field trig[] = {
        { ParamId::sequence, "SEQUENCE" }, { ParamId::incClock, "INC CLOCK" }, { ParamId::decClock, "DEC CLOCK" },
        { ParamId::resetClock, "RESET CLOCK" }, { ParamId::envTrigger, "ENV TRIG" }, { ParamId::trigLevel, "TRIG LEVEL" },
        { ParamId::trigInc, "INC" }, { ParamId::trigDec, "DEC" }, { ParamId::trigReset, "RESET" },
    };
    static constexpr hdui::Field knobsRows[] = {
        { ParamId::morph, "MORPH" }, { ParamId::freq, "FREQ" }, { ParamId::transform, "TRANSFORM" }, { ParamId::emphasis, "EMPHASIS" },
        { ParamId::glide, "GLIDE" },
    };
    static constexpr hdui::Field lfo[] = {
        { ParamId::lfoSync, "SYNC" }, { ParamId::lfoRate, "RATE" }, { ParamId::lfoShape, "SHAPE" }, { ParamId::lfoPhase, "PHASE" },
        { ParamId::lfoMorph, "LFO>MORPH" }, { ParamId::lfoFreq, "LFO>FREQ" }, { ParamId::lfoTransform, "LFO>XFORM" },
    };
    static constexpr hdui::Field envelope[] = {
        { ParamId::envGain, "ENV GAIN" }, { ParamId::envAttack, "ATTACK" }, { ParamId::envRelease, "RELEASE" },
        { ParamId::envMorph, "ENV>MORPH" }, { ParamId::envFreq, "ENV>FREQ" }, { ParamId::envTransform, "ENV>XFORM" },
    };
    static constexpr hdui::Field out[] = {
        { ParamId::drive, "DRIVE" }, { ParamId::mix, "MIX" }, { ParamId::output, "OUTPUT" }, { ParamId::spread, "SPREAD" },
    };
    hdui::Group groupList[8] {};

public:
    // Before each paint: Transform's gain reads as the distortion's while the edit filter gives Transform to it.
    void relabel()
    {
        const bool drivesEdit = store.used (editSlot()) && store.field (editSlot(), fieldTransformDist) >= 0.5f;
        filterFields[7].label = drivesEdit ? "DIST GAIN" : "XFORM GAIN";
    }
};
} // namespace cortex
