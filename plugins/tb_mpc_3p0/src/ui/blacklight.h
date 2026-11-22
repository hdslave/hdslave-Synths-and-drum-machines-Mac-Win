// Blacklight Transfer drawing for the shared instrument screen.
#pragma once

namespace tbmpc
{
inline bool Screen::paintBlacklight (uint32_t* pixels, int stride, bool whole, const Host& h, int slip, const Changes& d)
{
    bool drew = false;
    if (d.page)
        drew |= picture.region<236,21> (pixels, stride, whole, 84, 0, [&](Raster& r) { ticketTitle (r); });
    if (d.page || slip != lastSlip)
        drew |= picture.region<84,106> (pixels, stride, whole, 0, 0, [&](Raster& r) { passArt (r, slip); });
    if (d.selection || d.parameters || d.publication)
        drew |= picture.region<84,79> (pixels, stride, whole, 0, 106, [&](Raster& r) { passData (r); });
    if (!group)
    {
        if (d.position || d.parameters)
            drew |= picture.region<236,22> (pixels, stride, whole, 84, 21, [&](Raster& r) { transportRow (r, h); });
        if (d.score)
            drew |= picture.region<236,142> (pixels, stride, whole, 84, 43, [&](Raster& r) {
                plots::pitchRoll (r, view, h.playing);
            });
    }
    else
    {
        if (d.page || d.panel)
            drew |= picture.region<236,64> (pixels, stride, whole, 84, 21, [&](Raster& r) {
                if (group < 3) plots::response (r, context(), val (ParamId::cutoff), nullptr);
                else ticketMini (r, h);
            });
        if (d.selection || d.parameters)
            drew |= picture.region<236,100> (pixels, stride, whole, 84, 85, [&](Raster& r) { ticketList (r); });
    }
    return drew;
}

inline void Screen::ticketTitle (const Raster& r) const
{
    using namespace hdui;
    r.mask (5, 4, art::logo, 11, ticketInk.paper);
    for (int y = 2; y < 17; ++y)
    {
        const int x = 162 - (y-2)/2;
        r.box (x, y, 233-x, 1, ticketInk.violet);
    }
    r.right (228, 5, fields().name, ticketInk.bg);
    r.box (4, 19, 229, 1, ticketInk.grid);
}

inline void Screen::passArt (const Raster& r, int slip)
{
    using namespace hdui;
    r.box (0, 0, 83, r.h, ticketInk.field);
    r.stamp (2, 2, blacklightArt::sticker, blacklightArt::palette);
    if (slip)
    {
        r.slip (15, 62, 46, 4, slip);
        r.slip (16, 70, 43, 3, -slip);
    }
    r.box (82, 0, 1, 99, ticketInk.violet);
    for (int x = 3; x < 80; x += 5) r.box (x, 102, 3, 1, ticketInk.violet);
    for (int y = 0; y < 7; ++y)
    {
        const int rim = 4-std::abs (y-3);
        r.box (0, 99+y, rim, 1, ticketInk.bg);
        r.box (83-rim, 99+y, rim, 1, ticketInk.bg);
    }
}

inline void Screen::passData (const Raster& r) const
{
    using namespace hdui;
    r.box (0, 0, 83, 77, ticketInk.field);
    r.box (82, 0, 1, 77, ticketInk.violet);
    char s[48];
    if (!group)
    {
        r.text (4, 3, pendingSeed() ? "NEXT" : "SEED", pendingSeed() ? ticketInk.pink : ticketInk.muted);
        r.right (79, 3, val (ParamId::seedMode) >= .5f ? "LOCKED" : "RANDOM", ticketInk.live);
        r.hex (4, 16, unsigned (val (ParamId::seed)), ticketInk.paper, blacklightArt::digits, 18);
        std::snprintf (s, sizeof (s), "%s / %s", scaleNames[int (val (ParamId::scale))], rootChoices[int (val (ParamId::root))]);
        if (std::strlen (s) <= 13)
        {
            r.text (4, 43, s, ticketInk.muted);
            std::snprintf (s, sizeof (s), "OCT %+d", int (val (ParamId::octave)));
            r.text (4, 57, s, ticketInk.muted);
        }
        else
        {
            const char* scale = scaleNames[int (val (ParamId::scale))];
            char first[14];
            std::snprintf (first, sizeof (first), "%.13s", scale);
            r.text (4, 41, first, ticketInk.muted);
            const size_t used = std::strlen (first);
            std::snprintf (s, sizeof (s), "%s / %s", scale+used, rootChoices[int (val (ParamId::root))]);
            r.text (4, 50, s, ticketInk.muted);
            std::snprintf (s, sizeof (s), "OCT %+d", int (val (ParamId::octave)));
            r.text (4, 59, s, ticketInk.muted);
        }
        std::snprintf (s, sizeof (s), "DEN %+d", int (val (ParamId::density)));
        r.text (4, 68, s, ticketInk.muted);
        return;
    }
    if (group < 3)
    {
        r.text (4, 4, "FILTER", ticketInk.muted);
        r.reading (3, 15, 80, filterChoices[int (val (ParamId::filter))], ticketInk.live, true);
    }
    else
    {
        r.text (4, 4, "SCORE", ticketInk.muted);
        if (plots::stepCount (view)) r.hex (4, 16, unsigned (view.seed), ticketInk.live, blacklightArt::digits, 18);
        else r.reading (4, 15, 80, "----", ticketInk.live, true);
    }
    const auto& f = fields().fields[row];
    r.text (4, 45, f.label, editing ? ticketInk.pink : ticketInk.muted);
    formatted (f.id, s);
    if (r.readingWidth (s) <= 77) r.reading (3, 55, 81, s, ticketInk.paper);
    else
    {
        char first[14];
        std::snprintf (first, sizeof (first), "%.13s", s);
        const char* rest = s+std::strlen (first);
        if (char* gap = std::strrchr (first, ' '))
        {
            *gap = 0;
            rest = s+(gap-first)+1;
        }
        r.text (4, 56, first, ticketInk.paper);
        r.text (4, 66, rest, ticketInk.paper);
    }
}

inline void Screen::transportRow (const Raster& r, const Host& h) const
{
    using namespace hdui;
    const int n = plots::stepCount (view);
    const bool midi = val (ParamId::mode) >= .5f;
    const bool detail = pendingSeed() || midi;
    const int y = detail ? 0 : 5;
    if (h.playing)
        for (int i = 0; i < 7; ++i) r.box (6+i, y+1+i/2, 1, 9-i, ticketInk.live);
    else r.box (6, y+2, 6, 6, ticketInk.muted);
    r.text (18, y+1, h.playing ? "RUN" : "STOP", h.playing ? ticketInk.live : ticketInk.muted);
    char s[24], div[12];
    if (view.current >= 0 && view.current < n)
        std::snprintf (s, sizeof (s), "%02d/%02d", view.current+1, n);
    else std::snprintf (s, sizeof (s), "--/%02d", n ? n : int (val (ParamId::steps)));
    if (detail) r.text (55, y+1, s, ticketInk.paper);
    else r.reading (46, 0, 118, s, ticketInk.paper, true);
    formatted (ParamId::clockDiv, div);
    std::snprintf (s, sizeof (s), "DIV %s", div);
    r.right (232, y+1, s, ticketInk.muted);
    if (pendingSeed())
    {
        std::snprintf (s, sizeof (s), "PENDING / SCORE %04X", unsigned (view.seed) & 65535);
        r.text (5, 12, s, ticketInk.pink);
    }
    if (midi) r.right (232, 12, "MIDI / SEQ MUTED", ticketInk.muted);
}

inline void Screen::ticketList (const Raster& r) const
{
    using namespace hdui;
    const int top = std::min ((row/visible)*visible, std::max (0, fields().count-visible));
    for (int i = top; i < std::min (top+visible,fields().count); ++i)
    {
        const int slot = i-top, x = 4+116*(slot/4), y = 3+24*(slot%4);
        const auto& f = fields().fields[i];
        const bool focus = i == row;
        const int width = f.id == noParam ? r.w-x-4 : 112;
        r.box (x, y, width, focus ? 22 : 1, focus ? ticketInk.violet : ticketInk.grid);
        r.text (x+3, y+2, f.label, focus ? ticketInk.bg : ticketInk.muted);
        char value[32];
        formatted (f.id, value);
        if (focus && editing)
        {
            r.box (x+2, y+11, 108, 10, ticketInk.bg);
            r.text (x+4, y+12, value, ticketInk.live);
        }
        else r.text (x+3, y+12, value, focus ? ticketInk.bg : ticketInk.paper);
    }
}

inline void Screen::ticketMini (const Raster& r, const Host& h) const
{
    using namespace hdui;
    char s[16];
    r.text (5, 2, h.playing ? "RUN" : "STOP", h.playing ? ticketInk.live : ticketInk.muted);
    if (view.current >= 0 && view.current < plots::stepCount (view))
        std::snprintf (s, sizeof (s), "%02d/%02d", view.current+1, view.steps);
    else std::snprintf (s, sizeof (s), "--/--");
    r.right (231, 2, s, ticketInk.paper);
    plots::sequence (r, view, 5, 14, 226, group == 4 ? 32 : 44, h.playing, ticketInk);
    if (group == 4)
    {
        r.text (5, 51, "SWING", ticketInk.muted);
        formatted (ParamId::swing, s);
        r.right (231, 51, s, ticketInk.paper);
        const int split = int (std::lround (141*val (ParamId::swing)));
        r.box (46, 53, split-1, 3, ticketInk.violet);
        r.box (46+split+1, 53, 140-split, 3, ticketInk.pink);
        r.dot (116, 58, ticketInk.paper);
    }
}

inline void Screen::ticketCards (const Raster& r, const Host& h) const
{
    using namespace hdui;
    const Ink track = h.accent ? h.accent | 0xff000000u : ticketInk.violet;
    for (int k = 0; k < 3; ++k)
    {
        const int x = 320*k/3, end = 320*(k+1)/3;
        r.box (x+2, 0, end-x-4, 1, ticketInk.violet);
        r.box (x+3, 4, 14, 10, ticketInk.muted);
        const char tag[] = { 'K', char ('1'+k), 0 };
        r.text (x+4, 5, tag, ticketInk.bg);
        const uint32_t id = cardField (k);
        if (id == noParam) continue;
        const auto& p = paramTable[paramIndex (id)];
        const auto& f = group ? fields().fields[row+k] : layout::voice[k+1];
        r.text (x+21, 5, f.label, ticketInk.muted);
        if (p.kind == Kind::button)
        {
            r.reading (x+3, 15, end-2, k == 0 ? "Push" : "Button", ticketInk.paper);
            continue;
        }
        char value[32];
        formatted (id, value);
        const int width = r.readingWidth (value);
        if (width <= end-x-6) r.reading (x+3, 15, end-2, value, ticketInk.paper);
        else r.text (x+3, 18, value, ticketInk.paper);
        const int begin = x+std::max (49, width+8);
        // A long value uses the full card, with its rail below the baseline.
        const bool stacked = begin > end-23;
        const int left = stacked ? x+4 : begin, y = stacked ? 29 : 24, length = end-6-left;
        const int at = left + int (std::lround ((length-1)*(val (id)-p.min)/(p.max-p.min)));
        r.box (left, y, length, 2, ticketInk.grid);
        r.box (left, y, at-left+1, 2, track);
        r.box (at, y-2, 2, 5, ticketInk.paper);
        if (h.pot[k] >= 0 && !h.engaged[k])
        {
            const int pot = left + std::clamp (int (h.pot[k]),0,127)*(length-1)/127;
            r.box (pot-2, y-2, 5, 5, ticketInk.bg);
            r.frame (pot-2, y-2, 5, 5, ticketInk.paper);
        }
    }
}
} // namespace tbmpc
