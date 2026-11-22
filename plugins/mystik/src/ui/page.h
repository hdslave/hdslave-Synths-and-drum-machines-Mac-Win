// Native-pixel drawing for Mystik's page on Melodee's screen (320 x 218, shown as RGB565): the palette, primitives,
// text, and a picture that sends only the pixels that changed.
#pragma once

#include "font.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace hdui
{
constexpr int width = 320, height = 218;
constexpr uint32_t noParam = 0xffffffffu;
using Ink = uint32_t;

// Black vinyl, cold chrome, enamel pink, and the vactrol’s acid light.
namespace ink
{
constexpr Ink bg = 0xff07070e, field = 0xff10101c, card = 0xff1f1d30;
constexpr Ink rule = 0xff58516f, grid = 0xff393347, faint = 0xff201d31;
constexpr Ink muted = 0xffbeb7ce, soft = 0xffd2ceee, paper = 0xfff4f6ff, parked = 0xff918aab;
constexpr Ink acid = 0xffc9ff14, hot = 0xffedffb0, lime = 0xff80aa20;
constexpr Ink cyan = 0xff71eafe, pink = 0xffff50ac, violet = 0xffad86ff;
}

inline Ink mix (Ink a, Ink b, int t256) // a toward b by t/256
{
    auto ch = [&] (int s) {
        const int x = (int) ((a >> s) & 255), y = (int) ((b >> s) & 255);
        return (uint32_t) (x + (((y - x) * t256) >> 8)) << s;
    };
    return 0xff000000u | ch (16) | ch (8) | ch (0);
}

struct Host
{
    uint64_t nowMs = 0;
    Ink accent = 0;
    int16_t pot[3] = { -1, -1, -1 };
    bool engaged[3] = { true, true, true };
    bool shift = false, playing = false;
};

enum class Key : uint8_t { wheel = 0, push = 1, nudge = 2 }; // as MELODEE_SCREEN_WHEEL, _PUSH, _NUDGE

// What an input did: parameters set (plain values) and buttons pressed.
struct Edits
{
    struct Edit { uint32_t id; float value; bool press; };
    Edit list[4];
    int count = 0;
    void set (uint32_t id, float v) { if (count < 4) list[count++] = { id, v, false }; }
    void press (uint32_t id) { if (count < 4) list[count++] = { id, 0.0f, true }; }
};

struct Raster
{
    Ink* px;
    int w, h;

    void box (int x, int y, int wide, int high, Ink c) const
    {
        const int left = std::max (0, x), right = std::min (w, x + wide);
        if (right <= left) return;
        for (int j = std::max (0, y); j < std::min (h, y + high); ++j)
            std::fill (px + j * w + left, px + j * w + right, c);
    }
    void dot (int x, int y, Ink c) const
    {
        if (x >= 0 && x < w && y >= 0 && y < h)
            px[y * w + x] = c;
    }
    void blend (int x, int y, Ink c, int t256) const
    {
        if (x >= 0 && x < w && y >= 0 && y < h)
            px[y * w + x] = mix (px[y * w + x], c, t256);
    }
    void frame (int x, int y, int wide, int high, Ink c) const
    {
        box (x, y, wide, 1, c);
        box (x, y + high - 1, wide, 1, c);
        box (x, y, 1, high, c);
        box (x + wide - 1, y, 1, high, c);
    }
    void line (int x, int y, int endX, int endY, Ink c) const
    {
        const int dx = std::abs (endX - x), dy = -std::abs (endY - y);
        const int sx = x < endX ? 1 : -1, sy = y < endY ? 1 : -1;
        int error = dx + dy;
        for (;;)
        {
            dot (x, y, c);
            if (x == endX && y == endY) return;
            const int twice = 2 * error;
            if (twice >= dy) { error += dy; x += sx; }
            if (twice <= dx) { error += dx; y += sy; }
        }
    }
    // A dotted line: every `every`th pixel.
    void dotted (int x, int y, int endX, int endY, Ink c, int every) const
    {
        const int dx = std::abs (endX - x), dy = -std::abs (endY - y);
        const int sx = x < endX ? 1 : -1, sy = y < endY ? 1 : -1;
        int error = dx + dy, n = 0;
        for (;;)
        {
            if (n++ % every == 0) dot (x, y, c);
            if (x == endX && y == endY) return;
            const int twice = 2 * error;
            if (twice >= dy) { error += dy; x += sx; }
            if (twice <= dx) { error += dx; y += sy; }
        }
    }
    // A filled disc and a ring (radius r, integer centre).
    void disc (int cx, int cy, int r, Ink c) const
    {
        for (int j = -r; j <= r; ++j)
        {
            const int half = (int) std::sqrt ((float) (r * r - j * j) + 0.25f);
            box (cx - half, cy + j, 2 * half + 1, 1, c);
        }
    }
    void ring (int cx, int cy, int r, Ink c) const
    {
        int x = r, y = 0, err = 1 - r;
        while (x >= y)
        {
            dot (cx + x, cy + y, c), dot (cx - x, cy + y, c), dot (cx + x, cy - y, c), dot (cx - x, cy - y, c);
            dot (cx + y, cy + x, c), dot (cx - y, cy + x, c), dot (cx + y, cy - x, c), dot (cx - y, cy - x, c);
            ++y;
            if (err < 0) err += 2 * y + 1;
            else --x, err += 2 * (y - x) + 1;
        }
    }
    int text (int x, int y, const char* s, Ink c, int advance = font::glyphWidth) const
    {
        for (; *s; ++s, x += advance)
        {
            const unsigned ch = static_cast<unsigned char> (*s);
            const auto& glyph = font::glyphs[(ch >= 32 && ch <= 127 ? ch : '?') - 32];
            for (int j = 0; j < font::glyphHeight; ++j)
                for (int bits = glyph[j], k = 0; bits; bits >>= 1, ++k)
                    if (bits & 1) dot (x + k, y + j, c);
        }
        return x;
    }
    // A strip with a sloped right edge, like the cut on the wordmark.
    void tab (int x, int y, int wide, int high, Ink c) const
    {
        for (int j = 0; j < high; ++j)
            box (x, y + j, wide - j / 2, 1, c);
    }
    void right (int end, int y, const char* s, Ink c, int advance = font::glyphWidth) const
    {
        text (end - advance * (int) std::strlen (s), y, s, c, advance);
    }
    void centre (int mid, int y, const char* s, Ink c, int advance = font::glyphWidth) const
    {
        text (mid - advance * (int) std::strlen (s) / 2, y, s, c, advance);
    }
};

// Keeps the last pixels the plug-in drew (Melodee draws its own overlays on the canvas), so each region is drawn into
// scratch, compared row by row, and only the changed span of a changed row is copied out.
class Picture
{
public:
    template <int W, int H, typename Draw>
    bool region (Ink* dest, int stride, bool whole, int x, int y, Draw draw)
    {
        static_assert (W * H <= capacity, "a region fits the scratch");
        Raster r { scratch, W, H };
        std::fill_n (scratch, W * H, ink::bg);
        draw (r);
        bool touched = false;
        for (int j = 0; j < H; ++j)
        {
            Ink* old = prior + (y + j) * width + x;
            const Ink* row = scratch + j * W;
            if (! whole && std::memcmp (old, row, W * sizeof (Ink)) == 0)
                continue;
            int left = 0, right = W;
            if (! whole)
            {
                while (old[left] == row[left]) ++left;
                while (old[right - 1] == row[right - 1]) --right;
            }
            std::memcpy (old + left, row + left, (size_t) (right - left) * sizeof (Ink));
            std::memcpy (dest + (y + j) * stride + x + left, row + left, (size_t) (right - left) * sizeof (Ink));
            touched = true;
        }
        return touched;
    }

private:
    static constexpr int capacity = 320 * 142;
    Ink prior[width * height] {}, scratch[capacity] {};
};
} // namespace hdui
