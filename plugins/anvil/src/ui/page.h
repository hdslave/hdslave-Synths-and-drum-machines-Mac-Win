// Native-pixel drawing and Melodee controls for the instrument page.
#pragma once

#include "font.h"
#include "identity.h"

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
namespace ink
{
constexpr Ink bg = 0xff0c0710, field = 0xff16101e, card = 0xff201828, selected = 0xff36273f;
constexpr Ink rule = 0xff5c4269, grid = 0xff39273f, muted = 0xffb4a2be, soft = 0xffd5b5ee;
constexpr Ink paper = 0xfff5f2e8, live = 0xffc6f62c, cursor = 0xff3f4721, parked = 0xff87738d;
}

struct Field { uint32_t id; const char* label; };
struct Group { const char* name; const Field* fields; int count; };

struct Host
{
    uint64_t nowMs = 0;
    Ink accent = 0;     // the track's colour (0: none given)
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
        std::fill_n (scratch, W * H, ink::bg);
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
    static constexpr int capacity = 320 * 84;
    Ink prior[width * height], scratch[capacity];
};
} // namespace hdui
