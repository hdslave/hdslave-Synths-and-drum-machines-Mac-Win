// Nexus's page on Melodee's screen (melodee_screen.h): 320 x 218 under the soft keys.
//
//   PLAITS  the model (its bank's grid and name), FREQ HARMONICS TIMBRE MORPH, the attenuverters, the low-pass gate,
//           OUT/AUX and a scope of what plays, with what the three macros do in this model.
//   VOICE   voice mode, notes, unison, detune, spread, pan, glide, bend, velocity and pressure curves, the filter
//           (with its response) and the eight voices at work.
//   ENV     both ADSRs (drawn, with where each is now), volume and the retrigger rate.
//   LFO     both LFOs (shape, rate, phase, sync, division, retrigger, per voice), each drawn and moving.
//   CYCLE   the cycling envelope and the random source, drawn and moving.
//   MOD     the fixed routes: LFO 1, Env 2, Cycle and Random to pitch, harmonics, timbre and morph.
//   ASSIGN  the assign matrix: four sources down, four targets across, the sixteen amounts between.
// The wheel moves between controls, Push edits one (and flips a switch at once), Minus and Plus turn the pages;
// K1-K3 hold the control picked and the next two. On the matrices Shift + Push turns a route on or off. Shift shows
// the controls. Nothing is redrawn while nothing changes; while voices sound the page moves at 25 frames a second.
#pragma once

#include "engine.h"
#include "params.h"
#include "ui/page.h"
#include "ui/wordmark.h"

#include <array>
#include <atomic>
#include <iterator>

namespace nexus
{
class Screen final
{
    using Raster = hdui::Raster;
    using Host = hdui::Host;
    using Ink = hdui::Ink;

public:
    Screen (const std::atomic<float>* values, const Telemetry& telemetry) : values (values), tel (telemetry)
    {
        for (int i = 0; i < paramCount; ++i) seen[i] = paramTable[i].def;
    }

    enum Page { plaitsPage, voicePage, envPage, lfoPage, cyclePage, modPage, assignPage, pageCount };

    void knobs (uint32_t ids[3]) const
    {
        const Layout l = layout (page);
        for (int k = 0; k < 3; ++k)
        {
            const int c = focus[page] + k;
            ids[k] = c < l.count ? l.cells[c].id : hdui::noParam;
        }
    }

    bool animating() const { return animated; }

    bool input (hdui::Key key, int amount, bool shift, hdui::Edits& out)
    {
        using hdui::Key;
        touched = true;
        if (key == Key::nudge && ! editing)
        {
            page = (page + (amount > 0 ? 1 : -1) + pageCount) % pageCount;
            return true;
        }
        const Layout l = layout (page);
        int& f = focus[page];
        f = std::clamp (f, 0, l.count - 1);
        const Cell& cell = l.cells[f];
        const Param& p = paramTable[cell.id];
        if (key == Key::push)
        {
            if (cell.style == matrix && shift)
            {
                out.set (cell.id + 1, live (cell.id + 1) >= 0.5f ? 0.0f : 1.0f);
                return true;
            }
            if (p.unit == Unit::onOff)
            {
                out.set (cell.id, live (cell.id) >= 0.5f ? 0.0f : 1.0f);
                return true;
            }
            editing = ! editing;
            return true;
        }
        if (! editing && key == Key::wheel)
        {
            f = std::clamp (f + amount, 0, l.count - 1);
            return true;
        }
        if (editing && (key == Key::wheel || key == Key::nudge))
        {
            const float delta = p.kind == Kind::continuous ? (p.max - p.min) / (key == Key::nudge ? 16.0f : (shift ? 1024.0f : 128.0f))
                                                           : (key == Key::nudge ? std::max (1.0f, std::round ((p.max - p.min) / 8.0f)) : 1.0f);
            const float before = live (cell.id);
            const float next = clampParam (p, before + delta * (float) amount);
            if (next != before) out.set (cell.id, next);
            return true;
        }
        return false;
    }

    bool paint (const Host& h, uint32_t* pixels, int stride, bool whole)
    {
        whole |= first;
        first = false;
        bool params = false;
        for (int i = 0; i < paramCount; ++i)
        {
            const float v = values[i].load (std::memory_order_relaxed);
            params |= v != seen[i];
            seen[i] = v;
        }
        const uint32_t sounding = tel.sounding.load (std::memory_order_relaxed);
        const uint32_t gated = tel.gated.load (std::memory_order_relaxed);
        animated = sounding != 0;
        const bool frame = animated && (h.nowMs - frameAt >= 40 || h.nowMs < frameAt);
        const bool voicesMoved = sounding != shownSounding || gated != shownGated;
        bool pots = false;
        for (int k = 0; k < 3; ++k)
            pots |= h.pot[k] != lastHost.pot[k] || h.engaged[k] != lastHost.engaged[k];
        const bool chrome = whole || page != shownPage || touched;
        if (! frame && ! pots && ! chrome && ! params && ! voicesMoved && h.shift == lastHost.shift && h.accent == lastHost.accent)
            return false;
        if (frame || voicesMoved)
        {
            frameAt = h.nowMs;
            sampleTelemetry();
        }
        bool drew = false;
        if (chrome || params || voicesMoved)
            drew |= picture.region<320, headerH> (pixels, stride, whole, 0, 0, [&] (Raster& r) { header (r, sounding, gated); });
        // The whole body when what it shows changed; between, only the pictures that move.
        if (chrome || params)
            drew |= picture.region<320, 142> (pixels, stride, whole, 0, headerH, [&] (Raster& r) { body (r, sounding, gated); });
        else if (frame || voicesMoved)
            drew |= moving (pixels, stride, sounding, gated);
        if (chrome || params || pots || h.shift != lastHost.shift || h.accent != lastHost.accent)
            drew |= picture.region<320, footerH> (pixels, stride, whole, 0, footerY, [&] (Raster& r) {
                if (h.shift) legend (r);
                else cards (r, h);
            });
        shownPage = page;
        shownSounding = sounding, shownGated = gated;
        lastHost = h;
        touched = false;
        return drew;
    }

private:
    // --- Layout -----------------------------------------------------------------------------------------------------
    enum Style : uint8_t { plain, grid, macro, matrix, compact }; // grid: the model's bank grid
    struct Cell { uint32_t id; int16_t x, y, w, h; Style style; const char* label; };
    struct Layout { const Cell* cells; int count; };

    static constexpr int headerH = 41, footerY = headerH + 142, footerH = hdui::height - footerY;
    static constexpr int scopeX = 102, scopeY = 2, scopeW = 216, scopeH = 49;
    static constexpr int16_t W = 50, H = 44, row0 = 2, row1 = 48, row2 = 94;
    static constexpr int16_t col (int i) { return (int16_t) (2 + 52 * i); }

    static Layout layout (int p)
    {
        using namespace ParamId;
        static const Cell plaitsCells[] = {
            { model, 2, 2, 98, 49, grid, "MODEL" },
            { frequency, 2, 53, 77, 42, macro, "FREQ" }, { harmonics, 81, 53, 77, 42, macro, "HARM" },
            { timbre, 160, 53, 78, 42, macro, "TIMBRE" }, { morph, 240, 53, 78, 42, macro, "MORPH" },
            { timbreAtt, 2, 97, 43, H, plain, "T ATT" }, { fmAtt, 47, 97, 43, H, plain, "FM ATT" },
            { morphAtt, 92, 97, 43, H, plain, "M ATT" }, { ampMode, 137, 97, 43, H, plain, "AMP" },
            { lpgColor, 182, 97, 43, H, plain, "COLOR" }, { lpgDecay, 227, 97, 43, H, plain, "DECAY" },
            { auxMix, 272, 97, 46, H, plain, "AUX" },
        };
        static const Cell voiceCells[] = {
            { voiceMode, col (0), row0, W, H, plain, "MODE" }, { notes, col (1), row0, W, H, plain, "NOTES" },
            { unison, col (2), row0, W, H, plain, "UNISON" }, { detune, col (3), row0, W, H, plain, "DETUNE" },
            { spread, col (4), row0, W, H, plain, "SPREAD" }, { pan, col (5), row0, W, H, plain, "PAN" },
            { glide, col (0), row1, W, H, plain, "GLIDE" }, { bendRange, col (1), row1, W, H, plain, "BEND" },
            { velCurve, col (2), row1, W, H, plain, "VEL" }, { atCurve, col (3), row1, W, H, plain, "AT" },
            { filterMode, col (0), row2, W, 46, plain, "FILTER" }, { cutoff, col (1), row2, W, 46, plain, "CUTOFF" },
            { resonance, col (2), row2, W, 46, plain, "RESO" },
        };
        static const Cell envCells[] = {
            { env1Attack, col (0), row0, W, H, plain, "ATK 1" }, { env1Decay, col (1), row0, W, H, plain, "DEC 1" },
            { env1Sustain, col (2), row0, W, H, plain, "SUS 1" }, { env1Release, col (3), row0, W, H, plain, "REL 1" },
            { env1Retrig, col (4), row0, W, H, plain, "RTRG 1" }, { volume, col (5), row0, W, H, plain, "VOLUME" },
            { env2Attack, col (0), row1, W, H, plain, "ATK 2" }, { env2Decay, col (1), row1, W, H, plain, "DEC 2" },
            { env2Sustain, col (2), row1, W, H, plain, "SUS 2" }, { env2Release, col (3), row1, W, H, plain, "REL 2" },
            { env2Retrig, col (4), row1, W, H, plain, "RTRG 2" }, { trigRate, col (5), row1, W, H, plain, "TRIG" },
        };
        static const Cell lfoCells[] = {
            { lfo1Shape, col (0), row0, W, H, plain, "SHAPE1" }, { lfo1Rate, col (1), row0, W, H, plain, "RATE 1" },
            { lfo1Phase, col (2), row0, W, H, plain, "PHASE1" }, { lfo1Sync, col (3), row0, W, H, plain, "SYNC 1" },
            { lfo1Div, col (4), row0, W, H, plain, "DIV 1" }, { lfo1Retrig, col (5), row0, W, H, plain, "RTRG 1" },
            { lfo2Shape, col (0), row1, W, H, plain, "SHAPE2" }, { lfo2Rate, col (1), row1, W, H, plain, "RATE 2" },
            { lfo2Phase, col (2), row1, W, H, plain, "PHASE2" }, { lfo2Sync, col (3), row1, W, H, plain, "SYNC 2" },
            { lfo2Div, col (4), row1, W, H, plain, "DIV 2" }, { lfo2Retrig, col (5), row1, W, H, plain, "RTRG 2" },
            { lfo1PerVoice, col (0), row2, W, 46, plain, "VOICE1" }, { lfo2PerVoice, 162, row2, W, 46, plain, "VOICE2" },
        };
        static const Cell cycleCells[] = {
            { cycRise, col (0), row0, W, H, plain, "RISE" }, { cycFall, col (1), row0, W, H, plain, "FALL" },
            { cycShape, col (2), row0, W, H, plain, "SHAPE" }, { cycBipolar, col (3), row0, W, H, plain, "BIPOL" },
            { cycRetrig, col (4), row0, W, H, plain, "C RTRG" },
            { rndMode, col (0), row1, W, H, plain, "RANDOM" }, { rndRate, col (1), row1, W, H, plain, "RATE" },
            { rndSlew, col (2), row1, W, H, plain, "SLEW" }, { rndSync, col (3), row1, W, H, plain, "SYNC" },
            { rndDiv, col (4), row1, W, H, plain, "DIV" }, { rndRetrig, col (5), row1, W, H, plain, "R RTRG" },
        };
        static Cell modCells[routeSources * routeTargets];
        static Cell assignCells[2 * assignSlots + assignSlots * assignSlots];
        static bool made = false;
        if (! made)
        {
            for (int s = 0; s < routeSources; ++s)
                for (int t = 0; t < routeTargets; ++t)
                    modCells[4 * s + t] = { route (s, t), (int16_t) (48 + 68 * t), (int16_t) (14 + 32 * s), 66, 30, matrix, nullptr };
            int n = 0;
            for (int t = 0; t < assignSlots; ++t)
                assignCells[n++] = { assignTarget0 + (uint32_t) t, (int16_t) (66 + 63 * t), 1, 61, 24, compact, nullptr };
            for (int s = 0; s < assignSlots; ++s)
            {
                assignCells[n++] = { assignSource0 + (uint32_t) s, 2, (int16_t) (28 + 28 * s), 62, 26, compact, nullptr };
                for (int t = 0; t < assignSlots; ++t)
                    assignCells[n++] = { assign (s, t), (int16_t) (66 + 63 * t), (int16_t) (28 + 28 * s), 61, 26, matrix, nullptr };
            }
            made = true;
        }
        switch (p)
        {
            case plaitsPage: return { plaitsCells, (int) std::size (plaitsCells) };
            case voicePage: return { voiceCells, (int) std::size (voiceCells) };
            case envPage: return { envCells, (int) std::size (envCells) };
            case lfoPage: return { lfoCells, (int) std::size (lfoCells) };
            case cyclePage: return { cycleCells, (int) std::size (cycleCells) };
            case modPage: return { modCells, (int) std::size (modCells) };
            default: return { assignCells, (int) std::size (assignCells) };
        }
    }

    float live (uint32_t id) const { return values[id].load (std::memory_order_relaxed); }
    float val (uint32_t id) const { return seen[id]; }
    int ival (uint32_t id) const { return (int) seen[id]; }
    void formatted (uint32_t id, char* s, uint32_t n) const { formatParam (paramTable[id], val (id), s, n); }

    static const char* sourceShort (int s)
    {
        static const char* const n[] = { "OFF", "LFO 1", "LFO 2", "ENV 1", "ENV 2", "CYCLE", "RANDOM", "VELO", "AFTER", "WHEEL" };
        return n[std::clamp (s, 0, Source::count - 1)];
    }
    static const char* targetShort (int t)
    {
        static const char* const n[] = { "OFF", "PITCH", "HARM", "TIMBRE", "MORPH", "FM", "DECAY", "COLOR", "CUTOFF", "RESO",
                                         "VOLUME", "PAN", "AUX" };
        return n[std::clamp (t, 0, Target::count - 1)];
    }

    // --- Telemetry the page keeps -------------------------------------------------------------------------------------
    void sampleTelemetry()
    {
        lfoShown[0] = tel.lfo[0].load (std::memory_order_relaxed);
        lfoShown[1] = tel.lfo[1].load (std::memory_order_relaxed);
        cycleShown = tel.cycle.load (std::memory_order_relaxed);
        randomShown = tel.random.load (std::memory_order_relaxed);
        env1Shown = tel.env1.load (std::memory_order_relaxed);
        env2Shown = tel.env2.load (std::memory_order_relaxed);
        cutoffShown = tel.cutoff.load (std::memory_order_relaxed);
        std::memmove (randomTrail, randomTrail + 1, sizeof (randomTrail) - sizeof (randomTrail[0]));
        randomTrail[trailLength - 1] = randomShown;
        for (int i = 0; i < maxVoices; ++i)
            levelShown[i] = tel.levels[i].load (std::memory_order_relaxed), keyShown[i] = tel.keys[i].load (std::memory_order_relaxed);
        // The scope: the newest samples, from a rising zero crossing.
        const uint32_t end = tel.scopeAt.load (std::memory_order_acquire);
        constexpr int span = 1024;
        float x[span];
        for (int i = 0; i < span; ++i)
            x[i] = tel.scope[(end - (uint32_t) span + (uint32_t) i) % Telemetry::scopeSize].load (std::memory_order_relaxed);
        int start = 0;
        for (int i = 1; i < span - scopeSamples; ++i)
            if (x[i - 1] <= 0.0f && x[i] > 0.0f) { start = i; break; }
        float peak = 1e-4f;
        for (int i = 0; i < scopeSamples; ++i)
            scopeTrace[i] = x[start + i], peak = std::max (peak, std::fabs (x[start + i]));
        scopeGain = std::max (peak, scopeGain * 0.9f);
    }

    // --- Header -------------------------------------------------------------------------------------------------------
    void header (const Raster& r, uint32_t sounding, uint32_t gated) const
    {
        using namespace hdui;
        for (int y = 0; y < wordmark::height; ++y)
            for (int x = 0; x < wordmark::width; ++x)
                r.px[y * r.w + x] = wordmark::palette[wordmark::rows[y][x] - ' '];

        static const char* const names[pageCount] = { "PLAITS", "VOICE", "ENV", "LFO", "CYCLE", "MOD", "ASSIGN" };
        // A slashed navigation strip. Its seven cuts double as the page indicator.
        for (int y = 0; y < 15; ++y)
        {
            const int left = 170 + (14 - y) / 2;
            r.box (left, 2 + y, 318 - left, 1, ink::acid);
        }
        r.text (180, 6, names[page], ink::bg);
        for (int i = 0; i < pageCount; ++i)
            for (int j = 0; j < 6; ++j)
                r.box (256 + i * 8 + (5 - j) / 2, 7 + j, i == page ? 6 : 2, 1, ink::bg);

        const int m = std::clamp (ival (ParamId::model), 0, modelCount - 1);
        r.text (172, 25, modelChoices[m], ink::paper);
        for (int i = 0; i < maxVoices; ++i)
        {
            const bool on = (sounding >> i) & 1u, held = (gated >> i) & 1u;
            const int x = 286 + 4 * i;
            r.box (x, 25, 2, 7, held ? ink::acid : (on ? ink::lilac : ink::grid));
        }
        r.box (2, 40, 316, 1, ink::rule);
    }

    // --- Body ---------------------------------------------------------------------------------------------------------
    void body (const Raster& r, uint32_t sounding, uint32_t gated) const
    {
        using namespace hdui;
        const Layout l = layout (page);
        const int f = std::clamp (focus[page], 0, l.count - 1);
        switch (page)
        {
            case plaitsPage: scope (r, scopeX, scopeY); break;
            case voicePage: voices (r, 210, row1, 108, H, sounding, gated), filterCurve (r, 158, row2, 160, 46); break;
            case envPage:
                envCurve (r, 2, row2, 156, 46, 0, env1Shown);
                envCurve (r, 162, row2, 156, 46, 1, env2Shown);
                break;
            case lfoPage: lfoWave (r, 54, row2, 106, 46, 0), lfoWave (r, 214, row2, 104, 46, 1); break;
            case cyclePage: cycleWave (r, 2, row2, 156, 46), randomTrace (r, 162, row2, 156, 46), cycleNow (r, col (5), row0, W, H); break;
            case modPage: modFrame (r); break;
            default: assignFrame (r); break;
        }
        for (int i = 0; i < l.count; ++i)
            cell (r, l.cells[i], i == f, i == f && editing);
    }

    // The pictures that move on the page shown, each redrawn alone (body() draws them in place).
    bool moving (uint32_t* pixels, int stride, uint32_t sounding, uint32_t gated)
    {
        constexpr int top = headerH;
        bool drew = false;
        switch (page)
        {
            case plaitsPage: drew |= picture.region<scopeW, scopeH> (pixels, stride, false, scopeX, top + scopeY, [&] (Raster& r) { scope (r, 0, 0); }); break;
            case voicePage:
                drew |= picture.region<108, H> (pixels, stride, false, 210, top + row1, [&] (Raster& r) { voices (r, 0, 0, 108, H, sounding, gated); });
                drew |= picture.region<160, 46> (pixels, stride, false, 158, top + row2, [&] (Raster& r) { filterCurve (r, 0, 0, 160, 46); });
                break;
            case envPage:
                drew |= picture.region<156, 46> (pixels, stride, false, 2, top + row2, [&] (Raster& r) { envCurve (r, 0, 0, 156, 46, 0, env1Shown); });
                drew |= picture.region<156, 46> (pixels, stride, false, 162, top + row2, [&] (Raster& r) { envCurve (r, 0, 0, 156, 46, 1, env2Shown); });
                break;
            case lfoPage:
                drew |= picture.region<106, 46> (pixels, stride, false, 54, top + row2, [&] (Raster& r) { lfoWave (r, 0, 0, 106, 46, 0); });
                drew |= picture.region<104, 46> (pixels, stride, false, 214, top + row2, [&] (Raster& r) { lfoWave (r, 0, 0, 104, 46, 1); });
                break;
            case cyclePage:
                drew |= picture.region<156, 46> (pixels, stride, false, 2, top + row2, [&] (Raster& r) { cycleWave (r, 0, 0, 156, 46); });
                drew |= picture.region<156, 46> (pixels, stride, false, 162, top + row2, [&] (Raster& r) { randomTrace (r, 0, 0, 156, 46); });
                drew |= picture.region<W, H> (pixels, stride, false, col (5), top + row0, [&] (Raster& r) { cycleNow (r, 0, 0, W, H); });
                break;
            case modPage: drew |= picture.region<44, 126> (pixels, stride, false, 2, top + 14, [&] (Raster& r) { modMeters (r, -2, -14); }); break;
            default:
            {
                // The sources' cells, with their levels.
                const Layout l = layout (page);
                const int f = std::clamp (focus[page], 0, l.count - 1);
                drew |= picture.region<62, 110> (pixels, stride, false, 2, top + 28, [&] (Raster& r) {
                    for (int i = 0; i < l.count; ++i)
                        if (l.cells[i].id >= ParamId::assignSource0 && l.cells[i].id < ParamId::assignTarget0)
                        {
                            Cell c = l.cells[i];
                            c.x = (int16_t) (c.x - 2), c.y = (int16_t) (c.y - 28);
                            cell (r, c, i == f, i == f && editing);
                        }
                });
                break;
            }
        }
        return drew;
    }

    static void plate (const Raster& r, const Cell& c, bool focused, bool edited)
    {
        using namespace hdui;
        const Ink key = edited ? ink::pink : ink::acid;
        r.box (c.x, c.y, c.w, c.h, focused ? ink::selected : ink::card);
        r.box (c.x, c.y, c.w, 1, focused ? key : ink::rule);
        if (focused)
        {
            r.box (c.x, c.y, 2, c.h, key);
            r.box (c.x + c.w - 6, c.y + c.h - 1, 6, 1, key);
            r.box (c.x + c.w - 1, c.y + c.h - 4, 1, 4, key);
        }
        if (edited) r.frame (c.x, c.y, c.w, c.h, key);
    }

    void macroCell (const Raster& r, const Cell& c, bool focused, bool edited) const
    {
        using namespace hdui;
        const Ink color = accentFor (c.id);
        const Ink strip = focused ? (edited ? ink::pink : ink::acid) : ink::grid;
        r.box (c.x + 1, c.y + 1, c.w - 2, 12, strip);
        r.text (c.x + 4, c.y + 3, c.label, focused ? ink::bg : ink::paper, 6);
        const int m = std::clamp (ival (ParamId::model), 0, modelCount - 1);
        const ModelRole& roles = modelRoles[m];
        const char* role = "Pitch";
        if (c.id == ParamId::harmonics) role = roles.harmonics;
        if (c.id == ParamId::timbre) role = roles.timbre;
        if (c.id == ParamId::morph) role = roles.morph;

        const Param& p = paramTable[c.id];
        const float unit = (val (c.id) - p.min) / (p.max - p.min);
        const bool longRole = std::strlen (role) > 8;
        knob (r, c.x + 14, c.y + (longRole ? 22 : 27), longRole ? 8 : 10, unit, p.min < 0, color);
        char value[24];
        formatted (c.id, value, sizeof (value));
        r.right (c.x + c.w - 3, c.y + 17, value, ink::paper, 6);
        if (longRole) valueText (r, c.x + c.w / 2, c.y + 32, c.w - 4, role, color);
        else r.right (c.x + c.w - 3, c.y + 30, role, color, 6);
    }

    // A cell: its label, a knob, a switch or a choice, and its value.
    void cell (const Raster& r, const Cell& c, bool focused, bool edited) const
    {
        using namespace hdui;
        const Param& p = paramTable[c.id];
        if (c.style == matrix) { matrixCell (r, c, focused, edited); return; }
        plate (r, c, focused, edited);
        if (c.style == compact) { compactCell (r, c, focused); return; }
        if (c.style == grid) { modelCell (r, c, focused, edited); return; }
        if (c.style == macro) { macroCell (r, c, focused, edited); return; }
        const int mid = c.x + c.w / 2;
        if (focused) r.box (c.x + 1, c.y + 1, c.w - 2, 11, edited ? ink::pink : ink::acid);
        r.centre (mid, c.y + 3, c.label, focused ? ink::bg : ink::muted, 6);
        char s[24];
        formatted (c.id, s, sizeof (s));
        const Ink accent = accentFor (c.id);
        if (p.unit == Unit::onOff)
        {
            const bool on = val (c.id) >= 0.5f;
            r.box (mid - 13, c.y + 17, 26, 11, on ? accent : ink::field);
            r.frame (mid - 13, c.y + 17, 26, 11, on ? accent : ink::rule);
            r.box (on ? mid + 3 : mid - 11, c.y + 19, 8, 7, on ? ink::bg : ink::parked);
            r.centre (mid, c.y + c.h - 13, s, on ? ink::paper : ink::muted);
            return;
        }
        if (p.kind == Kind::choice)
        {
            // A choice: its picture where it has one, else its name in a box.
            if (! choiceIcon (r, c, accent))
            {
                // Where the choice sits among its options: a segment each, the one chosen lit.
                const int n = (int) (p.max - p.min) + 1, chosen = (int) (val (c.id) - p.min);
                const int room = c.w - 10, gap = n > 8 ? 1 : 2, seg = std::max (2, (room - gap * (n - 1)) / n);
                const int span = n * seg + (n - 1) * gap, x0 = mid - span / 2;
                for (int i = 0; i < n; ++i)
                    r.box (x0 + i * (seg + gap), c.y + 19, seg, 7, i == chosen ? accent : mix (accent, ink::bg, 215));
            }
            valueText (r, mid, c.y + c.h - 13, c.w - 4, s, focused ? ink::paper : ink::soft);
            return;
        }
        knob (r, mid, c.y + 22, 9, (val (c.id) - p.min) / (p.max - p.min), p.min < 0.0f, accent);
        valueText (r, mid, c.y + c.h - 13, c.w - 4, s, focused ? ink::paper : ink::soft);
    }

    static void valueText (const Raster& r, int mid, int y, int room, const char* s, Ink c)
    {
        const int n = (int) std::strlen (s);
        const int advance = n * 7 <= room ? 7 : (n * 6 <= room ? 6 : 5);
        r.centre (mid, y, s, c, advance);
    }

    Ink accentFor (uint32_t id) const
    {
        using namespace hdui;
        using namespace ParamId;
        if (id == harmonics) return ink::acid;
        if (id == timbre || id == timbreAtt) return ink::pink;
        if (id == morph || id == morphAtt) return ink::lilac;
        if (id == ampMode || id == lpgColor || id == lpgDecay) return ink::orange;
        if (id >= env1Attack && id <= env1Retrig) return ink::acid;
        if (id >= env2Attack && id <= env2Retrig) return ink::pink;
        if (id >= lfo1Shape && id <= lfo1Phase) return ink::acid;
        if (id >= lfo2Shape && id <= lfo2Phase) return ink::pink;
        if (id >= cycRise && id <= cycRetrig) return ink::gold;
        if (id >= rndMode && id <= rndRetrig) return ink::lime;
        if (id == cutoff || id == resonance || id == filterMode) return ink::violet;
        return ink::ice;
    }

    // A ring knob: a 270-degree track, the value's arc (from the top when bipolar) and a pointer.
    static void knob (const Raster& r, int cx, int cy, int radius, float unit, bool bipolar, Ink accent)
    {
        using namespace hdui;
        unit = std::clamp (unit, 0.0f, 1.0f);
        constexpr int steps = 40;
        static constexpr float start = 2.35619449f, sweep = 4.71238898f; // from 135 degrees, clockwise 270
        struct Arc
        {
            int8_t x[steps + 1], y[steps + 1];
            explicit Arc (int size)
            {
                for (int i = 0; i <= steps; ++i)
                {
                    const float a = start + sweep * (float) i / steps;
                    x[i] = (int8_t) std::lround (size * std::cos (a));
                    y[i] = (int8_t) std::lround (size * std::sin (a));
                }
            }
        };
        static const Arc arcs[] = { Arc (8), Arc (9), Arc (10) };
        const Arc& arc = arcs[radius - 8];
        // A chrome cap with a curved horizon reflected across its face.
        static constexpr char cap[][14] = {
            "    56654    ", "  568888764  ", " 57788887764 ", " 67888776655 ",
            "4577776655543", "2456655544432", "1123444433221", "2111222222212",
            "3221111111233", " 43322212344 ", " 54443334554 ", "  665555664  ", "    45554    "
        };
        static constexpr Ink metal[] = { ink::bg, 0xff282235, 0xff42374e, 0xff574966, 0xff786786,
                                        0xff9e89ae, 0xffbfb3d0, 0xffe6dfef, ink::paper };
        for (int j = 0; j < 13; ++j)
            for (int i = 0; i < 13; ++i)
                if (cap[j][i] != ' ') r.dot (cx + i - 6, cy + j - 6, metal[cap[j][i] - '0']);
        const float from = bipolar ? 0.5f : 0.0f;
        const Ink shade = mix (accent, ink::bg, 100);
        for (int i = 0; i <= steps; ++i)
        {
            const float t = (float) i / steps;
            const bool lit = (t >= std::min (from, unit) && t <= std::max (from, unit));
            r.dot (cx + arc.x[i], cy + arc.y[i], lit ? accent : ink::rule);
            r.dot (cx + arc.x[i], cy + arc.y[i] + 1, lit ? shade : ink::grid);
        }
        const int step = (int) std::lround (unit * steps);
        const int dx = arc.x[step], dy = arc.y[step];
        r.line (cx, cy + 1, cx + dx * 3 / 4, cy + dy * 3 / 4 + 1, ink::bg);
        r.line (cx, cy, cx + dx * 3 / 4, cy + dy * 3 / 4, ink::paper);
        r.dot (cx + dx, cy + dy, accent);
    }

    // Choices drawn as pictures: LFO shapes, the cycle's shape, the filter's type.
    bool choiceIcon (const Raster& r, const Cell& c, Ink accent) const
    {
        using namespace hdui;
        using namespace ParamId;
        const int x0 = c.x + 8, y0 = c.y + 16, w = c.w - 16, h = 13;
        auto plot = [&] (auto fn) {
            int last = -1;
            for (int x = 0; x < w; ++x)
            {
                const float y = fn ((float) x / (float) (w - 1));
                const int py = y0 + (int) std::lround ((1.0f - y) * 0.5f * (float) (h - 1));
                if (last >= 0) r.line (x0 + x - 1, last, x0 + x, py, accent);
                last = py;
            }
        };
        if (c.id == lfo1Shape || c.id == lfo2Shape)
        {
            const int s = ival (c.id);
            static const float rnd[] = { 0.6f, -0.4f, 0.9f, -0.8f, 0.2f };
            plot ([&] (float t) {
                switch (s)
                {
                    case LfoShape::sine: return std::sin (6.2831853f * t);
                    case LfoShape::triangle: return t < 0.5f ? -1.0f + 4.0f * t : 3.0f - 4.0f * t;
                    case LfoShape::saw: return -1.0f + 2.0f * t;
                    case LfoShape::square: return t < 0.5f ? 1.0f : -1.0f;
                    case LfoShape::random: return rnd[std::min (4, (int) (t * 5.0f))];
                    default:
                    {
                        const float u = t * 4.0f;
                        const int k = std::min (3, (int) u);
                        return rnd[k] + (rnd[k + 1] - rnd[k]) * (u - (float) k);
                    }
                }
            });
            return true;
        }
        if (c.id == cycShape)
        {
            const int s = ival (c.id);
            plot ([&] (float t) {
                const float p = t < 0.5f ? t * 2.0f : (t - 0.5f) * 2.0f;
                const bool fall = t >= 0.5f;
                float u = fall ? 1.0f - p : p;
                if (s == 1) u = fall ? (1.0f - p) * (1.0f - p) : p * p;
                if (s == 2) u = fall ? 1.0f - p * p : 1.0f - (1.0f - p) * (1.0f - p);
                return 2.0f * u - 1.0f;
            });
            return true;
        }
        if (c.id == filterMode)
        {
            const int s = ival (c.id);
            plot ([&] (float t) {
                const float w = std::pow (10.0f, (t - 0.5f) * 2.4f);
                const float d = std::sqrt ((1.0f - w * w) * (1.0f - w * w) + w * w * 2.0f);
                const float g = s == 0 ? 1.0f / d : (s == 1 ? w * 1.414f / d : w * w / d);
                return std::clamp (2.0f * g - 1.0f, -1.0f, 1.0f);
            });
            return true;
        }
        return false;
    }

    // Model index and bank lamps share a printed ticket.
    void modelCell (const Raster& r, const Cell& c, bool focused, bool edited) const
    {
        using namespace hdui;
        const int m = std::clamp (ival (ParamId::model), 0, modelCount - 1);
        const Ink stock = focused ? (edited ? ink::pink : ink::acid) : ink::lilac;
        r.box (c.x, c.y, c.w, c.h, stock);
        r.text (c.x + 5, c.y + 4, c.label, ink::bg, 6);
        r.right (c.x + c.w - 4, c.y + 4, "/24", ink::bg, 6);
        r.dotted (c.x + 4, c.y + 16, c.x + c.w - 4, c.y + 16, ink::bg, 3);
        char number[8];
        std::snprintf (number, sizeof (number), "%02d", m + 1);
        r.large (c.x + 4, c.y + 23, number, ink::bg, ink::bg, 14);
        for (int i = 0; i < modelCount; ++i)
        {
            const int x = c.x + 40 + i % 8 * 7, y = c.y + 23 + i / 8 * 7;
            if (i == m) r.box (x - 1, y - 1, 6, 5, ink::bg);
            else if (i / 8 == m / 8) r.frame (x, y, 4, 3, ink::bg);
            else r.box (x, y + 1, 4, 1, mix (stock, ink::bg, 158));
        }
        if (focused) r.frame (c.x + 1, c.y + 1, c.w - 2, c.h - 2, ink::bg);
    }

    // An assign source or target: its short name, the slot's number.
    void compactCell (const Raster& r, const Cell& c, bool focused) const
    {
        using namespace hdui;
        const bool isSource = c.id >= ParamId::assignSource0 && c.id < ParamId::assignTarget0;
        const int slot = (int) (c.id - (isSource ? ParamId::assignSource0 : ParamId::assignTarget0));
        const int v = ival (c.id);
        const char* name = isSource ? sourceShort (v) : targetShort (v);
        char tag[4] = { isSource ? 'S' : 'D', char ('1' + slot), 0, 0 };
        r.text (c.x + 4, c.y + 2, tag, ink::muted, 6);
        const Ink accent = v == 0 ? ink::parked : (isSource ? ink::acid : ink::gold);
        r.right (c.x + c.w - 3, c.y + 2, name, focused ? ink::paper : accent, 6);
        // The source's level now, under it.
        if (isSource)
        {
            const float level = sourceLevel (v);
            const int w = c.w - 8, mid = c.x + 4 + w / 2;
            r.box (c.x + 4, c.y + c.h - 6, w, 2, ink::grid);
            const int at = mid + (int) std::lround (std::clamp (level, -1.0f, 1.0f) * (float) (w / 2));
            r.box (std::min (mid, at), c.y + c.h - 6, std::abs (at - mid) + 1, 2, accent);
        }
        else
            r.box (c.x + 4, c.y + c.h - 5, c.w - 8, 1, accent);
    }

    float sourceLevel (int s) const
    {
        switch (s)
        {
            case Source::lfo1: return lfoShown[0];
            case Source::lfo2: return lfoShown[1];
            case Source::env1: return env1Shown;
            case Source::env2: return env2Shown;
            case Source::cycle: return cycleShown;
            case Source::random: return randomShown;
            case Source::aftertouch: return tel.pressure.load (std::memory_order_relaxed);
            case Source::wheel: return tel.wheel.load (std::memory_order_relaxed);
            default: return 0.0f;
        }
    }

    // A route's amount: a bar from the middle, its value; dim when switched off.
    void matrixCell (const Raster& r, const Cell& c, bool focused, bool edited) const
    {
        using namespace hdui;
        const bool on = val (c.id + 1) >= 0.5f;
        const float a = val (c.id);
        plate (r, c, focused, edited);
        const int w = c.w - 10, left = c.x + 5, mid = left + w / 2, y = c.y + c.h - 9;
        r.box (left, y, w, 3, ink::grid);
        r.box (mid, y - 2, 1, 7, ink::rule);
        const Ink fill = ! on ? ink::parked : (a >= 0.0f ? ink::acid : ink::pink);
        const int at = mid + (int) std::lround (a * (float) (w / 2));
        if (a != 0.0f) r.box (std::min (mid, at), y, std::abs (at - mid) + 1, 3, fill);
        char s[16];
        if (! on) std::snprintf (s, sizeof (s), "OFF");
        else formatted (c.id, s, sizeof (s));
        r.centre (c.x + c.w / 2, c.y + 3, s, ! on ? ink::parked : (focused ? ink::paper : (a == 0.0f ? ink::muted : ink::soft)));
    }

    void modFrame (const Raster& r) const
    {
        using namespace hdui;
        for (int t = 0; t < routeTargets; ++t)
        {
            static const char* const n[] = { "PITCH", "HARM", "TIMBRE", "MORPH" };
            static const Ink inks[] = { ink::ice, ink::acid, ink::pink, ink::lilac };
            r.centre (48 + 68 * t + 33, 1, n[t], inks[t]);
        }
        modMeters (r, 0, 0);
    }

    // MOD: the four sources, each with its level now (drawn at an offset when redrawn alone).
    void modMeters (const Raster& r, int ox, int oy) const
    {
        using namespace hdui;
        const float levels[routeSources] = { lfoShown[0], env2Shown, cycleShown, randomShown };
        for (int s = 0; s < routeSources; ++s)
        {
            const int y = oy + 14 + 32 * s, x = ox + 2;
            r.box (x, y, 44, 30, ink::field);
            static const char* const n[] = { "LFO 1", "ENV 2", "CYCLE", "RANDOM" };
            r.text (x + 3, y + 4, n[s], ink::soft);
            const int mid = x + 22;
            const int at = mid + (int) std::lround (std::clamp (levels[s], -1.0f, 1.0f) * 18.0f);
            r.box (x + 4, y + 21, 36, 2, ink::grid);
            r.box (std::min (mid, at), y + 21, std::abs (at - mid) + 1, 2, ink::acid);
        }
    }

    void assignFrame (const Raster& r) const
    {
        using namespace hdui;
        r.line (3, 2, 62, 24, ink::rule);
        r.text (5, 13, "SRC", ink::muted, 6);
        r.text (39, 3, "DST", ink::muted, 6);
    }

    // --- Pictures -----------------------------------------------------------------------------------------------------
    void panel (const Raster& r, int x, int y, int w, int h, const char* title) const
    {
        using namespace hdui;
        r.box (x, y, w, h, ink::field);
        r.frame (x, y, w, h, ink::grid);
        r.box (x + 1, y, w / 4, 1, ink::rule);
        for (int gx = 12; gx < w - 3; gx += 14)
            for (int gy = 17; gy < h - 3; gy += 8)
                r.dot (x + gx, y + gy, ink::grid);
        if (title != nullptr) r.text (x + 4, y + 3, title, ink::muted);
    }

    // Screen-printed checker floor behind the measured output. Draw the fixed perspective once.
    static const std::array<Ink, scopeW * scopeH>& scopeGlass()
    {
        using namespace hdui;
        static const auto glass = [] {
            std::array<Ink, scopeW * scopeH> pixels {};
            Raster r { pixels.data(), scopeW, scopeH };
            r.box (0, 0, r.w, r.h, ink::field);
            for (int y = 0; y < r.h; ++y)
                for (int x = 0; x < r.w; ++x)
                {
                    const float u = (float) x / (r.w - 1);
                    const float depth = (float) y - (29.0f - 16.0f * u + 5.0f * std::sin (5.8f * u));
                    if (depth <= 0.0f) continue;
                    const float z = 12.0f / (depth + 3.5f);
                    const int lane = (int) std::floor ((u - 0.32f) * z * 32.0f + 0.9f * std::sin (7.2f * u));
                    const int row = (int) std::floor (3.9f * z);
                    if ((lane + row) % 2 == 0)
                        r.dot (x, y, mix (ink::field, 0xff8d58bf, (int) std::min (139.0f, depth * 5.0f)));
                }
            // An orbit around the scope's activity lamp.
            constexpr int cx = scopeW - 27, cy = 10;
            for (int i = 0; i < 120; ++i)
            {
                const float angle = i * 6.2831853f / 120.0f;
                const float u = std::cos (angle), v = std::sin (angle);
                const int px = cx + (int) std::lround (21.0f * u);
                const int py = cy + (int) std::lround (3.0f * v - 6.0f * u);
                r.dot (px, py, v > 0.0f ? ink::lilac : ink::rule);
            }
            r.disc (cx, cy, 6, ink::rule);
            r.disc (cx - 1, cy - 1, 5, ink::lilac);
            r.disc (cx, cy + 1, 4, ink::field);
            r.line (cx - 15, cy + 5, cx + 15, cy - 4, ink::soft);
            r.box (cx + 3, cy - 5, 5, 1, ink::paper);
            r.box (cx + 5, cy - 7, 1, 5, ink::paper);
            r.frame (0, 0, r.w, r.h, ink::rule);
            r.text (5, 4, "OUTPUT", ink::soft, 6);
            return pixels;
        }();
        return glass;
    }

    void scope (const Raster& r, int x, int y) const
    {
        using namespace hdui;
        const auto& glass = scopeGlass();
        for (int j = 0; j < scopeH; ++j)
            std::copy_n (glass.data() + j * scopeW, scopeW, r.px + (y + j) * r.w + x);
        const int top = y + 13, high = scopeH - 16, mid = top + high / 2;
        r.dotted (x + 3, mid, x + scopeW - 4, mid, ink::muted, 4);
        if (! animated) return;
        r.box (x + scopeW - 29, y + 9, 4, 2, ink::acid);
        const float scale = (float) (high / 2 - 1) / std::max (scopeGain, 0.02f);
        const int cols = scopeW - 6;
        int last = mid;
        for (int i = 0; i < cols; ++i)
        {
            const int k = i * scopeSamples / cols;
            const int py = std::clamp (mid - (int) std::lround (scopeTrace[k] * scale), top, top + high - 1);
            const int xx = x + 3 + i;
            if (i)
            {
                r.line (xx - 1, last + 1, xx, py + 1, ink::deep);
                r.line (xx - 1, last, xx, py, ink::acid);
            }
            last = py;
        }
    }

    // VOICE: the eight voices, each a bar of its level (bright while its key is held), and the keys held.
    void voices (const Raster& r, int x, int y, int w, int h, uint32_t sounding, uint32_t gated) const
    {
        using namespace hdui;
        panel (r, x, y, w, h, "VOICES");
        static const char* const names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        char words[maxVoices][16] {};
        int count = 0;
        for (int i = 0; i < maxVoices; ++i)
        {
            const bool on = (sounding >> i) & 1u, g = (gated >> i) & 1u;
            const int bx = x + 5 + i * 6, by = y + 14, bh = h - 17;
            r.box (bx, by, 4, bh, ink::grid);
            if (on)
            {
                const float level = std::sqrt (std::clamp (levelShown[i] / 0.4f, 0.0f, 1.0f));
                const int fill = std::max (1, (int) std::lround (level * (float) bh));
                r.box (bx, by + bh - fill, 4, fill, g ? ink::acid : ink::deep);
            }
            const int key = keyShown[i];
            if (! g || key < 0) continue;
            bool dup = false;
            for (int j = 0; j < i; ++j) dup |= ((gated >> j) & 1u) && keyShown[j] == key;
            if (! dup && count < maxVoices) std::snprintf (words[count++], 16, "%s%d", names[std::clamp (key, 0, 127) % 12], std::clamp (key, 0, 127) / 12 - 2);
        }
        // The keys held, two a line beside the bars.
        const int tx = x + 56;
        for (int k = 0; k < count && k < 6; ++k)
            r.text (tx + (k % 2) * 25, y + 14 + (k / 2) * 10, words[k], ink::soft, 6);
        if (count == 0) r.text (tx, y + 14, "-", ink::muted);
    }

    // VOICE: the filter's response, 20 Hz to 20 kHz, -30 to +18 dB; where a modulated cutoff is now.
    void filterCurve (const Raster& r, int x, int y, int w, int h) const
    {
        using namespace hdui;
        panel (r, x, y, w, h, "FILTER");
        const int mode = ival (ParamId::filterMode);
        const float fc = cutoffHz (val (ParamId::cutoff)), q = resonanceQ (val (ParamId::resonance));
        const int top = y + 13, high = h - 15;
        auto dbToY = [&] (float db) { return top + (int) std::lround ((18.0f - std::clamp (db, -30.0f, 18.0f)) / 48.0f * (float) (high - 1)); };
        r.box (x + 2, dbToY (0.0f), w - 4, 1, ink::grid);
        int last = -1;
        for (int i = 0; i < w - 4; ++i)
        {
            const float hz = 20.0f * std::pow (1000.0f, (float) i / (float) (w - 5));
            const float u = hz / fc;
            const float d = std::sqrt ((1.0f - u * u) * (1.0f - u * u) + (u / q) * (u / q));
            const float g = mode == 0 ? 1.0f / d : (mode == 1 ? (u / q) / d : u * u / d);
            const int py = dbToY (20.0f * std::log10 (std::max (g, 1e-4f)));
            if (last >= 0) r.line (x + 2 + i - 1, last, x + 2 + i, py, ink::violet);
            last = py;
        }
        if (animated)
        {
            const int cx = x + 2 + (int) std::lround (std::clamp (cutoffShown, 0.0f, 1.0f) * (float) (w - 5));
            r.box (cx, top, 1, high, mix (ink::violet, ink::paper, 128));
        }
    }

    // ENV: the ADSR's shape (times on the knobs' own curve) and the newest voice's level now.
    void envCurve (const Raster& r, int x, int y, int w, int h, int which, float now) const
    {
        using namespace hdui;
        using namespace ParamId;
        const uint32_t o = which == 0 ? env1Attack : env2Attack;
        panel (r, x, y, w, h, which == 0 ? "ENV 1" : "ENV 2");
        const Ink c = which == 0 ? ink::acid : ink::pink;
        const float a = val (o), d = val (o + 1), s = val (o + 2), rel = val (o + 3);
        const int top = y + 13, high = h - 16, base = top + high;
        const float room = (float) (w - 30);
        const float total = std::max (a + d + rel, 0.05f);
        const float scale = std::min (room / total, room / 1.2f);
        const int wa = (int) std::lround (a * scale), wd = (int) std::lround (d * scale), wr = (int) std::lround (rel * scale), ws = 22;
        auto yOf = [&] (float level) { return base - (int) std::lround (std::clamp (level, 0.0f, 1.0f) * (float) high); };
        int px = x + 4, py = base;
        auto to = [&] (int nx, int ny) { r.line (px, py, nx, ny, c); px = nx, py = ny; };
        for (int i = 1; i <= std::max (wa, 1); ++i)
        {
            const float t = (float) i / (float) std::max (wa, 1);
            to (x + 4 + i, yOf (std::min (1.0f, 1.5f * (1.0f - std::pow (1.0f / 3.0f, t)))));
        }
        const int dx = px;
        for (int i = 1; i <= std::max (wd, 1); ++i)
        {
            const float t = (float) i / (float) std::max (wd, 1);
            to (dx + i, yOf (s + (1.0f - s) * std::exp (-3.0f * t)));
        }
        to (px + ws, yOf (s));
        const int rx = px;
        for (int i = 1; i <= std::max (wr, 1); ++i)
        {
            const float t = (float) i / (float) std::max (wr, 1);
            to (rx + i, yOf (s * std::exp (-3.0f * t)));
        }
        if (animated && now > 0.0f)
        {
            const int ny = yOf (now);
            r.box (x + 2, ny, w - 4, 1, mix (c, ink::bg, 150));
            r.disc (x + w - 6, ny, 2, ink::paper);
        }
    }

    // LFO: one cycle of the shape at its phase, and where the LFO is now.
    void lfoWave (const Raster& r, int x, int y, int w, int h, int which) const
    {
        using namespace hdui;
        using namespace ParamId;
        const uint32_t o = which == 0 ? lfo1Shape : lfo2Shape;
        panel (r, x, y, w, h, which == 0 ? "LFO 1" : "LFO 2");
        const Ink c = which == 0 ? ink::acid : ink::pink;
        char rate[24];
        if (val (o + 2) >= 0.5f) std::snprintf (rate, sizeof (rate), "%s", divChoices[std::clamp (ival (o + 3), 0, 10)]);
        else formatted (o + 1, rate, sizeof (rate));
        r.right (x + w - 4, y + 2, rate, ink::soft, 6);
        LfoState shape;
        static const float rnd[] = { 0.55f, -0.35f, 0.85f, -0.75f, 0.15f };
        const int s = ival (o), top = y + 14, high = h - 17, mid = top + high / 2;
        r.box (x + 2, mid, w - 4, 1, ink::grid);
        int last = -1;
        for (int i = 0; i < w - 6; ++i)
        {
            const float t = (float) i / (float) (w - 7);
            float v;
            if (s == LfoShape::random) v = rnd[std::min (4, (int) (t * 5.0f))];
            else if (s == LfoShape::smooth)
            {
                const float u = t * 4.0f;
                const int k = std::min (3, (int) u);
                v = rnd[k] + (rnd[k + 1] - rnd[k]) * (u - (float) k);
            }
            else
            {
                shape.phase = t;
                v = shape.value (s, val (o + 6));
            }
            const int py = mid - (int) std::lround (v * (float) (high / 2));
            if (last >= 0) r.line (x + 3 + i - 1, last, x + 3 + i, py, c);
            last = py;
        }
        if (animated)
        {
            const int py = mid - (int) std::lround (std::clamp (lfoShown[which], -1.0f, 1.0f) * (float) (high / 2));
            r.box (x + 2, py, w - 4, 1, mix (c, ink::bg, 160));
            r.disc (x + w - 6, py, 2, ink::paper);
        }
    }

    // CYCLE: one rise and fall at their times and shape, and where it is now.
    void cycleWave (const Raster& r, int x, int y, int w, int h) const
    {
        using namespace hdui;
        using namespace ParamId;
        panel (r, x, y, w, h, "CYCLE");
        const float rise = cycleMs (val (cycRise)), fall = cycleMs (val (cycFall));
        char s[32];
        std::snprintf (s, sizeof (s), "%.2f Hz", 1000.0f / (rise + fall));
        r.right (x + w - 4, y + 2, s, ink::soft, 6);
        const bool bipolar = val (cycBipolar) >= 0.5f;
        const int shape = ival (cycShape), top = y + 14, high = h - 17;
        const float split = rise / (rise + fall);
        int last = -1;
        for (int i = 0; i < w - 6; ++i)
        {
            const float t = (float) i / (float) (w - 7);
            const bool falling = t >= split;
            const float p = falling ? (t - split) / std::max (1e-4f, 1.0f - split) : t / std::max (1e-4f, split);
            float u = falling ? 1.0f - p : p;
            if (shape == 1) u = falling ? (1.0f - p) * (1.0f - p) : p * p;
            if (shape == 2) u = falling ? 1.0f - p * p : 1.0f - (1.0f - p) * (1.0f - p);
            const int py = top + high - 1 - (int) std::lround (u * (float) (high - 1));
            if (last >= 0) r.line (x + 3 + i - 1, last, x + 3 + i, py, ink::gold);
            last = py;
        }
        if (bipolar) r.box (x + 2, top + high / 2, w - 4, 1, ink::grid);
        if (animated)
        {
            const float u = bipolar ? (cycleShown + 1.0f) * 0.5f : cycleShown;
            const int py = top + high - 1 - (int) std::lround (std::clamp (u, 0.0f, 1.0f) * (float) (high - 1));
            r.box (x + 2, py, w - 4, 1, mix (ink::gold, ink::bg, 160));
            r.disc (x + w - 6, py, 2, ink::paper);
        }
    }

    // CYCLE: the cycle's value now, large, in the row's spare place.
    void cycleNow (const Raster& r, int x, int y, int w, int h) const
    {
        using namespace hdui;
        panel (r, x, y, w, h, nullptr);
        r.centre (x + w / 2, y + 2, "NOW", ink::muted);
        const float u = std::clamp (cycleShown, -1.0f, 1.0f);
        const int mid = x + w / 2, bw = w - 10;
        r.box (x + 5, y + 22, bw, 4, ink::grid);
        const bool bipolar = val (ParamId::cycBipolar) >= 0.5f;
        const int origin = bipolar ? mid : x + 5;
        const int at = bipolar ? mid + (int) std::lround (u * (float) (bw / 2)) : x + 5 + (int) std::lround (u * (float) bw);
        r.box (std::min (origin, at), y + 22, std::abs (at - origin) + 1, 4, ink::gold);
        char s[16];
        std::snprintf (s, sizeof (s), "%+.2f", animated ? u : 0.0f);
        r.centre (mid, y + h - 13, s, ink::soft, 6);
    }

    // CYCLE: the random source's last few seconds.
    void randomTrace (const Raster& r, int x, int y, int w, int h) const
    {
        using namespace hdui;
        panel (r, x, y, w, h, "RANDOM");
        char s[24];
        if (val (ParamId::rndSync) >= 0.5f) std::snprintf (s, sizeof (s), "%s", divChoices[std::clamp (ival (ParamId::rndDiv), 0, 10)]);
        else formatted (ParamId::rndRate, s, sizeof (s));
        r.right (x + w - 4, y + 2, s, ink::soft, 6);
        const int top = y + 14, high = h - 17, mid = top + high / 2;
        r.box (x + 2, mid, w - 4, 1, ink::grid);
        constexpr int n = trailLength;
        int last = -1;
        for (int i = 0; i < n && i < w - 6; ++i)
        {
            const float v = randomTrail[n - std::min (n, w - 6) + i];
            const int py = mid - (int) std::lround (std::clamp (v, -1.0f, 1.0f) * (float) (high / 2));
            if (last >= 0) r.line (x + 3 + i - 1, last, x + 3 + i, py, ink::lime);
            last = py;
        }
    }

    // --- The knobs' cards and the legend ------------------------------------------------------------------------------
    void cards (const Raster& r, const Host& h) const
    {
        using namespace hdui;
        const Ink track = h.accent ? mix (h.accent | 0xff000000u, ink::paper, 52) : ink::acid;
        const Layout l = layout (page);
        for (int k = 0; k < 3; ++k)
        {
            const int x = 4 + 105 * k;
            const int c = focus[page] + k;
            r.box (x, 1, 102, 32, ink::field);
            const Ink badge = k == 0 ? ink::acid : ink::lilac;
            r.box (x, 1, 102, 1, ink::rule);
            r.box (x, 2, 16, 11, badge);
            const char tag[] = { 'K', char ('1' + k), 0 };
            r.text (x + 2, 4, tag, ink::bg, 6);
            if (c >= l.count) continue;
            const uint32_t id = l.cells[c].id;
            const auto& p = paramTable[id];
            char title[32];
            cardTitle (l.cells[c], title, sizeof (title));
            const int titleRoom = 82;
            const int titleLength = (int) std::strlen (title);
            r.text (x + 19, 4, title, ink::soft, titleLength * 7 <= titleRoom ? 7 : (titleLength * 6 <= titleRoom ? 6 : 5));
            char s[32];
            formatted (id, s, sizeof (s));
            r.text (x + 3, 17, s, l.cells[c].style == matrix && val (id + 1) < 0.5f ? ink::parked : ink::paper,
                    (int) std::strlen (s) * 7 <= 98 ? 7 : 6);
            const int start = x + 3, w = 96, rail = 30;
            const float unit = (val (id) - p.min) / (p.max - p.min);
            const int at = start + (int) std::lround (unit * (float) (w - 1));
            r.box (start, rail, w, 2, ink::rule);
            const int origin = p.min < 0 ? start + (w - 1) / 2 : start;
            r.box (std::min (at, origin), rail, std::abs (at - origin) + 1, 2, track);
            r.box (at, rail - 2, 2, 6, track);
            if (h.pot[k] >= 0 && ! h.engaged[k])
            {
                const int pos = start + std::clamp ((int) h.pot[k], 0, 127) * (w - 1) / 127;
                r.box (pos - 2, rail - 3, 5, 7, ink::card);
                r.frame (pos - 2, rail - 3, 5, 7, ink::paper);
            }
        }
    }

    // What a card calls its control: the matrices name both ends; the macros say what they do in this model.
    void cardTitle (const Cell& c, char* s, size_t n) const
    {
        using namespace ParamId;
        const int m = std::clamp (ival (model), 0, modelCount - 1);
        if (c.id >= route0 && c.id < assignSource0)
        {
            const int k = (int) (c.id - route0) / 2;
            static const char* const from[] = { "LFO1", "ENV2", "CYC", "RND" };
            static const char* const to[] = { "PITCH", "HARM", "TIMB", "MORPH" };
            std::snprintf (s, n, "%s>%s", from[k / 4], to[k % 4]);
        }
        else if (c.id >= assign0)
        {
            const int k = (int) (c.id - assign0) / 2;
            std::snprintf (s, n, "%s>%s", sourceShort (ival (assignSource0 + (uint32_t) (k / 4))), targetShort (ival (assignTarget0 + (uint32_t) (k % 4))));
        }
        else if (c.id == harmonics) std::snprintf (s, n, "%s", modelRoles[m].harmonics);
        else if (c.id == timbre) std::snprintf (s, n, "%s", modelRoles[m].timbre);
        else if (c.id == morph) std::snprintf (s, n, "%s", modelRoles[m].morph);
        else if (c.id == auxMix) std::snprintf (s, n, "AUX %s", modelRoles[m].aux);
        else std::snprintf (s, n, "%s", c.label != nullptr ? c.label : paramTable[c.id].label);
    }

    void legend (const Raster& r) const
    {
        using namespace hdui;
        const bool matrixPage = page == modPage || page == assignPage;
        const char* top[3] = { "WHEEL", "PUSH", "MINUS / PLUS" };
        const char* plain[3] = { "Move", "Edit / flip", "Page" };
        const char* shifted[3] = { "", "", "" };
        if (editing) plain[0] = "Value", shifted[0] = "+SH Fine", plain[1] = "Done", plain[2] = "Coarse value";
        if (matrixPage) shifted[1] = "+SH Route on/off";
        for (int k = 0; k < 3; ++k)
        {
            const int x = 4 + 105 * k;
            r.box (x, 1, 102, 33, ink::field);
            r.box (x, 1, 102, 10, ink::lilac);
            r.text (x + 3, 2, top[k], ink::bg, 6);
            r.text (x + 3, 13, plain[k], ink::paper, 6);
            r.text (x + 3, 24, shifted[k], ink::muted, 6);
        }
    }

    const std::atomic<float>* values;
    const Telemetry& tel;
    hdui::Picture picture;
    Host lastHost {};
    float seen[paramCount] {};
    int page = plaitsPage, focus[pageCount] {};
    int shownPage = -1;
    uint32_t shownSounding = 0, shownGated = 0;
    bool editing = false, first = true, touched = false, animated = false;
    uint64_t frameAt = 0;
    static constexpr int scopeSamples = 512;
    float scopeTrace[scopeSamples] {}, scopeGain = 0.05f;
    float lfoShown[2] {}, cycleShown = 0.0f, randomShown = 0.0f, env1Shown = 0.0f, env2Shown = 0.0f, cutoffShown = 1.0f;
    static constexpr int trailLength = 150;
    float randomTrail[trailLength] {};
    float levelShown[maxVoices] {};
    int keyShown[maxVoices] {};
};
} // namespace nexus
