// A small software renderer for the desktop panel. Everything is drawn in logical units multiplied by an integer
// device scale (1 on a standard display, 2 on Retina / 200% Windows), so the pixel font and the live MPC page stay
// crisp at every size. Pixels are 0xAARRGGBB, which is BGRA in memory: what CoreGraphics (32Little, skip-first) and
// a Win32 top-down DIB both take as is.
#pragma once

#include "pixel_font.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>

namespace hdslave_desktop
{

struct Rect
{
    int x = 0, y = 0, w = 0, h = 0;
    bool contains (int px, int py) const { return px >= x && py >= y && px < x + w && py < y + h; }
    bool empty() const { return w <= 0 || h <= 0; }
    Rect unite (const Rect& o) const
    {
        if (empty()) return o;
        if (o.empty()) return *this;
        const int x0 = std::min (x, o.x), y0 = std::min (y, o.y);
        const int x1 = std::max (x + w, o.x + o.w), y1 = std::max (y + h, o.y + o.h);
        return { x0, y0, x1 - x0, y1 - y0 };
    }
    Rect intersect (const Rect& o) const
    {
        const int x0 = std::max (x, o.x), y0 = std::max (y, o.y);
        const int x1 = std::min (x + w, o.x + o.w), y1 = std::min (y + h, o.y + o.h);
        if (x1 <= x0 || y1 <= y0) return {};
        return { x0, y0, x1 - x0, y1 - y0 };
    }
    Rect inset (int d) const { return { x + d, y + d, w - 2 * d, h - 2 * d }; }
};

inline uint32_t mix (uint32_t a, uint32_t b, float t)
{
    t = std::clamp (t, 0.0f, 1.0f);
    auto ch = [&] (int s) {
        const float ca = float ((a >> s) & 0xFFu), cb = float ((b >> s) & 0xFFu);
        return uint32_t (ca + (cb - ca) * t + 0.5f) << s;
    };
    return 0xFF000000u | ch (16) | ch (8) | ch (0);
}

class Canvas
{
public:
    uint32_t* px = nullptr; // physical pixels
    int pw = 0, ph = 0;     // physical size
    int s = 1;              // device scale
    Rect clip {};           // physical clip

    void begin (uint32_t* pixels, int physW, int physH, int scale)
    {
        px = pixels;
        pw = physW;
        ph = physH;
        s = std::max (1, scale);
        clip = { 0, 0, pw, ph };
    }

    // Clip to a logical rectangle (intersected with the whole canvas).
    void setClip (const Rect& logical) { clip = Rect { logical.x * s, logical.y * s, logical.w * s, logical.h * s }.intersect ({ 0, 0, pw, ph }); }
    void resetClip() { clip = { 0, 0, pw, ph }; }

    static uint32_t blend (uint32_t dst, uint32_t src, uint32_t a)
    {
        if (a >= 255) return src | 0xFF000000u;
        if (a == 0) return dst;
        const uint32_t ia = 255 - a;
        const uint32_t rb = ((((src & 0xFF00FFu) * a) + ((dst & 0xFF00FFu) * ia)) >> 8) & 0xFF00FFu;
        const uint32_t g = ((((src & 0x00FF00u) * a) + ((dst & 0x00FF00u) * ia)) >> 8) & 0x00FF00u;
        return 0xFF000000u | rb | g;
    }

    inline void plot (int x, int y, uint32_t c, uint32_t a = 255)
    {
        if (x < clip.x || y < clip.y || x >= clip.x + clip.w || y >= clip.y + clip.h) return;
        uint32_t& d = px[(size_t) y * (size_t) pw + (size_t) x];
        d = blend (d, c, a);
    }

    // Physical rectangle fill.
    void fillPhys (int x0, int y0, int x1, int y1, uint32_t c, uint32_t a = 255)
    {
        x0 = std::max (x0, clip.x);
        y0 = std::max (y0, clip.y);
        x1 = std::min (x1, clip.x + clip.w);
        y1 = std::min (y1, clip.y + clip.h);
        if (x0 >= x1 || y0 >= y1) return;
        for (int y = y0; y < y1; ++y)
        {
            uint32_t* row = px + (size_t) y * (size_t) pw;
            if (a >= 255)
                std::fill (row + x0, row + x1, c | 0xFF000000u);
            else
                for (int x = x0; x < x1; ++x) row[x] = blend (row[x], c, a);
        }
    }

    void fill (int x, int y, int w, int h, uint32_t c, uint32_t a = 255)
    {
        if (w <= 0 || h <= 0) return;
        fillPhys (x * s, y * s, (x + w) * s, (y + h) * s, c, a);
    }
    void fill (const Rect& r, uint32_t c, uint32_t a = 255) { fill (r.x, r.y, r.w, r.h, c, a); }

    // A 1-logical-pixel outline.
    void frame (const Rect& r, uint32_t c, uint32_t a = 255)
    {
        fill (r.x, r.y, r.w, 1, c, a);
        fill (r.x, r.y + r.h - 1, r.w, 1, c, a);
        fill (r.x, r.y + 1, 1, r.h - 2, c, a);
        fill (r.x + r.w - 1, r.y + 1, 1, r.h - 2, c, a);
    }

    // Hairline: one physical pixel, for fine Retina detail.
    void hline (int x, int y, int w, uint32_t c, uint32_t a = 255) { fillPhys (x * s, y * s, (x + w) * s, y * s + 1, c, a); }

    // Rectangle with its corners cut off at 45 degrees, the pages' card shape.
    void fillChamfer (const Rect& r, int cut, uint32_t c, uint32_t a = 255)
    {
        const int ps = s;
        const int x0 = r.x * ps, y0 = r.y * ps, x1 = (r.x + r.w) * ps, y1 = (r.y + r.h) * ps;
        const int k = cut * ps;
        for (int y = y0; y < y1; ++y)
        {
            int inset = 0;
            if (y - y0 < k) inset = k - (y - y0);
            else if (y1 - 1 - y < k) inset = k - (y1 - 1 - y);
            fillPhys (x0 + inset, y, x1 - inset, y + 1, c, a);
        }
    }

    void frameChamfer (const Rect& r, int cut, uint32_t c, uint32_t a = 255)
    {
        const int ps = s;
        const int x0 = r.x * ps, y0 = r.y * ps, x1 = (r.x + r.w) * ps, y1 = (r.y + r.h) * ps;
        const int k = cut * ps;
        fillPhys (x0 + k, y0, x1 - k, y0 + ps, c, a);
        fillPhys (x0 + k, y1 - ps, x1 - k, y1, c, a);
        fillPhys (x0, y0 + k, x0 + ps, y1 - k, c, a);
        fillPhys (x1 - ps, y0 + k, x1, y1 - k, c, a);
        for (int i = 0; i < k; ++i)
        {
            fillPhys (x0 + k - 1 - i, y0 + i, x0 + k - i + ps - 1, y0 + i + 1, c, a);
            fillPhys (x1 - k + i - ps + 1, y0 + i, x1 - k + i + 1, y0 + i + 1, c, a);
            fillPhys (x0 + k - 1 - i, y1 - 1 - i, x0 + k - i + ps - 1, y1 - i, c, a);
            fillPhys (x1 - k + i - ps + 1, y1 - 1 - i, x1 - k + i + 1, y1 - i, c, a);
        }
    }

    // Vertical gradient with a 4x4 ordered dither, like the pages' grounds.
    void gradient (const Rect& r, uint32_t top, uint32_t bottom)
    {
        static const int bayer[4][4] = { { 0, 8, 2, 10 }, { 12, 4, 14, 6 }, { 3, 11, 1, 9 }, { 15, 7, 13, 5 } };
        const int x0 = std::max (r.x * s, clip.x), x1 = std::min ((r.x + r.w) * s, clip.x + clip.w);
        const int y0 = std::max (r.y * s, clip.y), y1 = std::min ((r.y + r.h) * s, clip.y + clip.h);
        const float span = float (std::max (1, r.h * s - 1));
        for (int y = y0; y < y1; ++y)
        {
            const float t = float (y - r.y * s) / span;
            uint32_t* row = px + (size_t) y * (size_t) pw;
            const uint32_t c0 = mix (top, bottom, t), c1 = mix (top, bottom, t + 1.0f / 48.0f);
            for (int x = x0; x < x1; ++x)
                row[x] = (bayer[y & 3][x & 3] < 8) ? c0 : c1;
        }
    }

    // Anti-aliased line in logical units (thickness in logical units).
    void line (float ax, float ay, float bx, float by, float thick, uint32_t c, uint32_t alpha = 255)
    {
        ax *= s; ay *= s; bx *= s; by *= s;
        const float half = thick * s * 0.5f;
        const float dx = bx - ax, dy = by - ay;
        const float len2 = std::max (1e-6f, dx * dx + dy * dy);
        const int x0 = std::max (clip.x, (int) std::floor (std::min (ax, bx) - half - 1));
        const int x1 = std::min (clip.x + clip.w - 1, (int) std::ceil (std::max (ax, bx) + half + 1));
        const int y0 = std::max (clip.y, (int) std::floor (std::min (ay, by) - half - 1));
        const int y1 = std::min (clip.y + clip.h - 1, (int) std::ceil (std::max (ay, by) + half + 1));
        for (int y = y0; y <= y1; ++y)
            for (int x = x0; x <= x1; ++x)
            {
                const float fx = x + 0.5f, fy = y + 0.5f;
                const float t = std::clamp (((fx - ax) * dx + (fy - ay) * dy) / len2, 0.0f, 1.0f);
                const float ex = ax + t * dx - fx, ey = ay + t * dy - fy;
                const float d = std::sqrt (ex * ex + ey * ey);
                const float cov = std::clamp (half + 0.5f - d, 0.0f, 1.0f);
                if (cov > 0.0f) plot (x, y, c, (uint32_t) (cov * alpha));
            }
    }

    // Anti-aliased filled disc / ring in logical units.
    void disc (float cx, float cy, float r, uint32_t c, uint32_t alpha = 255)
    {
        ring (cx, cy, 0.0f, r, c, alpha);
    }
    void ring (float cx, float cy, float rIn, float rOut, uint32_t c, uint32_t alpha = 255)
    {
        cx *= s; cy *= s; rIn *= s; rOut *= s;
        const int x0 = std::max (clip.x, (int) std::floor (cx - rOut - 1)), x1 = std::min (clip.x + clip.w - 1, (int) std::ceil (cx + rOut + 1));
        const int y0 = std::max (clip.y, (int) std::floor (cy - rOut - 1)), y1 = std::min (clip.y + clip.h - 1, (int) std::ceil (cy + rOut + 1));
        for (int y = y0; y <= y1; ++y)
            for (int x = x0; x <= x1; ++x)
            {
                const float dx = x + 0.5f - cx, dy = y + 0.5f - cy;
                const float d = std::sqrt (dx * dx + dy * dy);
                float cov = std::clamp (rOut + 0.5f - d, 0.0f, 1.0f);
                if (rIn > 0.0f) cov = std::min (cov, std::clamp (d - rIn + 0.5f, 0.0f, 1.0f));
                if (cov > 0.0f) plot (x, y, c, (uint32_t) (cov * alpha));
            }
    }

    // ---- Departure Mono text. size multiplies the 7 x 12 glyph cell (in logical pixels). ----
    static constexpr int glyphW = pixel_font::glyphWidth;
    static constexpr int glyphH = pixel_font::glyphHeight;
    static int textWidth (const std::string& t, int size = 1) { return (int) t.size() * glyphW * size; }

    void text (int x, int y, const std::string& t, uint32_t c, int size = 1, int maxW = 0)
    {
        const int cell = size * s; // physical pixels per font pixel
        int cx = x;
        for (char ch : t)
        {
            if (maxW > 0 && cx + glyphW * size > x + maxW) break;
            int code = (unsigned char) ch;
            if (code < 32 || code > 127) code = '?';
            const uint8_t* g = pixel_font::glyphs[code - 32];
            for (int row = 0; row < glyphH; ++row)
            {
                const uint8_t bits = g[row];
                if (bits == 0) continue;
                for (int col = 0; col < glyphW; ++col)
                    if (bits & (1u << col))
                        fillPhys ((cx + col * size) * s, (y + row * size) * s, (cx + col * size) * s + cell, (y + row * size) * s + cell, c);
            }
            cx += glyphW * size;
        }
    }

    // Fit text into maxW by trimming with a trailing '.', and draw it aligned (0 left, 1 centre, 2 right).
    static std::string fit (const std::string& t, int maxW, int size = 1)
    {
        if (t.empty()) return {};
        const int maxChars = std::max (1, maxW / (glyphW * size));
        if ((int) t.size() <= maxChars) return t;
        if (maxChars <= 1) return t.substr (0, 1);
        return t.substr (0, (size_t) maxChars - 1) + ".";
    }
    void textAligned (const Rect& r, const std::string& t, uint32_t c, int size, int align)
    {
        const std::string f = fit (t, r.w, size);
        const int w = textWidth (f, size);
        const int x = align == 0 ? r.x : align == 1 ? r.x + (r.w - w) / 2 : r.x + r.w - w;
        const int y = r.y + (r.h - glyphH * size) / 2;
        text (x, y, f, c, size);
    }

    // The pages' tag: text on a filled accent block (K1, PLAY, SCOPE, ...).
    void tag (int x, int y, const std::string& t, uint32_t bg, uint32_t fg, int padX = 3)
    {
        const int w = textWidth (t) + 2 * padX;
        fill (x, y, w, glyphH + 2, bg);
        text (x + padX, y + 1, t, fg);
    }

    // Integer upscale blit of an opaque ARGB image (the live page) at logical (x, y) with `zoom` logical pixels per
    // source pixel.
    void blit (const uint32_t* src, int sw, int sh, int x, int y, int zoom)
    {
        const int k = zoom * s;
        const int dx0 = x * s, dy0 = y * s;
        const int cx0 = std::max (clip.x, dx0), cx1 = std::min (clip.x + clip.w, dx0 + sw * k);
        const int cy0 = std::max (clip.y, dy0), cy1 = std::min (clip.y + clip.h, dy0 + sh * k);
        for (int py = cy0; py < cy1; ++py)
        {
            const uint32_t* srow = src + (size_t) ((py - dy0) / k) * (size_t) sw;
            uint32_t* drow = px + (size_t) py * (size_t) pw;
            for (int pxl = cx0; pxl < cx1; ++pxl)
                drow[pxl] = srow[(pxl - dx0) / k] | 0xFF000000u;
        }
    }

    // LED-ring knob: 25 segments over 270 degrees around a dark cap with a pointer. norm 0..1; bipolar lights from
    // the top. hot: hovered or dragged.
    void knob (float cx, float cy, float r, float norm, bool bipolar, uint32_t lit, uint32_t unlit, uint32_t cap,
               uint32_t capEdge, bool hot)
    {
        constexpr float pi = 3.14159265f;
        constexpr int segs = 25;
        const float start = 0.75f * pi, sweep = 1.5f * pi;
        norm = std::clamp (norm, 0.0f, 1.0f);
        const float lo = bipolar ? std::min (0.5f, norm) : 0.0f;
        const float hi = bipolar ? std::max (0.5f, norm) : norm;
        for (int i = 0; i < segs; ++i)
        {
            const float t = float (i) / float (segs - 1);
            const float a = start + t * sweep;
            const float ca = std::cos (a), sa = std::sin (a);
            const bool on = (t >= lo - 0.02f && t <= hi + 0.02f) && !(bipolar && std::fabs (norm - 0.5f) < 0.01f && std::fabs (t - 0.5f) > 0.03f);
            line (cx + ca * (r - 4.5f), cy + sa * (r - 4.5f), cx + ca * r, cy + sa * r, 1.6f, on ? lit : unlit);
        }
        disc (cx, cy, r - 7.0f, hot ? capEdge : mix (capEdge, cap, 0.5f));
        disc (cx, cy, r - 8.5f, cap);
        const float a = start + norm * sweep;
        line (cx + std::cos (a) * (r - 15.0f) * 0.25f, cy + std::sin (a) * (r - 15.0f) * 0.25f,
              cx + std::cos (a) * (r - 10.5f), cy + std::sin (a) * (r - 10.5f), 2.0f, hot ? 0xFFFFFFFFu : lit);
    }
};

} // namespace hdslave_desktop
