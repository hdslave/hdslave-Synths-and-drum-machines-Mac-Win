// Included after PatternView in screen.h. Plot coordinates are native pixels.
#pragma once

namespace tbmpc::plots
{
using hdui::Raster;
namespace ink = hdui::ink;


inline int stepCount (const PatternView& view)
{
    return std::clamp (int (view.steps), 0, 32);
}

inline void noteName (int semitone, char (&s)[12])
{
    const int midi = semitone + noteAtZeroVolts;
    const int pc = (midi % 12 + 12) % 12;
    std::snprintf (s, sizeof (s), "%s%d", rootChoices[pc], (midi - pc) / 12 - 1);
}

inline void pitchLabel (const Raster& r, int mid, int y, int pitch, hdui::Ink c)
{
    char s[12];
    noteName (int (std::lround (pitch / 128.0)), s);
    const int length = int (std::strlen (s));
    const int advance = length > 3 ? 4 : 5;
    int x = mid - (length * advance - 1) / 2;
    for (const char* ch = s; *ch; ++ch, x += advance)
    {
        const char* at = std::strchr (hdui::font::noteChars, *ch);
        if (!at) continue;
        const auto& glyph = hdui::font::notes[at - hdui::font::noteChars];
        for (int j = 0; j < 8; ++j)
            for (int k = 0; k < 4; ++k)
                if ((glyph[j] >> k) & 1) r.dot (x + k, y + j, c);
    }
}

// Each bank has sixteen fixed columns so a note keeps its width on long phrases.
inline void matrix (const Raster& r, const PatternView& v, bool playing)
{
    const int n = stepCount (v);
    if (!n)
    {
        r.box (2, 2, 316, 81, ink::field);
        r.text (117, 39, "Awaiting clock", ink::paper);
        return;
    }
    const int banks = n > 16 ? 2 : 1;
    constexpr int tall[] = { 0, 15, 33, 50, 66, 82 }, shortRows[] = { 0, 9, 19, 27, 35, 43 };
    constexpr const char* labels[] = { "STEP", "NOTE", "GATE", "ACC", "SLD" };
    const int* edges = banks == 1 ? tall : shortRows;
    for (int bank = 0; bank < banks; ++bank)
        for (int lane = 0; lane < 5; ++lane)
        {
            const int y = 1 + bank * 43 + edges[lane], height = edges[lane+1] - edges[lane] - 1;
            const int cy = y + height / 2, ty = y + (height-8)/2;
            r.box (2, y, 36, height, ink::card);
            r.text (6, banks == 2 ? y + (height-6)/2 : ty, labels[lane], ink::paper, banks == 2);
            for (int col = 0; col < 16; ++col)
            {
                const int i = bank * 16 + col, a = 40 + 279*col/16, b = 40 + 279*(col+1)/16;
                const int m = (a+b)/2;
                const bool current = playing && v.running && v.current == i && i < n;
                const auto fill = current ? ink::live : i >= n ? ink::field : lane == 0 ? ink::header
                                          : (col/4)%2 ? ink::alternate : ink::metal;
                r.box (a, y, b-a-1, height, fill);
                if (i >= n) continue;
                if (lane == 0)
                {
                    const char num[] = { char ('0'+(i+1)/10), char ('0'+(i+1)%10), 0 };
                    r.text (m-6, ty, num, current ? ink::bg : ink::paper);
                }
                else if (lane == 1)
                {
                    pitchLabel (r, m, ty, v.pitch[i], ink::bg);
                    if (v.pitch[i] % 128) r.dot (b-3, y+height-1, ink::cobalt);
                }
                else if (lane == 2)
                {
                    if ((v.gates >> i) & 1) r.box (m-4, cy-height/4, 9, std::max (3,height/2), ink::bg);
                    else r.frame (m-3, cy-1, 7, 3, ink::rule);
                }
                else if (lane == 3 && ((v.accents >> i) & 1))
                {
                    const int size = std::min (4, (height-1)/2);
                    for (int j = -size; j <= size; ++j)
                    {
                        const int half = size - std::abs (j);
                        r.box (m-half, cy+j, 2*half+1, 1, ink::bg);
                    }
                }
                else if (lane == 4 && ((v.slides >> i) & 1))
                {
                    for (int k = 0; k < 9; ++k) r.box (m-4+k, cy+2-k/2, 1, 2, ink::bg);
                    r.box (m+1, cy-2, 4, 1, ink::bg);
                    r.box (m+4, cy-2, 1, 4, ink::bg);
                }
                else r.box (m-1, cy, 3, 1, ink::rule);
            }
        }
}

inline void sequence (const Raster& r, const PatternView& v, int x, int y, int w, int h, bool playing, const hdui::PlotPalette& palette)
{
    const int n = stepCount (v);
    if (!n)
    {
        r.box (x, y, w, h, palette.field);
        r.box (x + 4, y + h / 2 - 13, w - 8, 1, palette.grid);
        r.box (x + 4, y + h / 2 + 13, w - 8, 1, palette.grid);
        r.caution (x + 6, y + h / 2 - 4, 11, 8, palette.violet);
        r.text (x + 21, y + h / 2 - 4, "Awaiting clock", palette.paper);
        return;
    }
    const auto limits = std::minmax_element (v.pitch, v.pitch + n);
    const int low = int (std::floor (*limits.first / 1536.0)) * 1536;
    const int high = std::max (low + 1536, int (std::ceil (*limits.second / 1536.0)) * 1536);
    const int columns = n <= 16 ? 16 : 32;
    for (int i = 0; i < n; ++i)
    {
        const int a = x + i * w / columns, b = x + (i + 1) * w / columns;
        const bool current = playing && v.running && v.current == i;
        const auto c = current ? palette.live : palette.paper;
        if (current) r.box (a, y, b - a, h, palette.cursor);
        if (i % 4 == 0) r.box (a, y, 1, h - 5, palette.grid);
        const int line = y + h - 8 - (v.pitch[i] - low) * (h - 11) / (high - low);
        if ((v.gates >> i) & 1) r.box (a + 1, line - 1, std::max (1,b - a - 2), 3, c);
        else r.dot ((a + b) / 2, line, c);
        if ((v.accents >> i) & 1) r.dot ((a + b) / 2, y + h - 4, current ? c : palette.pink);
        if ((v.slides >> i) & 1) r.box ((a + b) / 2, y + h - 2, std::max (1,(b - a) / 2), 1, current ? c : palette.violet);
        if (current) r.box (a, y, b - a, 2, palette.live);
    }
}

inline void scopePanel (const Raster& r, const char* name)
{
    r.box (2, 2, r.w - 4, r.h - 5, ink::field);
    r.box (2, 2, r.w - 4, 14, ink::card);
    r.text (7, 5, name, ink::paper);
}

inline void response (const Raster& r, const TextContext& ctx, float cutoff, const char* reading)
{
    const bool detailed = reading != nullptr;
    const auto& palette = detailed ? hdui::pressInk : hdui::ticketInk;
    const int kind = int (ctx.filter);
    const int left = detailed ? 8 : 7, right = r.w - (detailed ? 8 : 6);
    const int top = detailed ? 36 : 5, bottom = r.h - (detailed ? 24 : 16);
    const int width = right - left;
    const float k = feedbackOf (kind, ctx.resonance), nominal = nominalHz (kind, cutoff);
    const double ref = filterGain2 (kind, k, ctx.spread,
                                     kind == filterTwinPeak ? std::exp2 (ctx.spread * 0.5) : 1e-3);
    auto py = [&] (double db) {
        return top + int (std::lround ((24.0 - std::clamp (db, -36.0, 24.0)) / 60.0 * (bottom - top)));
    };
    auto px = [&] (double hz) {
        return left + int (std::lround (std::clamp (std::log10 (hz / 20.0) / 3.0, 0.0, 1.0) * (width - 1)));
    };
    if (detailed)
    {
        scopePanel (r, "FILTER");
        r.right (right, 5, filterChoices[kind], palette.paper);
        r.right (right, 21, reading, palette.muted);
    }
    for (double db : { 0.0, -24.0 })
        r.box (left, py (db), width, 1, palette.grid);
    for (int hz : { 100, 1000, 10000 })
        for (int j = top; j <= bottom; j += 4) r.dot (px (hz), j, palette.rule);

    const int edge = px (cutoffHz (cutoff, ctx));
    const auto marker = detailed ? palette.violet : hdui::ticketInk.pink;
    for (int j = top; j <= bottom; j += 3) r.dot (edge, j, marker);
    r.box (edge - 2, bottom + 1, 5, 2, marker);
    // Two samples per pixel retain the narrow resonant peaks at high feedback.
    int previous = 0;
    for (int i = 0; i <= 2 * (width - 1); ++i)
    {
        const double hz = 20.0 * std::pow (1000.0, double (i) / (2 * (width - 1)));
        const double gain = filterGain2 (kind, k, ctx.spread, hz / nominal);
        const int y = py (10.0 * std::log10 (gain / ref + 1e-12));
        if (i == 0) previous = y;
        const int x = left + i / 2, lo = std::min (previous, y), hi = std::max (previous, y);
        r.box (x, lo, 1, hi - lo + 1, palette.live);
        previous = y;
    }
    r.text (left, bottom + (detailed ? 8 : 5), "20 Hz", palette.muted);
    r.right (right, bottom + (detailed ? 8 : 5), "20 kHz", palette.muted);
}
} // namespace tbmpc::plots
