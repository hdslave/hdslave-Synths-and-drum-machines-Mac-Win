// Native-pixel drawing for Reverie's page on Melodee's screen (320 x 218, shown as RGB565): the palette, primitives,
// glass controls, proportional type and a picture that sends only the pixels that changed.
#pragma once

#include "screen_art.h"

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

// Pale enamel, ocean-blue glass, and high-contrast inks for the LCD.
namespace ink
{
constexpr Ink bg = 0xffdbedf0, sheet = 0xffeff8f7, rule = 0xffabc9cd, label = 0xff32566a, text = 0xff0a344c;
constexpr Ink field = 0xff064260, deep = 0xff063451, grid = 0xff215e76, guide = 0xff4789a0;
constexpr Ink white = 0xfff8ffff, ice = 0xffccefff, foam = 0xff65ecff, lime = 0xffc9fb64, gold = 0xffffbf48;
constexpr Ink blue = 0xff0872a5, teal = 0xff00836e;
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
    void gradient (int x, int y, int wide, int high, Ink top, Ink bottom) const
    {
        for (int j = 0; j < high; ++j)
            box (x, y + j, wide, 1, mix (top, bottom, j * 256 / std::max (1, high - 1)));
    }

    // Cut corners, a bright upper reflection and a darker lower lip, all on native pixel boundaries.
    void gel (int x, int y, int wide, int high, Ink rim, Ink top, Ink middle, Ink bottom) const
    {
        for (int j = 0; j < high; ++j)
        {
            const int edge = std::min (j, high - 1 - j);
            const int inset = edge == 0 ? 3 : (edge == 1 ? 1 : 0);
            box (x + inset, y + j, wide - 2 * inset, 1, rim);
            if (! edge) continue;
            const int split = high * 2 / 5;
            Ink c = j <= split ? mix (top, middle, (j - 1) * 160 / std::max (1, split - 1))
                               : mix (middle, bottom, (j - split) * 256 / (high - split));
            box (x + inset + 1, y + j, wide - 2 * inset - 2, 1, c);
        }
        box (x + 4, y + 1, wide - 8, 1, mix (top, ink::white, 140));
    }

    void bitmap (int x, int y, int wide, int high, const uint16_t* src) const
    {
        for (int j = 0; j < high; ++j)
            for (int i = 0; i < wide; ++i)
            {
                const int v = src[j * wide + i];
                const unsigned r = v >> 11, g = (v >> 5) & 63, b = v & 31;
                dot (x + i, y + j, 0xff000000u | ((r << 3 | r >> 2) << 16) | ((g << 2 | g >> 4) << 8) | (b << 3 | b >> 2));
            }
    }

    static int measure (const char* s, int size)
    {
        const auto* widths = size == 12 ? art::width12 : art::width10;
        int total = 0;
        for (; *s; ++s) total += widths[glyph (*s)];
        return total;
    }

    int text (int x, int y, const char* s, Ink color, int size = 12) const
    {
        const auto* widths = size == 12 ? art::width12 : art::width10;
        const auto* offsets = size == 12 ? art::offset12 : art::offset10;
        const auto* mask = size == 12 ? art::mask12 : art::mask10;
        for (; *s; ++s)
        {
            const unsigned ch = glyph (*s);
            const int width = widths[ch];
            const auto* row = mask + offsets[ch];
            for (int j = 0; j < size + 2; ++j)
                for (int i = 0; i < width; ++i)
                {
                    const int a = *row++;
                    if (a == 255) dot (x + i, y + j, color);
                    else if (a) blend (x + i, y + j, color, a);
                }
            x += width;
        }
        return x;
    }
    void right (int end, int y, const char* s, Ink color, int size = 12) const
    {
        text (end - measure (s, size), y, s, color, size);
    }
    void centre (int mid, int y, const char* s, Ink color, int size = 12) const
    {
        text (mid - measure (s, size) / 2, y, s, color, size);
    }
private:
    static unsigned glyph (char c)
    {
        return c >= 32 && c <= 126 ? (unsigned) (c - 32) : (unsigned) ('?' - 32);
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
