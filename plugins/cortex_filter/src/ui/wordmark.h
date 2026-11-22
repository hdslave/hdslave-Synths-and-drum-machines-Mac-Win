// The logo is shaded at compile time. The screen thread only blends scanlines and indexes the spiral.
#pragma once

#include "vortex_coverage.h"
#include <algorithm>
#include <array>
#include <cstdint>

namespace cortex::wordmark
{
using Ink = uint32_t;
constexpr Ink background = 0xff05070e;
constexpr int width = 186, height = 26; // the status readouts can start at x = 188
using Plate = std::array<Ink, width * height>;

// A full turn is 64 steps. Both the reflected light and the enamel in the iris use the same curve.
constexpr int wave (int phase)
{
    constexpr int quarter[] = { 0, 2, 3, 5, 6, 8, 9, 10, 11, 12, 13, 14, 15, 15, 16, 16, 16 };
    const int t = static_cast<unsigned> (phase) & 63;
    const int half = t & 31;
    const int amplitude = quarter[half > 16 ? 32 - half : half];
    return t < 32 ? amplitude : -amplitude;
}

// Mix neighbouring texels in sixteenths of a pixel. Red and blue fit in separate halves of one word.
constexpr Ink between (Ink a, Ink b, unsigned weight)
{
    constexpr Ink rb = 0x00ff00ff, g = 0x0000ff00;
    const unsigned rest = 16 - weight;
    return 0xff000000 | ((((a & rb) * rest + (b & rb) * weight) >> 4) & rb)
                      | ((((a & g) * rest + (b & g) * weight) >> 4) & g);
}

// Angular position on the dial in 64ths of a turn. Only the compiler evaluates the geometry.
constexpr int angle (int x, int y)
{
    const int ax = x < 0 ? -x : x, ay = y < 0 ? -y : y;
    const int a = (16 * ay + (ax + ay) / 2) / (ax + ay);
    const int upper = x < 0 ? 32 - a : a;
    return (y < 0 ? 64 - upper : upper) & 63;
}

// The planet fits in the O counter. A dark outer gutter keeps the letter open around it.
struct Vortex
{
    struct Dot { uint8_t x, y, phase; bool ring; };
    std::array<Dot, 120> dots {};
    int count = 0;

    constexpr Vortex()
    {
        for (int y = 7; y <= 18; ++y)
            for (int x = 31; x <= 47; ++x)
            {
                const int dx = x - 39, dy = y - 13;
                const int u = 3 * dx - dy, v = dx + 3 * dy;
                const int orbit = u * u + 17 * v * v;
                const int r = 3 * dx * dx + 4 * dy * dy;
                const bool ring = orbit >= 465 && orbit <= 690 && (r > 71 || v >= 0);
                if (r > 68 && !ring)
                    continue;
                // Screen y increases downwards. Subtract the angle for clockwise rotation.
                int phase = ring ? angle (u, v * 4) : (r == 0 ? 0 : -2 * angle (dx, dy) - r / 2);
                dots[count++] = { uint8_t (x), uint8_t (y), uint8_t (phase & 63), ring };
            }
    }

    void turn (Ink* dst, int stride, int frame) const;
};
inline constexpr Vortex vortex {};

inline constexpr auto spiral = [] {
    std::array<std::array<Ink, 64>, 2> palette {};
    for (int i = 0; i < 64; ++i)
    {
        const int w = wave (i);
        palette[0][i] = w < -4 ? 0xff181344 : w < 6 ? between (0xff41207c, 0xff26e7bd, (w + 4) * 16 / 10)
                                                : between (0xff26e7bd, 0xffe6ff3b, (w - 6) * 16 / 10);
        palette[1][i] = between (0xff4b6893, 0xffedffcd, (w + 16) / 2);
    }
    return palette;
}();

inline void Vortex::turn (Ink* dst, int stride, int frame) const
{
    for (int i = 0; i < count; ++i)
    {
        const Dot& d = dots[i];
        dst[d.y * stride + d.x] = spiral[d.ring][(frame + d.phase) & 63];
    }
}

constexpr int inkAt (int x, int y)
{
    if (x < 0 || x >= int (sizeof coverage[0]) - 1 || y < 0 || y >= height) return 0;
    const char value = coverage[y][x];
    return value <= '9' ? value - '0' : value - 'a' + 10;
}

// The reflected horizon flexes with the lettering. Coverage gives each curved rim a directional bevel.
constexpr Ink face (int x, int y)
{
    const int horizon = y * 4 + wave (x / 2) / 2 + wave (x / 4 + 11) / 3;
    if (horizon < 20) return 0xffe5ff42;
    if (horizon < 37) return between (0xffe5ff42, 0xffa9d815, horizon - 20);
    if (horizon < 49) return between (0xffa8e800, 0xfffcffe1, (horizon - 37) * 16 / 12);
    if (horizon < 55) return 0xfff4ffe3;
    if (horizon < 63) return between (0xff3b236f, 0xff235276, (horizon - 55) * 2);
    if (horizon < 80) return between (0xff159d93, 0xff89ffbb, horizon - 63);
    return 0xffd5ff79;
}

constexpr Ink metal (int x, int y)
{
    const Ink shadow = between (background, 0xff6f3bbc, inkAt (x - 1, y - 1));
    Ink fill = face (x, y);
    fill = between (fill, 0xff404472, (16 - inkAt (x + 1, y + 1)) * 7 / 16);
    fill = between (fill, 0xfff3ffd0, (16 - inkAt (x - 1, y - 1)) * 12 / 16);
    return between (shadow, fill, inkAt (x, y));
}

// Lowercase filter at panel resolution. Spaces are transparent, digits give ink coverage in ninths.
constexpr void drawFilterLetters (Plate& plate)
{
    constexpr std::array rows {
        "    145661165  561",
        "   289988 495  99    441",
        "   794    11  397    99       1111",
        "  298111  22  595  26996663 48999993  158888",
        " 2999997 198  793  69999992597322597 1898774",
        " 2895332 396  99    793   2971112595 795",
        "  991    693 397    99    599999996 198",
        " 297     891 596   298    792111    496",
        " 495    198  599663299664 69854444  693",
        " 693    396  169991 58994 16899995  891",
    };
    for (int y = 0; y < int (rows.size()); ++y)
        for (int x = 0; rows[y][x]; ++x)
        {
            const char ink = rows[y][x];
            if (ink == ' ')
                continue;
            const unsigned coverage = ((ink - '0') * 16 + 4) / 9;
            Ink& pixel = plate[(12 + y) * width + 141 + x];
            pixel = between (pixel, 0xffdbfa98, coverage);
        }
}

constexpr Plate blankPlate()
{
    Plate plate {};
    for (auto& pixel : plate)
        pixel = background;
    return plate;
}

inline constexpr Plate fixed = [] {
    Plate layer = blankPlate();
    drawFilterLetters (layer);
    return layer;
}();

inline constexpr Plate letters = [] {
    Plate layer = blankPlate();
    for (int y = 1; y < height; ++y)
        for (int x = 1; x < int (sizeof coverage[0]) - 1; ++x)
            layer[y * width + x] = metal (x, y);
    return layer;
}();

// Bounds include a clear pixel on either side of the displaced ink. Blank ends and the filter script
// never enter the blend loop, including on the long, narrow upper terminal of the X.
struct Span { int begin, end; };
inline constexpr auto spans = [] {
    std::array<Span, height> rows {};
    for (int y = 0; y < height; ++y)
    {
        int first = width, last = -1;
        for (int x = 1; x < width - 1; ++x)
            if (letters[y * width + x] != background)
            {
                if (first == width) first = x;
                last = x;
            }
        if (last >= first) rows[y] = { first - 1, last + 2 };
    }
    return rows;
}();

constexpr int frameAt (uint64_t nowMs) { return (nowMs / 60) % 64; }
constexpr int restFrame = 0; // the frame the title rests on while nothing on the page moves

inline void drawMotion (Ink* dst, int stride, int frame)
{
    for (int y = 1; y < height; ++y)
    {
        const int shift = wave (frame + 5 * y / 2);
        const int step = shift < 0 ? -1 : 1;
        const unsigned weight = shift < 0 ? -shift : shift;
        const Ink* src = letters.data() + y * width;
        Ink* out = dst + y * stride;
        const Span span = spans[y];
        if (weight == 0 || weight == 16)
        {
            const int offset = weight == 16 ? step : 0;
            std::copy_n (src + span.begin + offset, span.end - span.begin, out + span.begin);
        }
        else
        {
            for (int x = span.begin; x < span.end; ++x)
                out[x] = between (src[x], src[x + step], weight);
        }
    }
    vortex.turn (dst, stride, frame);
}
} // namespace cortex::wordmark
