// Native-pixel drawing and Melodee controls for the instrument page.
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
using Ink = uint32_t;
constexpr Ink background = 0xff14111d;
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

    void line (int x, int y, int endX, int endY, Ink colour) const
    {
        const int dx = std::abs (endX - x), dy = -std::abs (endY - y);
        const int sx = x < endX ? 1 : -1, sy = y < endY ? 1 : -1;
        int error = dx + dy;
        for (;;)
        {
            dot (x, y, colour);
            if (x == endX && y == endY) return;
            const int twice = 2 * error;
            if (twice >= dy) { error += dy; x += sx; }
            if (twice <= dx) { error += dx; y += sy; }
        }
    }

    void frame (int x, int y, int wide, int high, Ink c) const
    {
        box (x, y, wide, 1, c);
        box (x, y + high - 1, wide, 1, c);
        box (x, y, 1, high, c);
        box (x + wide - 1, y, 1, high, c);
    }

    int text (int x, int y, const char* s, Ink color, int advance = font::glyphWidth) const
    {
        for (; *s; ++s, x += advance)
        {
            const unsigned ch = static_cast<unsigned char> (*s);
            const auto& glyph = font::glyphs[(ch >= 32 && ch <= 127 ? ch : '?') - 32];
            for (int j = 0; j < font::glyphHeight; ++j)
                for (int bits = glyph[j], k = 0; bits; bits >>= 1, ++k)
                    if (bits & 1) dot (x + k, y + j, color);
        }
        return x;
    }

    void right (int end, int y, const char* s, Ink c) const
    {
        text (end - font::glyphWidth * int (std::strlen (s)), y, s, c);
    }
};

// Retain the last plug-in pixels independently of any host overlays. Compare only
// invalidated regions. Scratch space is shared by all regions and bounded at compile time.
class Picture
{
public:
    template<int W, int H, typename Draw>
    bool region (Ink* dest, int stride, bool whole, int x, int y, Draw draw)
    {
        static_assert (W * H <= capacity);
        Raster r { scratch, W, H };
        std::fill_n (scratch, W * H, background);
        draw (r);
        bool touched = false;
        for (int j = 0; j < H; ++j)
        {
            auto* old = prior + (y + j) * width + x;
            const auto* row = scratch + j * W;
            if (! whole && std::memcmp (old, row, W * sizeof (Ink)) == 0) continue;
            int left = 0, right = W;
            if (! whole)
            {
                while (old[left] == row[left]) ++left;
                while (old[right - 1] == row[right - 1]) --right;
            }
            std::copy (row + left, row + right, old + left);
            std::copy (row + left, row + right, dest + (y + j) * stride + x + left);
            touched = true;
        }
        return touched;
    }

private:
    static constexpr int capacity = 164 * 122;
    Ink prior[width * height], scratch[capacity];
};
} // namespace hdui
