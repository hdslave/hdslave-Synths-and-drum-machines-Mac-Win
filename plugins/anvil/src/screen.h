#pragma once

#include "params.h"
#include "ui/groups.h"

#include "ui/masthead.h"
#include "ui/torus.h"
#include <atomic>
#include <iterator>

namespace anvil
{
// The page's own button: Push on STEPS plays the step picked. (Not a parameter: ids from here on are the screen's.)
constexpr uint32_t auditionId = 1000;

// What the audio thread says for the page: the step it is on and how many hits it has played.
struct StepState
{
    std::atomic<int> step { 0 };
    std::atomic<uint32_t> hits { 0 };
    std::atomic<bool> running { false };
};

class Screen final
{
    using Raster = hdui::Raster;
    using Host = hdui::Host;
    using Ink = hdui::Ink;

public:
    Screen (const std::atomic<float>* values, const StepState& state) : values (values), state (state) {}

    void update (uint64_t now)
    {
        const auto hits = state.hits.load (std::memory_order_acquire);
        const bool struck = hits != seenHits;
        const bool first = lastGroup < 0 || now < lastHost.nowMs;
        if (first) { motionStart = now; hasHit = false; }
        if (struck) { seenHits = hits; hitAt = now; hasHit = true; }
        step = std::clamp (state.step.load (std::memory_order_relaxed), 0, 7);
        running = state.running.load (std::memory_order_relaxed);
    }

    int pickedStep() const { return picked; }

    void knobs (uint32_t ids[3]) const
    {
        for (int k = 0; k < 3; ++k)
        {
            const auto id = cardField (k);
            ids[k] = id != hdui::noParam && paramTable[paramIndex (id)].kind != Kind::button ? id : hdui::noParam;
        }
    }

    bool input (hdui::Key key, int amount, bool shift, hdui::Edits& out)
    {
        using hdui::Key;
        if (key == Key::nudge && !editing)
        {
            const int next = std::clamp (group + (amount > 0) - (amount < 0), 0, 6);
            if (next == group) return false;
            group = next;
            row = 0;
            return true;
        }
        if (group == 0)
        {
            if (key == Key::push) { out.press (auditionId + picked); return true; }
            if (key != Key::wheel) return false;
            const int next = int (std::clamp<int64_t> (int64_t (picked) + amount, 0, 7));
            if (next == picked) return false;
            picked = next;
            return true;
        }
        const auto id = fields().fields[row].id;
        const Param& p = paramTable[paramIndex (id)];
        if (key == Key::push)
        {
            if (p.kind == Kind::button) out.press (id);
            else editing = !editing;
        }
        else if (!editing && key == Key::wheel)
            row = int (std::clamp<int64_t> (int64_t (row) + amount, 0, fields().count - 1));
        else if (editing && (key == Key::wheel || key == Key::nudge))
        {
            const float delta = p.kind == Kind::continuous
                ? (p.max - p.min) / (key == Key::nudge ? 16 : (shift ? 1024 : 128)) : 1.f;
            const float before = liveValue (id);
            const float next = clampParam (p, before + delta * amount);
            if (next != before) out.set (id, next);
        }
        else return false;
        return true;
    }

    bool paint (const Host& h, uint32_t* pixels, int stride, bool whole)
    {
        whole |= lastGroup < 0;
        const bool page = whole || group != lastGroup;
        const bool selection = page || lastRow != row || lastPicked != picked || lastEdit != editing;
        const bool sequence = lastStep != step || lastRun != running || lastHost.playing != h.playing;
        const int flash = hitLevel (h);
        bool parameters = false, pots = false;
        for (unsigned i = 0; i < paramCount; ++i)
        {
            const float v = values[i].load (std::memory_order_relaxed);
            float& prior = sampled[paramTable[i].id];
            parameters |= v != prior;
            prior = v;
        }
        for (int k = 0; k < 3; ++k)
            pots |= h.pot[k] != lastHost.pot[k] || h.engaged[k] != lastHost.engaged[k];
        bool drew = false;
        const uint64_t tick = (h.nowMs - motionStart) / hdui::torus::frameMs;
        const int pose = int (tick % hdui::masthead::frames);
        const int glint = hdui::masthead::glintFrame (tick);
        const bool motion = whole || pose != lastPose;
        const float cycle = float (pose % hdui::torus::frames) / hdui::torus::frames;
        const float phase = cycle * hdui::torus::tau;
        const float travel = 2.f * cycle; // one light/dark pair moves through per turn
        const hdui::print::Drift drift (pose * hdui::torus::tau / hdui::masthead::frames);
        if (motion)
        {
            drew |= picture.region<160,60> (pixels, stride, whole, 0, 0, [&](Raster& r) {
                hdui::print::underprint (r, 0, drift);
                hdui::masthead::draw (r, 0, phase, travel, drift.inkOffset);
            });
        }
        if (motion || glint != lastGlint)
        {
            drew |= picture.region<80,60> (pixels, stride, whole, 160, 0, [&](Raster& r) {
                hdui::print::underprint (r, 160, drift);
                hdui::torus::Outlet outlet;
                hdui::torus::draw (r, phase, outlet, drift.inkOffset);
                hdui::masthead::draw (r, 160, phase, travel, drift.inkOffset, &outlet);
                hdui::masthead::drawGlint (r, glint);
            });
        }
        if (motion || page || sequence || parameters || lastFlash != flash)
            drew |= picture.region<80,60> (pixels, stride, whole, 240, 0, [&](Raster& r) {
                hdui::print::underprint (r, 240, drift);
                hdui::masthead::draw (r, 240, phase, travel, drift.inkOffset);
                status (r, h, flash);
            });
        if (group == 0)
        {
            if (page || parameters)
                drew |= picture.region<320,16> (pixels, stride, whole, 0, 60, [&](Raster& r) { clockLine (r); });
            if (selection || sequence || parameters)
            {
                drew |= picture.region<320,63> (pixels, stride, whole, 0, 76, [&](Raster& r) { pitches (r, h); });
                drew |= picture.region<320,31> (pixels, stride, whole, 0, 139, [&](Raster& r) { velocities (r, h); });
            }
        }
        else
        {
            if (selection || parameters)
                drew |= picture.region<180,110> (pixels, stride, whole, 0, 60, [&](Raster& r) { list (r); });
            if (page || parameters || sequence)
                drew |= picture.region<140,110> (pixels, stride, whole, 180, 60, [&](Raster& r) { detail (r, h); });
        }
        if (selection || parameters || pots || h.shift != lastHost.shift || h.accent != lastHost.accent)
            drew |= picture.region<320,48> (pixels, stride, whole, 0, 170, [&](Raster& r) {
                if (h.shift) legend (r);
                else cards (r, h);
            });
        lastRow = row;
        lastGroup = group;
        lastPicked = picked;
        lastStep = step;
        lastRun = running;
        lastEdit = editing;
        lastFlash = flash;
        lastPose = pose;
        lastGlint = glint;
        lastHost = h;
        return drew;
    }

private:
    const std::atomic<float>* values;
    const StepState& state;
    hdui::Picture picture;
    Host lastHost {};
    float sampled[ParamId::count] {};
    int group = 0, row = 0, picked = 0, step = 0;
    int lastGroup = -1, lastRow = 0, lastPicked = 0, lastStep = 0, lastFlash = 0;
    int lastPose = -1, lastGlint = -1;
    bool running = false, editing = false, lastEdit = false, lastRun = false, hasHit = false;
    uint32_t seenHits = 0;
    uint64_t hitAt = 0, motionStart = 0;

    int hitLevel (const Host& h) const
    {
        if (!hasHit || h.nowMs < hitAt || h.nowMs - hitAt >= 144) return 0;
        return 3 - int ((h.nowMs - hitAt) / 48);
    }
    float liveValue (uint32_t id) const { return values[paramIndex (id)].load (std::memory_order_relaxed); }
    float val (uint32_t id) const { return sampled[id]; }
    const hdui::Group& fields() const { return layout::groups[group]; }

    template<size_t N> void formatted (uint32_t id, char (&s)[N]) const
    {
        formatParam (paramTable[paramIndex (id)], val (id), s, N, { val (ParamId::quantize) });
    }
    uint32_t cardField (int k) const
    {
        if (group == 0)
        {
            const uint32_t ids[] = { ParamId::pitch1 + unsigned (picked), ParamId::velocity1 + unsigned (picked), ParamId::cutoff };
            return ids[k];
        }
        return row + k < fields().count ? fields().fields[row + k].id : hdui::noParam;
    }

    void status (const Raster& r, const Host& h, int flash) const
    {
        using namespace hdui;
        r.box (2, 5, 1, 25, ink::grid);
        r.text (8, 4, fields().name, ink::paper);
        char s[20];
        std::snprintf (s, sizeof (s), "%02d/%02d", step + 1, int (val (ParamId::length)));
        r.text (46, 4, s, ink::paper, 6);
        for (int i = 0; i < 7; ++i) r.box (9 + i*9, 15, 7, 2, group == i ? ink::soft : ink::grid);
        const bool armed = val (ParamId::run) >= .5f;
        r.text (9, 21, !armed ? "STOP" : (h.playing ? "RUN" : "ARMED"), ink::muted);
        if (flash) // Hit count also covers auditions, MIDI triggers and ratchets.
        {
            const int cx = 69, cy = 25;
            r.box (cx - flash, cy, 2 * flash + 1, 1, ink::live);
            r.box (cx, cy - flash, 1, 2 * flash + 1, ink::live);
        }
        r.box (0, 59, 76, 1, ink::rule);
    }

    void clockLine (const Raster& r) const
    {
        using namespace hdui;
        char s[32];
        formatted (ParamId::clockDiv, s);
        r.text (7, 4, s, ink::paper);
        formatted (ParamId::direction, s);
        r.text (60, 4, s, ink::muted);
        std::snprintf (s, sizeof (s), "Q / %s", quantizeChoices[int (val (ParamId::quantize))]);
        r.right (312, 4, s, ink::soft);
    }

    static constexpr int lane = 34, left = 42;
    bool playing (int s, const Host& h) const
    {
        return h.playing && running && val (ParamId::run) >= .5f && s == step && s < int (val (ParamId::length));
    }
    Ink stepInk (int s, const Host& h) const
    {
        using namespace hdui;
        return playing (s, h) ? ink::live : (s < int (val (ParamId::length)) ? ink::soft : ink::parked);
    }
    float pitch (int s) const
    {
        return quantizedSemitones (val (ParamId::pitch1 + s), int (val (ParamId::quantize)));
    }
    int pitchRange() const
    {
        float peak = 0;
        for (int s = 0; s < 8; ++s) peak = std::max (peak, std::fabs (pitch (s)));
        return std::max (12, 12 * int (std::ceil (peak / 12)));
    }

    void pitches (const Raster& r, const Host& h) const
    {
        using namespace hdui;
        const int span = pitchRange();
        r.box (4, 0, 312, r.h, ink::field);
        r.text (7, 4, "PITCH", ink::muted);
        char s[24];
        std::snprintf (s, sizeof (s), "+%d", span);
        r.right (36, 19, s, ink::muted);
        std::snprintf (s, sizeof (s), "-%d", span);
        r.right (36, 41, s, ink::muted);
        for (int i = 0; i < 8; ++i)
        {
            const int x = left + i * lane;
            const bool live = playing (i, h);
            const auto colour = stepInk (i, h);
            std::snprintf (s, sizeof (s), "%02d", i + 1);
            if (live) { r.box (x + 3, 1, 28, 13, ink::cursor); r.box (x + 3, 0, 28, 2, ink::live); }
            r.text (x + 10, 4, s, i == picked ? ink::paper : colour);
            if (picked == i)
            {
                for (int edge : {x + 1, x + lane - 3})
                {
                    r.box (edge, 3, 1, 10, ink::paper);
                    r.box (edge == x + 1 ? edge : edge - 3, 3, 4, 1, ink::paper);
                    r.box (edge == x + 1 ? edge : edge - 3, 12, 4, 1, ink::paper);
                }
            }
            constexpr int zero = 34;
            r.box (x, zero, lane, 1, ink::rule);
            for (int y = 18; y < 49; y += 4) r.dot (x, y, ink::grid);
            const int offset = int (std::lround (pitch (i) * 14 / span));
            // A rising/falling four-terrace glyph. The final shelf is its pitch.
            for (int j = 0; j < 4; ++j)
            {
                const int dy = int (std::lround (offset * (j + 1) * .25f));
                const int y = zero - dy;
                r.box (x + 3 + j * 7, std::min (zero, y), 7, std::abs (dy) + 1,
                       live ? ink::cursor : ink::grid);
                r.box (x + 3 + j * 7, y, 7, 2, colour);
            }
            formatted (ParamId::pitch1 + i, s);
            if (char* unit = std::strchr (s, ' ')) *unit = 0;
            r.text (x + (lane - int (std::strlen (s))*6) / 2, 52, s, colour, 6);
        }
    }

    void velocities (const Raster& r, const Host& h) const
    {
        using namespace hdui;
        r.box (4, 0, 312, 30, ink::field);
        r.box (6, 0, 308, 1, ink::rule);
        r.text (7, 11, "VEL", ink::muted);
        for (int i = 0; i < 8; ++i)
        {
            const int x = left + i * lane, middle = x + lane / 2;
            const int height = int (std::lround (val (ParamId::velocity1 + i) * 23));
            const auto colour = stepInk (i, h);
            r.box (x + 2, 26, lane - 4, 1, ink::grid);
            if (!height) { r.box (middle - 1, 25, 3, 1, colour); continue; }
            for (int j = 0; j < height; j += 3)
            {
                const int radius = 1 + (height - 1 - j) * 12 / height;
                r.box (middle - radius, 25 - j, 2 * radius + 1, 2, colour);
            }
            r.dot (middle, 26 - height, colour);
        }
    }
    static constexpr int visible = 6, rowH = 17;
    int firstRow() const { return std::clamp (row - 3, 0, std::max (0, fields().count - visible)); }

    void listRow (const Raster& r, int i, int x, int y) const
    {
        using namespace hdui;
        const auto& f = fields().fields[i];
        const bool focus = i == row;
        if (focus)
        {
            r.box (x, y, 170, rowH - 1, ink::selected);
            r.box (x, y, 2, rowH - 1, ink::soft);
        }
        char s[32];
        formatted (f.id, s);
        r.text (x + 5, y + 4, f.label, focus ? ink::paper : ink::muted);
        const int end = x + 164;
        if (focus && editing)
        {
            const int w = int (std::strlen (s)) * 7;
            r.frame (end - w - 3, y + 1, w + 6, 14, ink::paper);
        }
        r.right (end, y + 4, s, focus ? ink::paper : ink::soft);
    }

    void list (const Raster& r) const
    {
        using namespace hdui;
        const int top = firstRow();
        for (int i = top; i < std::min (fields().count, top + visible); ++i)
            listRow (r, i, 4, 5 + (i - top) * rowH);
        if (fields().count > visible)
        {
            const int extent = visible * rowH;
            r.box (176, 5, 1, extent, ink::grid);
            r.box (176, 5 + top * extent / fields().count, 1, visible * extent / fields().count, ink::muted);
        }
    }

    void detail (const Raster& r, const Host& h) const
    {
        using namespace hdui;
        r.box (0, 4, 136, 102, ink::field);
        r.text (6, 9, "STEPS", ink::muted);
        char s[24];
        formatted (ParamId::clockDiv, s);
        r.right (131, 9, s, ink::paper);
        const int span = pitchRange();
        r.box (5, 33, 125, 1, ink::grid);
        for (int i = 0; i < 8; ++i)
        {
            const int x = 6 + i * 16;
            const auto colour = stepInk (i, h);
            const int y = 33 - int (std::lround (pitch (i) * 10 / span));
            r.box (x, y, 12, 2, colour);
            const int vel = int (std::lround (val (ParamId::velocity1 + i) * 10));
            for (int j = 0; j < vel; j += 2) r.box (x + j / 2, 57 - j, 12 - j, 1, colour);
            if (playing (i, h)) r.box (x, 20, 12, 1, ink::live);
        }
        r.box (5, 63, 125, 1, ink::grid);
        if (group == 1)
        {
            r.text (6, 70, "VCO 1", ink::muted);
            r.text (76, 70, "VCO 2", ink::muted);
            for (int n = 0; n < 2; ++n)
            {
                const int x = 6 + n * 70;
                const bool tri = val (n ? ParamId::vco2Wave : ParamId::vco1Wave) >= .5f;
                int last = 0;
                for (int j = 0; j < 52; ++j)
                {
                    const int k = j % 24;
                    const int y = tri ? 87 + std::abs (12 - k) : (k < 12 ? 87 : 99);
                    r.box (x+j, j ? std::min (last,y) : y, 1, j ? std::abs (last-y)+1 : 1, ink::soft);
                    last = y;
                }
            }
        }
        else if (group == 2 || group == 5)
        {
            const bool patch = group == 5;
            const char* names[] = { patch ? "V>C" : "V1", patch ? "P>C" : "NOISE", patch ? "E>2" : "V2" };
            const uint32_t mix[] = { ParamId::vco1Level, ParamId::noiseLevel, ParamId::vco2Level };
            const uint32_t routes[] = { ParamId::velCutoff, ParamId::pitchCutoff, ParamId::vcfEgVco2 };
            for (int k = 0; k < 3; ++k)
            {
                const int x = 6 + 43*k, origin = x + (patch ? 18 : 0);
                r.text (x, 72, names[k], ink::muted);
                const int at = origin + int (std::lround (val (patch ? routes[k] : mix[k]) * (patch ? 17 : 34)));
                r.box (x, 92, 36, 1, ink::rule);
                r.box (std::min (origin, at), 91, std::abs (at-origin)+1, 3, ink::soft);
                if (patch) r.dot (origin, 96, ink::muted);
            }
        }
        else if (group == 3 || group == 4)
        {
            const bool vca = group == 4;
            char label[24];
            formatted (vca ? ParamId::vcaEg : ParamId::vcfMode, label);
            r.text (6, 70, label, ink::paper);
            formatted (vca ? ParamId::vcaDecay : ParamId::vcfDecay, s);
            r.right (130, 70, s, ink::muted);
            // A single full-velocity envelope from silence over one second.
            const double attack = vca && val (ParamId::vcaEg) > .5f ? attackSlow : attackFast;
            const double decay = decaySeconds (val (vca ? ParamId::vcaDecay : ParamId::vcfDecay));
            int last = 100;
            for (int j = 0; j <= 122; ++j)
            {
                const double t = j / 122.0;
                const double amp = t < attack ? 1.25 * (1 - std::exp (-std::log (5.0)*t/attack))
                                              : std::exp (-std::log (1000.0)*(t - attack)/decay);
                const int y = 100 - int (std::lround (15 * amp));
                r.box (6+j, std::min (last,y), 1, std::abs (last-y)+1, ink::soft);
                last = y;
            }
        }
        else
        {
            formatted (ParamId::swing, s);
            r.text (6, 70, "SWING", ink::muted);
            r.right (130, 70, s, ink::paper);
            const int split = int (std::lround (val (ParamId::swing) * 120));
            r.box (7, 92, split - 1, 4, ink::soft);
            r.box (7 + split + 1, 92, 119 - split, 4, ink::rule);
            r.dot (67, 99, ink::muted);
        }
    }
    static void legend (const Raster& r, const char* const top[3], const char* const bottom[3])
    {
        using namespace hdui;
        for (int k = 0; k < 3; ++k)
        {
            const int x = 4 + 105 * k;
            r.box (x, 3, 102, 40, ink::field);
            r.text (x + 5, 10, top[k], ink::muted);
            r.text (x + 5, 26, bottom[k], ink::paper);
        }
    }

    void legend (const Raster& r) const
    {
        const char* top[] = { "WHEEL", "PUSH", "MINUS / PLUS" };
        const char* bottom[] = { "Pick step", "Audition", "Group" };
        if (group)
        {
            top[0] = editing ? "SHIFT + WHEEL" : "WHEEL";
            bottom[0] = editing ? "Fine value" : "Move cursor";
            bottom[1] = editing ? "Finish edit" : "Edit / press";
            bottom[2] = editing ? "Coarse value" : "Group";
        }
        legend (r, top, bottom);
    }

    void cards (const Raster& r, const Host& h) const
    {
        using namespace hdui;
        const Ink track = h.accent ? h.accent | 0xff000000u : ink::live;
        for (int k = 0; k < 3; ++k)
        {
            const int x = 4 + 105 * k;
            const uint32_t id = cardField (k);
            r.box (x, 3, 102, 40, ink::card);
            char tag[] = { 'K', char ('1' + k), 0 };
            r.text (x + 4, 8, tag, ink::muted);
            if (id == noParam) continue;
            const auto& p = paramTable[paramIndex (id)];
            r.text (x + 23, 8, group ? fields().fields[row+k].label : p.name, ink::paper);
            char s[32];
            formatted (id, s);
            if (p.kind == Kind::button)
            {
                r.text (x + 4, 23, k == 0 ? "Push" : "Button", ink::soft);
                continue;
            }
            r.text (x + 4, 21, s, ink::paper);
            const int start = x + 5, w = 92, rail = 35;
            const float unit = (val (id) - p.min) / (p.max - p.min);
            const int at = start + int (std::lround (unit * (w - 1)));
            r.box (start, rail, w, 2, ink::rule);
            const int origin = p.min < 0 ? start + (w - 1) / 2 : start;
            r.box (std::min (at, origin), rail, std::abs (at - origin) + 1, 2, track);
            r.box (at, rail - 2, 2, 5, track);
            if (h.pot[k] >= 0 && !h.engaged[k])
            {
                const int pos = start + std::clamp (int (h.pot[k]), 0, 127) * (w - 1) / 127;
                r.box (pos - 2, rail - 2, 5, 6, ink::card);
                r.frame (pos - 2, rail - 2, 5, 6, ink::paper);
            }
        }
    }
};
} // namespace anvil
