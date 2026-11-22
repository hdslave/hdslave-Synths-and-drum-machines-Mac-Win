// Blacklight Transfer pitch roll. Included after the shared plot helpers.
#pragma once

namespace tbmpc::plots
{
using hdui::ticketInk;

inline void pitchLabel (const Raster& r, int mid, int y, const char* name, hdui::Ink ink)
{
    const int length = int (std::strlen (name));
    const int advance = length <= 4 ? 5 : 4;
    int x = mid - (length*advance-1)/2;
    for (const char* ch = name; *ch; ++ch, x += advance)
    {
        const char* at = std::strchr (hdui::font::noteChars, *ch);
        if (!at) continue;
        const auto& glyph = hdui::font::notes[at-hdui::font::noteChars];
        for (int j = 0; j < 8; ++j)
            for (int k = 0; k < 4; ++k)
                if ((glyph[j] >> k) & 1) r.dot (x+k, y+j, ink);
    }
}

// All banks share one pitch scale. Give sparse semitone phrases a labelled
// staff, and preserve CV height plus individual names on dense phrases.
struct PitchAxis
{
    int notes[32], count, low, high;
    bool semitones = true;
    PitchAxis (const PatternView& v, int n)
    {
        for (int i = 0; i < n; ++i)
        {
            notes[i] = int (std::lround (v.pitch[i] / 128.0));
            semitones &= v.pitch[i] % 128 == 0;
        }
        std::sort (notes, notes+n);
        count = int (std::unique (notes, notes+n) - notes);
        const auto range = std::minmax_element (v.pitch, v.pitch+n);
        low = int (std::floor (*range.first / 1536.0))*12;
        high = std::max (low+12, int (std::ceil (*range.second / 1536.0))*12);
    }
};

inline void rollBank (const Raster& r, const PatternView& v, const PitchAxis& axis,
                      int start, int count, int y, int h, int active)
{
    constexpr int left = 29, width = 204;
    const int acc = y+h-(h > 100 ? 27 : 18), slide = y+h-(h > 100 ? 12 : 9);
    const int top = y+(h > 100 ? 21 : 12), foot = acc-11;
    const bool named = axis.semitones && (axis.count-1)*10 <= foot-top;
    const int bottom = named ? foot : acc-24;
    auto column = [](int i) { return left + i*width/16; };
    auto center = [&](int i) { return (column (i)+column (i+1))/2; };
    auto namedY = [&](int rank) {
        return axis.count == 1 ? (top+bottom)/2 : bottom - rank*(bottom-top)/(axis.count-1);
    };
    auto linearY = [&](int pitch) {
        return bottom - (pitch-axis.low*128)*(bottom-top)/((axis.high-axis.low)*128);
    };
    auto rule = [&](int pitch, int line) {
        char name[12];
        noteName (pitch, name);
        r.right (left-3, line-3, name, ticketInk.muted);
        for (int x = left; x < column (count); x += 3) r.dot (x, line, ticketInk.grid);
    };
    if (active >= start && active < start+count)
    {
        const int x = center (active-start)-4;
        r.box (x, y, 8, h-2, ticketInk.cursor);
        r.box (x, y, 8, 2, ticketInk.live);
    }
    if (named)
        for (int i = 0; i < axis.count; ++i) rule (axis.notes[i], namedY (i));
    else
    {
        rule (axis.high, top);
        rule (axis.low, bottom);
    }
    for (int i = 0; i < count; i += 4)
    {
        const int step = start+i+1;
        const char label[] = { char ('0'+step/10), char ('0'+step%10), 0 };
        r.text (column (i)+1, y+3, label, ticketInk.violet, h < 100);
        for (int j = top; j <= bottom; j += 5) r.dot (column (i), j, ticketInk.rule);
    }
    r.box (3, acc-3, 230, 1, ticketInk.grid);
    r.box (3, slide-3, 230, 1, ticketInk.grid);
    r.text (3, acc-1, "ACC", ticketInk.pink, h < 100);
    r.text (3, slide-1, "SLD", ticketInk.violet, h < 100);
    int lines[16];
    for (int i = 0; i < count; ++i)
    {
        const int cv = v.pitch[start+i];
        lines[i] = named ? namedY (int (std::lower_bound (axis.notes,axis.notes+axis.count,cv/128)-axis.notes))
                         : linearY (cv);
        if (!i || !((v.slides >> (start+i-1)) & 1)) continue;
        const int dx = center (i)-center (i-1), dy = lines[i]-lines[i-1], span = std::max (dx,std::abs (dy));
        for (int j = 1; j < span; ++j)
            r.dot (center (i-1)+j*dx/span, lines[i-1]+j*dy/span, ticketInk.pink);
    }
    for (int i = 0; i < count; ++i)
    {
        const int step = start+i, m = center (i), line = lines[i];
        const bool lit = active == step;
        const auto c = lit ? ticketInk.live : ticketInk.paper;
        if ((v.gates >> step) & 1)
        {
            r.box (m-2, line-2, 7, 2, c);
            r.box (m-3, line, 7, 2, c);
        }
        else
        {
            r.dot (m,line-3,c);
            r.dot (m,line+3,c);
            for (int j = -2; j <= 2; ++j)
            {
                r.dot (m-3+std::abs (j),line+j,c);
                r.dot (m+3-std::abs (j),line+j,c);
            }
        }
        if (!named)
        {
            const auto label = lit ? ticketInk.live : ticketInk.muted;
            char name[12];
            noteName (int (std::lround (v.pitch[step]/128.0)), name);
            // Stagger adjacent labels so their naturals, sharps and octaves have
            // two columns of horizontal space, even in a 32-step phrase.
            pitchLabel (r, std::clamp (m, left+8, r.w-11), acc-20+(i%2)*8, name, label);
        }
        if ((v.accents >> step) & 1)
            for (int j = -3; j <= 3; ++j)
            {
                const int half = 3-std::abs (j);
                r.box (m-half, acc+3+j, 2*half+1, 1, lit ? ticketInk.live : ticketInk.pink);
            }
        else r.dot (m, acc+3, ticketInk.rule);
        if ((v.slides >> step) & 1)
            for (int j = 0; j < 7; ++j) r.box (m-3+j, slide+4-j/2, 1, 2, lit ? ticketInk.live : ticketInk.violet);
        else r.dot (m, slide+3, ticketInk.rule);
    }
}

inline void pitchRoll (const Raster& r, const PatternView& v, bool playing)
{
    const int n = stepCount (v);
    if (!n)
    {
        r.text (68, 65, "Awaiting clock", ticketInk.paper);
        return;
    }
    const PitchAxis axis (v,n);
    const int active = playing && v.running ? v.current : -1;
    const int h = n > 16 ? r.h/2 : r.h;
    rollBank (r, v, axis, 0, std::min (n,16), 0, h, active);
    if (n > 16) rollBank (r, v, axis, 16, n-16, h, h, active);
}

} // namespace tbmpc::plots
