#include "screen.h"

namespace reverie
{
bool Screen::paint (const Host& h, uint32_t* pixels, int stride, bool whole)
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
    const uint32_t held = tel.padsHeld.load (std::memory_order_relaxed);
    const uint32_t strikes = tel.strikes.load (std::memory_order_relaxed);
    const float tail = tel.reverb.load (std::memory_order_relaxed);
    animated = sounding != 0 || tail > 1e-4f;
    const bool frame = animated && (h.nowMs - frameAt >= 40 || h.nowMs < frameAt);
    const bool notesMoved = sounding != shownSounding || held != shownHeld || strikes != shownStrikes;
    bool pots = false;
    for (int k = 0; k < 3; ++k)
        pots |= h.pot[k] != lastHost.pot[k] || h.engaged[k] != lastHost.engaged[k];
    const bool chrome = whole || page != shownPage || touched || h.accent != lastHost.accent;
    if (! frame && ! pots && ! chrome && ! params && ! notesMoved && h.shift == lastHost.shift && h.accent == lastHost.accent
        && ! (wasAnimated && ! animated))
        return false;
    if (frame || notesMoved) frameAt = h.nowMs;
    const bool stopped = wasAnimated && ! animated;
    wasAnimated = animated;
    accent = h.accent ? (h.accent | 0xff000000u) : hdui::ink::foam;
    bool drew = false;
    if (chrome || params)
        drew |= picture.region<320, headH> (pixels, stride, whole, 0, 0, [&] (Raster& r) { header (r); });
    if (chrome || params || notesMoved)
        drew |= picture.region<320, bodyH> (pixels, stride, whole, 0, headH, [&] (Raster& r) { body (r, sounding, held); });
    else if (frame || stopped)
        drew |= moving (pixels, stride, sounding);
    if (chrome || params || pots || h.shift != lastHost.shift)
        drew |= picture.region<320, dockH> (pixels, stride, whole, 0, headH + bodyH, [&] (Raster& r) {
            dock (r, h);
        });
    shownPage = page;
    shownSounding = sounding, shownHeld = held, shownStrikes = strikes;
    lastHost = h;
    touched = false;
    return drew;
}

void Screen::knobs (uint32_t ids[3]) const
{
    const Layout l = layout (page);
    for (int k = 0; k < 3; ++k)
    {
        const int c = focus[page] + k;
        ids[k] = c < l.count ? l.cells[c].id : hdui::noParam;
    }
}

bool Screen::input (hdui::Key key, int amount, bool shift, hdui::Edits& out)
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

Screen::Layout Screen::layout (int p)
{
    using namespace ParamId;
    constexpr int16_t L = 175, R = 246, W = 70, H = 16;
    static const Cell chordCells[] = {
        { progression, L, 3, 141, H, "Prog" }, { voicing, L, 20, 141, H, "Voicing" },
        { key, L, 37, W, H, "Key" }, { octave, R, 37, W, H, "Oct" },
        { strum, L, 54, W, H, "Strum" }, { humanize, R, 54, W, H, "Human" },
        { bass, L, 71, W, H, "Bass" }, { velocity, R, 71, W, H, "Vel" },
        { latch, L, 88, W, H, "Latch" }, { glide, R, 88, W, H, "Glide" },
    };
    constexpr int16_t ML = 129, MR = 223, MW = 93, MH = 13;
    auto mrow = [] (int r) { return (int16_t) (3 + MH * r); };
    static const Cell motionCells[] = {
        { motionX, ML, mrow (0), MW, MH, "X" }, { motionY, MR, mrow (0), MW, MH, "Y" },
        { motionDepth, ML, mrow (1), MW, MH, "Depth" }, { motionShape, MR, mrow (1), MW, MH, "Path" },
        { motionRate, ML, mrow (2), MW, MH, "Rate" }, { motionSpread, MR, mrow (2), MW, MH, "Spread" },
        { motionSync, ML, mrow (3), MW, MH, "Sync" }, { motionDiv, MR, mrow (3), MW, MH, "Division" },
        { motionRetrig, ML, mrow (4), MW, MH, "Retrig" },
        { xDest1, ML, mrow (5), MW, MH, "X to 1" }, { xAmt1, MR, mrow (5), MW, MH, "Amount" },
        { xDest2, ML, mrow (6), MW, MH, "X to 2" }, { xAmt2, MR, mrow (6), MW, MH, "Amount" },
        { yDest1, ML, mrow (7), MW, MH, "Y to 1" }, { yAmt1, MR, mrow (7), MW, MH, "Amount" },
        { yDest2, ML, mrow (8), MW, MH, "Y to 2" }, { yAmt2, MR, mrow (8), MW, MH, "Amount" },
    };
    static const auto oscCells = [] {
        std::array<Cell, 20> cells {};
        const char* const labels[] = { "Wave", "Pos", "Oct", "Semi", "Fine", "Unison", "Detune", "Level" };
        const int order[] = { 0, 1, 7, 2, 3, 4, 5, 6 };
        int n = 0;
        for (int o = 0; o < 2; ++o)
            for (int i = 0; i < 8; ++i)
            {
                const int f = order[i], col = i ? (i - 1) % 2 : 0, row = i ? 1 + (i - 1) / 2 : 0;
                cells[n++] = { ParamId::osc (o, (uint32_t) f), (int16_t) (4 + 158 * o + 77 * col),
                               (int16_t) (35 + 14 * row), (int16_t) (i ? 77 : 154), 14, labels[f] };
            }
        cells[n++] = { width, 4, 107, 77, 15, "Width" };
        cells[n++] = { air, 81, 107, 77, 15, "Air" };
        cells[n++] = { airTone, 162, 107, 77, 15, "Air tone" };
        cells[n++] = { drift, 239, 107, 77, 15, "Drift" };
        return cells;
    }();
    constexpr int16_t FL = 157, FR = 237, FW = 79, FH = 16;
    static const Cell filterCells[] = {
        { filterType, FL, 13, FW, FH, "Type" }, { cutoff, FR, 13, FW, FH, "Cutoff" },
        { resonance, FL, 29, FW, FH, "Reso" }, { filterEnv, FR, 29, FW, FH, "Env" },
        { keyTrack, FL, 45, FW, FH, "Key trk" }, { velCutoff, FR, 45, FW, FH, "Vel" },
        { fltAttack, FL, 78, FW, FH, "Attack" }, { fltDecay, FR, 78, FW, FH, "Decay" },
        { fltSustain, FL, 94, FW, FH, "Sustain" }, { fltRelease, FR, 94, FW, FH, "Release" },
    };
    static const Cell ampCells[] = {
        { ampAttack, FL, 14, FW, FH, "Attack" }, { ampDecay, FR, 14, FW, FH, "Decay" },
        { ampSustain, FL, 30, FW, FH, "Sustain" }, { ampRelease, FR, 30, FW, FH, "Release" },
        { lfoShape, FL, 74, FW, FH, "Shape" }, { lfoRate, FR, 74, FW, FH, "Rate" },
        { lfoDelay, FL, 90, FW, FH, "Delay" }, { lfoPitch, FR, 90, FW, FH, "Pitch" },
        { lfoCutoff, FL, 106, FW, FH, "Cutoff" }, { lfoWave, FR, 106, FW, FH, "Wave" },
    };
    static const Cell fxCells[] = {
        { ensMix, 112, 15, 68, 17, "Mix" }, { ensDepth, 180, 15, 68, 17, "Depth" }, { ensRate, 248, 15, 68, 17, "Rate" },
        { dlyMix, 112, 50, 68, 17, "Mix" }, { dlyTime, 180, 50, 68, 17, "Time" }, { dlyFeedback, 248, 50, 68, 17, "Fdbk" },
        { revMix, 112, 85, 68, 17, "Mix" }, { revSize, 180, 85, 68, 17, "Size" }, { revDecay, 248, 85, 68, 17, "Decay" },
        { revDamp, 112, 103, 68, 17, "Tone" }, { revPredelay, 180, 103, 68, 17, "Pre" }, { shimmer, 248, 103, 68, 17, "Shim" },
        { volume, 4, 103, 103, 17, "Volume" },
    };
    switch (p)
    {
        case chordsPage: return { chordCells, (int) std::size (chordCells) };
        case motionPage: return { motionCells, (int) std::size (motionCells) };
        case oscPage: return { oscCells.data(), (int) oscCells.size() };
        case filterPage: return { filterCells, (int) std::size (filterCells) };
        case ampPage: return { ampCells, (int) std::size (ampCells) };
        default: return { fxCells, (int) std::size (fxCells) };
    }
}

// The same plots are used for the first paint and the small animated regions.
bool Screen::moving (uint32_t* pixels, int stride, uint32_t sounding)
{
    if (page == chordsPage)
        return picture.region<320, 20> (pixels, stride, false, 0, headH + 104, [&] (Raster& r) { keyboard (r, 4, 4, 312, 14, sounding); });
    if (page == oscPage)
        return picture.region<320, 35> (pixels, stride, false, 0, headH, [&] (Raster& r) { plots (r, sounding); });
    if (page == fxPage)
        return picture.region<103, 13> (pixels, stride, false, 4, headH + 89, [&] (Raster& r) { outputMeter (r, 0, 0); });
    if (page == motionPage)
        return picture.region<127, bodyH> (pixels, stride, false, 0, headH, [&] (Raster& r) { plots (r, sounding); });
    return picture.region<155, bodyH> (pixels, stride, false, 0, headH, [&] (Raster& r) { plots (r, sounding); });
}

void Screen::formatted (uint32_t id, char* s, uint32_t n) const
{
    if (id == ParamId::key)
    {
        const bool minor = progressionTable[choice (ParamId::progression)].minor;
        const char* key = keyName (choice (id), minor);
        std::snprintf (s, n, "%.*s", (int) std::strlen (key) - (minor ? 1 : 0), key);
        return;
    }
    formatParam (paramTable[id], live (id), s, n);
}

void Screen::header (const Raster& r) const
{
    using namespace hdui;
    r.bitmap (0, 0, 320, 43, art::horizon);
    const int prog = std::clamp (choice (ParamId::progression), 0, progressionCount - 1);
    const auto& progression = progressionTable[prog];
    char key[5], mode[16];
    formatted (ParamId::key, key, sizeof (key));
    std::snprintf (mode, sizeof (mode), "%s %s", key, progression.minor ? "minor" : "major");
    const int w = r.measure (mode, 10) + 12;
    r.gel (141, 9, w, 14, 0xff4b9db8, 0xff346c86, 0xff17435e, 0xff19556e);
    r.centre (141 + w / 2, 10, mode, ink::white, 10);
    int x = r.text (8, 27, progression.name, ink::white);
    r.box (x + 5, 30, 1, 7, 0xff95d2de);
    r.text (x + 12, 27, voicingChoices[choice (ParamId::voicing)], ink::white);

    static const char* const names[] = { "chords", "motion", "osc", "filter", "amp", "fx" };
    const int edges[] = { 17, 76, 135, 177, 226, 269, 301 };
    r.gradient (0, 43, 320, 16, 0xff35738c, 0xff103f5e);
    for (int p = 0; p < pageCount; ++p)
    {
        const int start = edges[p], span = edges[p + 1] - start;
        if (page == p)
            r.gel (start + 1, 44, span - 2, 14, 0xff689834, 0xffecffbd, 0xffa9de5e, 0xffc0ee77);
        r.centre (start + span / 2, 44, names[p], page == p ? ink::text : ink::white);
    }
    arrow (r, 9, 51, -1, ink::ice);
    arrow (r, 310, 51, 1, ink::ice);
}

void Screen::arrow (const Raster& r, int x, int y, int dir, Ink color)
{
    r.box (x - 4, y, 9, 1, color);
    r.line (x + 4 * dir, y, x + dir, y - 3, color);
    r.line (x + 4 * dir, y, x + dir, y + 3, color);
}

void Screen::plots (const Raster& r, uint32_t sounding) const
{
    using namespace hdui;
    switch (page)
    {
        case motionPage: field (r, 4, 3, 120, 118, sounding); break;
        case oscPage: scope (r, 4, 3, 154, 31, 0); scope (r, 162, 3, 154, 31, 1); break;
        case filterPage:
            response (r, 4, 3, 148, 57);
            envelope (r, 4, 64, 148, 57, ParamId::fltAttack, tel.fltEnv.load (std::memory_order_relaxed), ink::lime, sounding != 0);
            break;
        case ampPage:
            envelope (r, 4, 3, 148, 57, ParamId::ampAttack, tel.ampEnv.load (std::memory_order_relaxed), ink::foam, sounding != 0);
            lfoPicture (r, 4, 64, 148, 57);
            break;
    }
}

void Screen::section (const Raster& r, int x, int y, int end, const char* title)
{
    const int edge = r.text (x + 2, y, title, hdui::ink::label, 10);
    r.box (edge + 5, y + 6, end - edge - 5, 1, hdui::ink::rule);
}

void Screen::body (const Raster& r, uint32_t sounding, uint32_t held)
{
    using namespace hdui;
    plots (r, sounding);
    switch (page)
    {
        case chordsPage:
            padGrid (r, held);
            keyboard (r, 4, 108, 312, 14, sounding);
            break;
        case filterPage:
            section (r, 157, 1, 316, "Filter response");
            section (r, 157, 65, 316, "Filter envelope");
            break;
        case ampPage:
            section (r, 157, 1, 316, "Amplitude envelope");
            section (r, 157, 61, 316, "Modulation oscillator");
            break;
        case fxPage:
            r.bitmap (4, 3, 103, 99, art::aquarium);
            r.frame (4, 3, 103, 99, ink::guide);
            outputMeter (r, 4, 89);
            section (r, 112, 2, 316, "Ensemble");
            section (r, 112, 37, 316, "Tempo delay");
            section (r, 112, 72, 316, "Shimmer reverb");
            break;
    }
    const Layout l = layout (page);
    for (int c = 0; c < l.count; ++c) cell (r, l.cells[c], c == focus[page], c == focus[page] && editing);
}

void Screen::cell (const Raster& r, const Cell& c, bool focused, bool edited) const
{
    using namespace hdui;
    if (focused)
    {
        if (edited) r.gel (c.x, c.y, c.w, c.h, 0xff94531d, 0xffffe3a1, 0xffedac4b, 0xffffc55e);
        else r.gel (c.x, c.y, c.w, c.h, ink::deep, 0xff367e9b, 0xff09567c, 0xff09718d);
    }
    else
    {
        r.box (c.x, c.y, c.w, c.h - 1, ink::sheet);
        r.box (c.x, c.y + c.h - 1, c.w - 1, 1, ink::rule);
    }
    char s[40];
    formatted (c.id, s, sizeof (s));
    const int space = c.w - 8 - r.measure (c.label, 10);
    const int size = r.measure (s, 12) <= space ? 12 : 10;
    r.text (c.x + 3, c.y + (c.h - 11) / 2, c.label, focused && ! edited ? ink::ice : ink::label, 10);
    r.right (c.x + c.w - 3, c.y + (c.h - size - 1) / 2, s, focused && ! edited ? ink::white : ink::text, size);
}

// The sixteen pads, pad 1 bottom left (as on the MPC); 9-16 the lifted chords.
void Screen::padGrid (const Raster& r, uint32_t held) const
{
    using namespace hdui;
    const int prog = std::clamp (choice (ParamId::progression), 0, progressionCount - 1);
    const int key = choice (ParamId::key);
    const bool minor = progressionTable[prog].minor;
    const int last = tel.lastSlot.load (std::memory_order_relaxed);
    const ParsedProgression& pp = parsedProgressions()[prog];
    for (int slot = 0; slot < padSlots; ++slot)
    {
        const int col = slot % 4, row = 3 - slot / 4;
        const int x = 4 + 42 * col, y = 3 + 25 * row;
        const bool lift = slot >= 8, on = (held >> slot) & 1u;
        const Ink rim = on ? 0xffa96419 : (lift ? 0xff65a86f : 0xff4c99ae);
        if (on) r.gel (x, y, 40, 24, rim, 0xffffecb7, 0xffffb54a, 0xffffce69);
        else if (lift) r.gel (x, y, 40, 24, rim, 0xffeeffcf, 0xffa8ddb3, 0xffb9edd2);
        else r.gel (x, y, 40, 24, rim, 0xffddf9ff, 0xff98d4ec, 0xffb4e5f3);
        char root[12];
        const ChordSymbol& c = pp.chords[slot % 8];
        chordRootText (c, key, minor, root, sizeof (root));
        const char* q = chordQualityText (c);
        r.text (x + 4, y + 1, root, ink::text, r.measure (root, 12) > 25 ? 10 : 12);
        r.text (x + 4, y + 12, q[0] ? q : "maj", ink::text, 10);
        if (slot == last)
            r.box (x + 15, y + 21, 10, 2, on ? 0xff7d470f : ink::blue);
        // The two-digit pad index has its own space even on a transposed slash chord.
        static constexpr uint16_t digits[] = {
            0b111101101101111, 0b010110010010111, 0b111001111100111, 0b111001111001111, 0b101101111001001,
            0b111100111001111, 0b111100111101111, 0b111001010010010, 0b111101111101111, 0b111101111001111
        };
        for (int digit = 0; digit < 2; ++digit)
        {
            const int value = digit == 0 ? (slot + 1) / 10 : (slot + 1) % 10;
            for (int bit = 0; bit < 15; ++bit)
                if (digits[value] & (1 << (14 - bit)))
                    r.dot (x + 30 + digit * 4 + bit % 3, y + 4 + bit / 3, lift ? 0xff427361 : 0xff3a697d);
        }
    }
}

// Notes C1 to C7; the sounding ones lit (the bass gold), else the last chord's, dim.
void Screen::keyboard (const Raster& r, int x0, int y0, int w, int h, uint32_t sounding) const
{
    using namespace hdui;
    constexpr int low = 24, high = 96;
    const int whites = 42, kw = w / whites, left = x0 + (w - kw * whites) / 2;
    r.box (x0, y0 - 2, w, h + 3, ink::guide);
    float lit[128] {};
    bool any = false;
    for (int i = 0; i < maxVoices; ++i)
        if ((sounding >> i) & 1u)
        {
            const int n = tel.notes[i].load (std::memory_order_relaxed);
            if (n >= 0 && n < 128) lit[n] = std::max (lit[n], std::min (1.0f, 0.25f + tel.levels[i].load (std::memory_order_relaxed) * 1.5f)), any = true;
        }
    int lowest = 128;
    if (! any)
    {
        const int slot = tel.lastSlot.load (std::memory_order_relaxed);
        if (slot >= 0 && slot < padSlots)
        {
            // the chord the pad plays, read from the same table as the engine (never searched for)
            const ChordNotes c = padChord (choice (ParamId::progression), choice (ParamId::voicing), choice (ParamId::key), slot);
            const int shift = 12 * choice (ParamId::octave);
            for (int i = 0; i < c.count; ++i)
            {
                const int n = c.note[i] + shift;
                if (n >= 0 && n < 128) lit[n] = 0.2f;
            }
        }
    }
    for (int n = low; n <= high; ++n)
        if (lit[n] > 0.0f) { lowest = n; break; }
    static const int whiteOf[12] = { 0, -1, 1, -1, 2, 3, -1, 4, -1, 5, -1, 6 };
    for (int n = low; n < high; ++n)
    {
        const int pc = n % 12;
        if (whiteOf[pc] < 0) continue;
        const int wi = (n / 12 - low / 12) * 7 + whiteOf[pc];
        const int x = left + wi * kw;
        const Ink lightInk = n == lowest && any ? 0xffffa021 : ink::teal;
        r.box (x, y0, kw - 1, h, lit[n] > 0.0f ? mix (ink::white, lightInk, (int) (95 + 160 * lit[n])) : ink::white);
    }
    for (int n = low; n < high; ++n)
    {
        const int pc = n % 12;
        if (whiteOf[pc] >= 0) continue;
        const int wi = (n / 12 - low / 12) * 7 + whiteOf[pc - 1];
        const int x = left + wi * kw + kw - 2;
        const Ink lightInk = n == lowest && any ? 0xffffa021 : ink::teal;
        r.box (x, y0, 4, h * 3 / 5, lit[n] > 0.0f ? mix (ink::deep, lightInk, (int) (115 + 140 * lit[n])) : ink::deep);
    }

}

// The X / Y field: its grid, the path around (X, Y), the notes on it.
void Screen::field (const Raster& r, int x0, int y0, int w, int h, uint32_t sounding) const
{
    using namespace hdui;
    // The meridians and the specular highlight belong to the lens, so bake them once.
    static constexpr int gw = 120, gh = 118;
    static const auto lens = [] {
        std::array<Ink, gw * gh> px {};
        Raster glass { px.data(), gw, gh };
        glass.gradient (0, 0, gw, gh, 0xff0d526f, 0xff052e4c);
        constexpr int cx = 60, cy = 53, radius = 45;
        for (int y = -radius; y <= radius; ++y)
        {
            const int half = (int) std::sqrt ((float) (radius * radius - y * y));
            for (int x = -half; x <= half; ++x)
            {
                const float nx = (float) x / radius, ny = (float) y / radius;
                const float nz = std::sqrt (std::max (0.f, 1.f - nx * nx - ny * ny));
                const float light = std::clamp (0.52f * nz - 0.33f * nx - 0.45f * ny, 0.f, 1.f);
                Ink color = mix (0xff053f63, 0xff4bbccb, (int) (light * 220));
                const float hx = (nx + .33f) / .68f, hy = (ny + .68f) / .32f;
                const float reflection = std::max (0.f, 1.f - hx * hx - hy * hy);
                color = mix (color, ink::white, (int) (reflection * 125.f));
                glass.dot (cx + x, cy + y, color);
            }
            if (y % 15 == 0) glass.box (cx - half, cy + y, 2 * half + 1, 1, 0xff458f9e);
            for (int meridian = -2; meridian <= 2; ++meridian)
                glass.dot (cx + meridian * half / 3, cy + y, 0xff5599a4);
        }
        glass.ring (cx, cy, radius, 0xff69aeba);
        glass.frame (0, 0, gw, gh, ink::guide);
        return px;
    }();
    for (int j = 0; j < gh; ++j)
        std::copy_n (lens.data() + j * gw, gw, r.px + (y0 + j) * r.w + x0);
    auto px = [&] (float v) { return x0 + w / 2 + (int) std::lround (v * 45); };
    auto py = [&] (float v) { return y0 + 53 - (int) std::lround (v * 45); };
    const float cx = live (ParamId::motionX), cy = live (ParamId::motionY);
    const int shape = choice (ParamId::motionShape);
    const float depth = shape == Motion::off ? 0.0f : live (ParamId::motionDepth);
    if (depth > 0.0f)
    {
        int lx = 0, ly = 0;
        for (int i = 0; i <= 160; ++i)
        {
            float mx, my;
            motionPoint (shape, (float) i / 160.0f, mx, my);
            const int x = px (std::clamp (cx + depth * mx, -1.0f, 1.0f)), y = py (std::clamp (cy + depth * my, -1.0f, 1.0f));
            if (i > 0) r.line (lx, ly, x, y, ink::lime);
            lx = x, ly = y;
        }
    }
    // the centre
    const int ccx = px (cx), ccy = py (cy);
    r.box (ccx - 4, ccy, 9, 1, ink::gold);
    r.box (ccx, ccy - 4, 1, 9, ink::gold);
    // the path's own point, then each note's
    r.ring (px (tel.pathX.load (std::memory_order_relaxed)), py (tel.pathY.load (std::memory_order_relaxed)), 4, ink::white);
    for (int i = 0; i < maxVoices; ++i)
        if ((sounding >> i) & 1u)
        {
            const float lv = std::min (1.0f, tel.levels[i].load (std::memory_order_relaxed) * 1.6f);
            const int n = tel.notes[i].load (std::memory_order_relaxed);
            const Ink c = mix (ink::lime, ink::foam, std::clamp ((n - 40) * 6, 0, 255));
            const int x = px (tel.x[i].load (std::memory_order_relaxed)), y = py (tel.y[i].load (std::memory_order_relaxed));
            r.disc (x, y, 3, mix (ink::field, c, (int) (90 + 160 * lv)));
            r.dot (x - 1, y - 1, ink::white);
        }
    r.text (x0 + 5, y0 + h - 15, motionChoices[std::clamp (shape, 0, Motion::count - 1)], ink::white);
    r.right (x0 + w - 5, y0 + h - 14, "X / Y", ink::ice, 10);
}

void Screen::well (const Raster& r, int x, int y, int w, int h)
{
    using namespace hdui;
    r.gradient (x, y, w, h, 0xff145d79, ink::deep);
    for (int i = 1; i < 6; ++i)
        r.dotted (x + w * i / 6, y + 2, x + w * i / 6, y + h - 2, ink::grid, 3);
    r.frame (x, y, w, h, ink::guide);
}

// An oscillator's wave where its Position is now (the newest note's, else the knob's).
void Screen::scope (const Raster& r, int x0, int y0, int w, int h, int o) const
{
    using namespace hdui;
    well (r, x0, y0, w, h);
    r.box (x0, y0 + h / 2, w, 1, ink::grid);
    r.text (x0 + 4, y0 + 1, o == 0 ? "OSC 1" : "OSC 2", ink::ice, 10);
    x0 += 32;
    w -= 34;
    const int wave = std::clamp (choice (ParamId::osc (o, 0)), 0, waveCount - 1);
    const bool sounding = tel.sounding.load (std::memory_order_relaxed) != 0;
    const float pos = sounding ? tel.morph[o].load (std::memory_order_relaxed) : live (ParamId::osc (o, 1));
    const float xf = pos * (float) (waveFrames - 1);
    const int f = std::min ((int) xf, waveFrames - 2);
    const float m = xf - (float) f;
    const WaveTable& a = WaveBank::get().table (wave, f, 2);
    const WaveTable& b = WaveBank::get().table (wave, f + 1, 2);
    const float level = live (ParamId::osc (o, 7));
    int ly = 0;
    for (int x = 0; x < w; ++x)
    {
        const uint32_t i = (uint32_t) ((float) x / (float) w * (float) a.length);
        const float v = (a.data[i] + (b.data[i] - a.data[i]) * m) * 2.2f;
        const int y = y0 + h / 2 - (int) std::lround (std::clamp (v, -1.0f, 1.0f) * (float) (h / 2 - 3));
        const Ink c = o ? ink::lime : ink::foam;
        const Ink c2 = level > 0.0f ? c : ink::guide;
        const int mid = y0 + h / 2;
        r.box (x0 + x, std::min (mid, y), 1, std::abs (mid - y), mix (ink::field, c2, 52));
        if (x > 0) r.line (x0 + x - 1, ly, x0 + x, y, c2);
        r.blend (x0 + x, y + 1, c2, 70);
        ly = y;
    }

}

// The filter's response, 20 Hz to 20 kHz, -36 to +18 dB; the newest note's cutoff marked.
void Screen::response (const Raster& r, int x0, int y0, int w, int h) const
{
    using namespace hdui;
    well (r, x0, y0, w, h);
    x0 += 2; y0 += 3; w -= 4; h -= 17;
    const int type = choice (ParamId::filterType);
    const float fc = cutoffHz (live (ParamId::cutoff));
    const float k = 2.0f - 1.94f * live (ParamId::resonance);
    auto db = [&] (float hz) {
        const float wv = hz / fc;
        auto mag = [&] (float kk) { const float re = 1.0f - wv * wv, im = kk * wv; return 1.0f / std::sqrt (re * re + im * im); };
        float m;
        switch (type)
        {
            case Filter::lp24: m = mag (1.4142136f) * mag (k); break;
            case Filter::bp: m = k * wv * mag (k); break;
            case Filter::hp: m = wv * wv * mag (k); break;
            default: m = mag (k); break;
        }
        return 20.0f * std::log10 (std::max (m, 1e-6f));
    };
    for (int i = 1; i < 3; ++i) r.dotted (x0 + i * w / 3, y0, x0 + i * w / 3, y0 + h - 1, ink::grid, 2);
    const int zero = y0 + (int) ((18.0f / 54.0f) * (float) h);
    r.box (x0, zero, w, 1, ink::grid);
    int ly = 0;
    for (int x = 0; x < w; ++x)
    {
        const float hz = 20.0f * std::pow (1000.0f, (float) x / (float) (w - 1));
        const float d = std::clamp (db (hz), -36.0f, 18.0f);
        const int y = y0 + (int) std::lround ((18.0f - d) / 54.0f * (float) (h - 1));
        for (int yy = y; yy < y0 + h; ++yy) r.blend (x0 + x, yy, ink::foam, std::max (0, 60 - (yy - y) * 2));
        if (x > 0) r.line (x0 + x - 1, ly, x0 + x, y, ink::foam);
        ly = y;
    }
    if (tel.sounding.load (std::memory_order_relaxed) != 0)
    {
        const float live = tel.cutoff.load (std::memory_order_relaxed);
        const int x = x0 + (int) std::lround (std::log (std::max (live, 20.0f) / 20.0f) / std::log (1000.0f) * (float) (w - 1));
        const int mark = std::clamp (x, x0, x0 + w - 1);
        r.dotted (mark, y0, mark, y0 + h - 1, ink::gold, 2);
    }
    r.text (x0 + 2, y0 + h + 1, "20 Hz", ink::ice, 10);
    r.right (x0 + w - 2, y0 + h + 1, "20 kHz", ink::ice, 10);
}

// An ADSR from the four ids from `first`, with where the newest note is.
void Screen::envelope (const Raster& r, int x0, int y0, int w, int h, uint32_t first, float now, Ink c, bool sounding) const
{
    using namespace hdui;
    well (r, x0, y0, w, h);
    const float a = envMs (live (first)), d = envMs (live (first + 1)), s = live (first + 2), rl = envMs (live (first + 3));
    constexpr float holdMs = 500.f;
    const float total = a + d + holdMs + rl;
    const int wa = (int) ((float) (w - 8) * a / total), wd = (int) ((float) (w - 8) * d / total);
    const int ws = (int) ((float) (w - 8) * holdMs / total), wr = (w - 8) - wa - wd - ws;
    const int top = y0 + 6, bottom = y0 + h - 4, sy = bottom - (int) ((float) (bottom - top) * s);
    const int x1 = x0 + 4, x2 = x1 + wa, x3 = x2 + wd, x4 = x3 + ws, x5 = x4 + wr;
    auto curve = [&] (int xa, int ya, int xb, int yb, bool exp) {
        int lx = xa, ly = ya;
        const int n = std::max (1, xb - xa);
        for (int i = 1; i <= n; ++i)
        {
            const float t = (float) i / (float) n;
            const float e = exp ? 1.0f - std::exp (-4.0f * t) : t;
            const int x = xa + (xb - xa) * i / n, y = ya + (int) std::lround ((float) (yb - ya) * (exp ? e / (1.0f - std::exp (-4.0f)) : e));
            r.line (lx, ly, x, y, c);
            for (int yy = y + 1; yy < bottom; ++yy) r.blend (x, yy, c, 26);
            lx = x, ly = y;
        }
    };
    curve (x1, bottom, x2, top, false);
    curve (x2, top, x3, sy, true);
    curve (x3, sy, x4, sy, false);
    curve (x4, sy, x5, bottom, true);
    if (sounding)
    {
        const int y = bottom - (int) ((float) (bottom - top) * std::clamp (now, 0.0f, 1.0f));
        r.box (x0 + 1, y, 3, 1, ink::white);
        r.disc (x0 + w - 5, y, 2, ink::white);
    }
    r.disc (x2, top, 2, c);
    r.disc (x3, sy, 2, c);
    r.disc (x4, sy, 2, c);
}

void Screen::lfoPicture (const Raster& r, int x0, int y0, int w, int h) const
{
    using namespace hdui;
    well (r, x0, y0, w, h);
    r.box (x0, y0 + h / 2, w, 1, ink::grid);
    const int shape = choice (ParamId::lfoShape);
    // Show two cycles (at least a second) with the delay on the same time axis.
    const float freq = lfoHz (live (ParamId::lfoRate));
    const float span = std::max (1.f, 2.f / freq);
    const float cycles = span * freq, fade = std::max (0.001f, lfoDelayMs (live (ParamId::lfoDelay)) * 0.001f / span);
    int ly = 0;
    for (int x = 0; x < w; ++x)
    {
        const float t = (float) x / (float) w;
        const float ph = t * cycles;
        float v;
        if (shape == LfoShape::triangle) v = 1.0f - 4.0f * std::fabs (ph - std::floor (ph) - 0.5f);
        else if (shape == LfoShape::drift) v = 0.6f * std::sin (ph * 6.28f) + 0.4f * std::sin (ph * 2.7f + 1.0f);
        else v = std::sin (ph * 6.2831853f);
        v *= std::min (1.0f, t / fade);
        const int y = y0 + h / 2 - (int) std::lround (v * (float) (h / 2 - 6));
        if (x > 0) r.line (x0 + x - 1, ly, x0 + x, y, ink::lime);
        ly = y;
    }
    r.text (x0 + 4, y0 + 2, lfoShapeChoices[shape], ink::ice, 10);
    char scale[20];
    std::snprintf (scale, sizeof (scale), "%.1f s", span);
    r.right (x0 + w - 4, y0 + 2, scale, ink::ice, 10);
}

// Post-limiter stereo peak, in 3 dB segments from -48 dBFS to full scale.
void Screen::outputMeter (const Raster& r, int x, int y) const
{
    using namespace hdui;
    r.box (x, y, 103, 13, ink::deep);
    r.text (x + 4, y + 1, "OUT", ink::ice, 10);
    const float peak = tel.reverb.load (std::memory_order_relaxed);
    const float db = 20.f * std::log10 (std::max (peak, 1.e-5f));
    for (int i = 0; i < 16; ++i)
    {
        const bool lit = db >= -48.f + (float) i * 3.f;
        r.box (x + 28 + i * 4, y + 4, 3, 5, lit ? (i > 13 ? ink::gold : ink::lime) : ink::grid);
    }
}

void Screen::knob (const Raster& r, int x, int y, float unit, Ink trackColor)
{
    using namespace hdui;
    // An acrylic cap in a metal socket. The indicator uses the same 270-degree throw for every pot.
    r.disc (x, y + 1, 9, ink::deep);
    r.disc (x, y, 9, 0xff5590a4);
    for (int j = -7; j <= 7; ++j)
    {
        const int half = (int) std::sqrt ((float) (49 - j * j));
        const Ink color = j < 0 ? mix (0xffcff9ff, 0xff4699b4, (j + 7) * 31)
                                : mix (0xff166081, 0xff69b6c7, j * 33);
        r.box (x - half, y + j, 2 * half + 1, 1, color);
    }
    const float angle = (unit * 1.5f - 1.25f) * 3.14159265f;
    const int dx = (int) std::lround (6.f * std::cos (angle)), dy = (int) std::lround (6.f * std::sin (angle));
    r.line (x + dx / 2, y + dy / 2, x + dx, y + dy, ink::deep);
    r.dot (x + dx, y + dy, ink::white);
    r.dot (x, y, mix (trackColor, ink::ice, 128));
}

void Screen::dock (const Raster& r, const Host& h) const
{
    using namespace hdui;
    const Layout l = layout (page);
    r.gradient (0, 0, 320, dockH, 0xff367995, 0xff113b58);
    r.box (0, 0, 320, 1, ink::white);
    for (int k = 0; k < 3; ++k)
    {
        const int x = 4 + 105 * k;
        if (k) r.box (x - 2, 4, 1, 25, 0xff4f8aa0);
        if (h.shift)
        {
            static const char* const keys[] = { "WHEEL", "PUSH", "MINUS / PLUS" };
            const char* actions[] = { editing ? "Fine value" : "Select control",
                                      editing ? "Finish edit" : "Edit / flip", editing ? "Coarse value" : "Change page" };
            r.text (x + 4, 4, keys[k], ink::ice, 10);
            r.text (x + 4, 16, actions[k], ink::white);
            continue;
        }
        const int c = focus[page] + k;
        const char tag[] = { 'K', char ('1' + k), 0 };
        r.centre (x + 10, 21, tag, ink::ice, 10);
        if (c >= l.count) continue;
        const uint32_t id = l.cells[c].id;
        const auto& p = paramTable[id];
        const float unit = (live (id) - p.min) / (p.max - p.min);
        knob (r, x + 10, 12, unit, accent);
        r.text (x + 22, 2, p.name, ink::ice, 10);
        char s[40];
        formatted (id, s, sizeof (s));
        const int size = r.measure (s, 12) <= 80 ? 12 : 10;
        const int valueX = std::min (x + 22, x + 103 - r.measure (s, size));
        r.text (valueX, 13, s, ink::white, size);
        const int start = x + 23, w = 76, rail = 28;
        const int at = start + (int) std::lround (unit * (float) (w - 1));
        r.box (start, rail, w, 2, ink::deep);
        const int origin = p.min < 0 ? start + (w - 1) / 2 : start;
        r.box (std::min (at, origin), rail, std::abs (at - origin) + 1, 2, ink::foam);
        r.box (at - 1, rail - 2, 3, 5, ink::white);
        r.dot (at, rail - 1, accent);
        if (h.pot[k] >= 0 && ! h.engaged[k])
        {
            const int pos = start + std::clamp ((int) h.pot[k], 0, 127) * (w - 1) / 127;
            r.frame (pos - 2, rail - 2, 5, 6, ink::gold);
        }
    }
}
} // namespace reverie
