#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>

namespace hdslave_desktop
{

struct Canvas
{
    uint32_t* pixels = nullptr; // 0xAARRGGBB (little-endian BGRA bytes — matches macOS kCGBitmapByteOrder32Little & Win32 DIB)
    int width = 0;
    int height = 0;

    static inline uint32_t blend (uint32_t dst, uint32_t src, uint8_t alpha)
    {
        if (alpha == 0) return dst;
        if (alpha == 255) return src | 0xFF000000u;
        const uint32_t inv = 255u - alpha;
        const uint32_t r = (((src >> 16) & 0xFFu) * alpha + ((dst >> 16) & 0xFFu) * inv) / 255u;
        const uint32_t g = (((src >> 8) & 0xFFu) * alpha + ((dst >> 8) & 0xFFu) * inv) / 255u;
        const uint32_t b = ((src & 0xFFu) * alpha + (dst & 0xFFu) * inv) / 255u;
        return 0xFF000000u | (r << 16) | (g << 8) | b;
    }

    static inline uint32_t brighten (uint32_t c, int delta)
    {
        int r = std::clamp (int ((c >> 16) & 0xFFu) + delta, 0, 255);
        int g = std::clamp (int ((c >> 8) & 0xFFu) + delta, 0, 255);
        int b = std::clamp (int (c & 0xFFu) + delta, 0, 255);
        return 0xFF000000u | (uint32_t (r) << 16) | (uint32_t (g) << 8) | uint32_t (b);
    }

    inline void clear (uint32_t color)
    {
        if (!pixels) return;
        const int n = width * height;
        for (int i = 0; i < n; ++i) pixels[i] = color;
    }

    inline void putPixel (int x, int y, uint32_t color, uint8_t alpha = 255)
    {
        if (x < 0 || y < 0 || x >= width || y >= height) return;
        uint32_t& dst = pixels[y * width + x];
        dst = (alpha == 255) ? (color | 0xFF000000u) : blend (dst, color, alpha);
    }

    inline void fillRect (int x, int y, int w, int h, uint32_t color, uint8_t alpha = 255)
    {
        if (!pixels || w <= 0 || h <= 0) return;
        int x0 = std::max (0, x);
        int y0 = std::max (0, y);
        int x1 = std::min (width, x + w);
        int y1 = std::min (height, y + h);
        if (x0 >= x1 || y0 >= y1) return;
        for (int py = y0; py < y1; ++py)
        {
            uint32_t* row = pixels + py * width;
            if (alpha == 255)
            {
                const uint32_t c = color | 0xFF000000u;
                for (int px = x0; px < x1; ++px) row[px] = c;
            }
            else
            {
                for (int px = x0; px < x1; ++px) row[px] = blend (row[px], color, alpha);
            }
        }
    }

    inline void drawRect (int x, int y, int w, int h, uint32_t color, uint8_t alpha = 255)
    {
        fillRect (x, y, w, 1, color, alpha);
        fillRect (x, y + h - 1, w, 1, color, alpha);
        fillRect (x, y, 1, h, color, alpha);
        fillRect (x + w - 1, y, 1, h, color, alpha);
    }

    inline void fillRoundedRect (int x, int y, int w, int h, int r, uint32_t color, uint8_t alpha = 255)
    {
        if (!pixels || w <= 0 || h <= 0) return;
        r = std::min (r, std::min (w / 2, h / 2));
        if (r <= 1)
        {
            fillRect (x, y, w, h, color, alpha);
            return;
        }
        int x0 = std::max (0, x);
        int y0 = std::max (0, y);
        int x1 = std::min (width, x + w);
        int y1 = std::min (height, y + h);
        const float rf = float (r);
        for (int py = y0; py < y1; ++py)
        {
            for (int px = x0; px < x1; ++px)
            {
                float dx = 0.0f, dy = 0.0f;
                if (px < x + r) dx = float (x + r - px) - 0.5f;
                else if (px >= x + w - r) dx = float (px - (x + w - r)) + 0.5f;
                if (py < y + r) dy = float (y + r - py) - 0.5f;
                else if (py >= y + h - r) dy = float (py - (y + h - r)) + 0.5f;

                if (dx > 0.0f && dy > 0.0f)
                {
                    float dist = std::sqrt (dx * dx + dy * dy);
                    if (dist > rf + 0.5f) continue;
                    float cov = std::clamp (rf + 0.5f - dist, 0.0f, 1.0f);
                    putPixel (px, py, color, uint8_t (cov * alpha));
                }
                else
                {
                    putPixel (px, py, color, alpha);
                }
            }
        }
    }

    inline void drawRoundedRect (int x, int y, int w, int h, int r, uint32_t color, uint8_t alpha = 255)
    {
        if (!pixels || w <= 0 || h <= 0) return;
        r = std::min (r, std::min (w / 2, h / 2));
        if (r <= 1)
        {
            drawRect (x, y, w, h, color, alpha);
            return;
        }
        fillRect (x + r, y, w - 2 * r, 1, color, alpha);
        fillRect (x + r, y + h - 1, w - 2 * r, 1, color, alpha);
        fillRect (x, y + r, 1, h - 2 * r, color, alpha);
        fillRect (x + w - 1, y + r, 1, h - 2 * r, color, alpha);

        // Corner arcs
        const int corners[4][2] = {
            { x + r, y + r },
            { x + w - r - 1, y + r },
            { x + r, y + h - r - 1 },
            { x + w - r - 1, y + h - r - 1 },
        };
        const float rf = float (r);
        for (int c = 0; c < 4; ++c)
        {
            int cx = corners[c][0];
            int cy = corners[c][1];
            int sx = (c % 2 == 0) ? -1 : 1;
            int sy = (c < 2) ? -1 : 1;
            for (int dy = 0; dy <= r; ++dy)
            {
                for (int dx = 0; dx <= r; ++dx)
                {
                    float d = std::sqrt (float (dx * dx + dy * dy));
                    float diff = std::fabs (d - (rf - 0.5f));
                    if (diff < 1.0f)
                    {
                        uint8_t a = uint8_t ((1.0f - diff) * alpha);
                        putPixel (cx + sx * dx, cy + sy * dy, color, a);
                    }
                }
            }
        }
    }

    inline void drawLine (float x0, float y0, float x1, float y1, float thickness, uint32_t color)
    {
        float dx = x1 - x0;
        float dy = y1 - y0;
        float len = std::sqrt (dx * dx + dy * dy);
        if (len < 0.001f) return;
        int minX = std::max (0, int (std::floor (std::min (x0, x1) - thickness - 1.0f)));
        int maxX = std::min (width - 1, int (std::ceil (std::max (x0, x1) + thickness + 1.0f)));
        int minY = std::max (0, int (std::floor (std::min (y0, y1) - thickness - 1.0f)));
        int maxY = std::min (height - 1, int (std::ceil (std::max (y0, y1) + thickness + 1.0f)));
        const float halfT = thickness * 0.5f;
        const float invLenSq = 1.0f / (len * len);

        for (int py = minY; py <= maxY; ++py)
        {
            for (int px = minX; px <= maxX; ++px)
            {
                float t = ((float (px) - x0) * dx + (float (py) - y0) * dy) * invLenSq;
                t = std::clamp (t, 0.0f, 1.0f);
                float cx = x0 + t * dx;
                float cy = y0 + t * dy;
                float d = std::sqrt ((float (px) - cx) * (float (px) - cx) + (float (py) - cy) * (float (py) - cy));
                if (d < halfT + 0.8f)
                {
                    float cov = std::clamp (halfT + 0.8f - d, 0.0f, 1.0f);
                    putPixel (px, py, color, uint8_t (cov * 255.0f));
                }
            }
        }
    }

    // Crisp anti-aliased rotary hardware knob
    inline void drawKnob (int cx, int cy, int radius, float norm, uint32_t accent, uint32_t trackCol,
                          bool hovered, bool active, bool learnTarget)
    {
        norm = std::clamp (norm, 0.0f, 1.0f);
        constexpr float kPi = 3.14159265358979323846f;
        constexpr float kStartAngle = 135.0f * kPi / 180.0f; // 7:30 o'clock (in screen coords where +Y is down)
        constexpr float kSweep = 270.0f * kPi / 180.0f;      // clockwise to 4:30 o'clock

        const float outerR = float (radius);
        const float arcOuter = outerR;
        const float arcInner = outerR - 3.6f;
        const float capR = outerR - 5.2f;

        const int minX = std::max (0, cx - radius - 2);
        const int maxX = std::min (width - 1, cx + radius + 2);
        const int minY = std::max (0, cy - radius - 2);
        const int maxY = std::min (height - 1, cy + radius + 2);

        const uint32_t capCol = active ? 0xFF323949u : (hovered ? 0xFF2B3140u : 0xFF232836u);
        const uint32_t capRim = learnTarget ? 0xFFFF3B5Cu : (active ? accent : (hovered ? 0xFF566078u : 0xFF3A4254u));

        for (int py = minY; py <= maxY; ++py)
        {
            float dy = float (py - cy);
            for (int px = minX; px <= maxX; ++px)
            {
                float dx = float (px - cx);
                float dist = std::sqrt (dx * dx + dy * dy);
                if (dist > arcOuter + 1.2f) continue;

                // Outer value arc
                if (dist >= arcInner - 0.8f && dist <= arcOuter + 0.8f)
                {
                    float ang = std::atan2 (dy, dx); // -pi..pi
                    if (ang < 0.0f) ang += 2.0f * kPi;
                    float rel = ang - kStartAngle;
                    if (rel < 0.0f) rel += 2.0f * kPi;

                    if (rel <= kSweep)
                    {
                        float t = rel / kSweep;
                        float edgeIn = std::clamp (dist - (arcInner - 0.6f), 0.0f, 1.0f);
                        float edgeOut = std::clamp ((arcOuter + 0.6f) - dist, 0.0f, 1.0f);
                        float cov = std::min (edgeIn, edgeOut);
                        uint32_t col = (t <= norm + 0.004f) ? accent : trackCol;
                        putPixel (px, py, col, uint8_t (cov * 255.0f));
                    }
                }

                // Inner knob cap
                if (dist <= capR + 0.8f)
                {
                    float cov = std::clamp (capR + 0.8f - dist, 0.0f, 1.0f);
                    // Subtle vertical gradient on cap
                    int shade = int ((-dy / std::max (1.0f, capR)) * 14.0f);
                    uint32_t col = (dist > capR - 1.3f) ? capRim : brighten (capCol, shade);
                    putPixel (px, py, col, uint8_t (cov * 255.0f));
                }
            }
        }

        // Pointer needle
        float angle = kStartAngle + norm * kSweep;
        float ca = std::cos (angle);
        float sa = std::sin (angle);
        drawLine (float (cx) + ca * (capR * 0.25f), float (cy) + sa * (capR * 0.25f),
                  float (cx) + ca * (capR - 2.2f), float (cy) + sa * (capR - 2.2f),
                  2.2f, active ? 0xFFFFFFFFu : accent);
    }

    // Blit an ARGB32 image (e.g. Melodee 320x218 live screen) at integer or fractional destination rect
    inline void blitScaled (const uint32_t* src, int srcW, int srcH, int dstX, int dstY, int dstW, int dstH)
    {
        if (!pixels || !src || srcW <= 0 || srcH <= 0 || dstW <= 0 || dstH <= 0) return;
        for (int y = 0; y < dstH; ++y)
        {
            int py = dstY + y;
            if (py < 0 || py >= height) continue;
            int sy = (y * srcH) / dstH;
            const uint32_t* srow = src + sy * srcW;
            uint32_t* drow = pixels + py * width;
            for (int x = 0; x < dstW; ++x)
            {
                int px = dstX + x;
                if (px < 0 || px >= width) continue;
                int sx = (x * srcW) / dstW;
                drow[px] = srow[sx] | 0xFF000000u;
            }
        }
    }

    // Clean built-in 5x7 bitmap font (ASCII 32..126)
    static inline const uint8_t* glyph5x7 (char ch)
    {
        static const uint8_t kFont[95][5] = {
            {0x00,0x00,0x00,0x00,0x00}, // ' '
            {0x00,0x00,0x5F,0x00,0x00}, // '!'
            {0x00,0x07,0x00,0x07,0x00}, // '"'
            {0x14,0x7F,0x14,0x7F,0x14}, // '#'
            {0x24,0x2A,0x7F,0x2A,0x12}, // '$'
            {0x23,0x13,0x08,0x64,0x62}, // '%'
            {0x36,0x49,0x55,0x22,0x50}, // '&'
            {0x00,0x05,0x03,0x00,0x00}, // '\''
            {0x00,0x1C,0x22,0x41,0x00}, // '('
            {0x00,0x41,0x22,0x1C,0x00}, // ')'
            {0x14,0x08,0x3E,0x08,0x14}, // '*'
            {0x08,0x08,0x3E,0x08,0x08}, // '+'
            {0x00,0x50,0x30,0x00,0x00}, // ','
            {0x08,0x08,0x08,0x08,0x08}, // '-'
            {0x00,0x60,0x60,0x00,0x00}, // '.'
            {0x20,0x10,0x08,0x04,0x02}, // '/'
            {0x3E,0x51,0x49,0x45,0x3E}, // '0'
            {0x00,0x42,0x7F,0x40,0x00}, // '1'
            {0x42,0x61,0x51,0x49,0x46}, // '2'
            {0x21,0x41,0x45,0x4B,0x31}, // '3'
            {0x18,0x14,0x12,0x7F,0x10}, // '4'
            {0x27,0x45,0x45,0x45,0x39}, // '5'
            {0x3C,0x4A,0x49,0x49,0x30}, // '6'
            {0x01,0x71,0x09,0x05,0x03}, // '7'
            {0x36,0x49,0x49,0x49,0x36}, // '8'
            {0x06,0x49,0x49,0x29,0x1E}, // '9'
            {0x00,0x36,0x36,0x00,0x00}, // ':'
            {0x00,0x56,0x36,0x00,0x00}, // ';'
            {0x08,0x14,0x22,0x41,0x00}, // '<'
            {0x14,0x14,0x14,0x14,0x14}, // '='
            {0x00,0x41,0x22,0x14,0x08}, // '>'
            {0x02,0x01,0x51,0x09,0x06}, // '?'
            {0x32,0x49,0x79,0x41,0x3E}, // '@'
            {0x7E,0x11,0x11,0x11,0x7E}, // 'A'
            {0x7F,0x49,0x49,0x49,0x36}, // 'B'
            {0x3E,0x41,0x41,0x41,0x22}, // 'C'
            {0x7F,0x41,0x41,0x22,0x1C}, // 'D'
            {0x7F,0x49,0x49,0x49,0x41}, // 'E'
            {0x7F,0x09,0x09,0x09,0x01}, // 'F'
            {0x3E,0x41,0x49,0x49,0x7A}, // 'G'
            {0x7F,0x08,0x08,0x08,0x7F}, // 'H'
            {0x00,0x41,0x7F,0x41,0x00}, // 'I'
            {0x20,0x40,0x41,0x3F,0x01}, // 'J'
            {0x7F,0x08,0x14,0x22,0x41}, // 'K'
            {0x7F,0x40,0x40,0x40,0x40}, // 'L'
            {0x7F,0x02,0x0C,0x02,0x7F}, // 'M'
            {0x7F,0x04,0x08,0x10,0x7F}, // 'N'
            {0x3E,0x41,0x41,0x41,0x3E}, // 'O'
            {0x7F,0x09,0x09,0x09,0x06}, // 'P'
            {0x3E,0x41,0x51,0x21,0x5E}, // 'Q'
            {0x7F,0x09,0x19,0x29,0x46}, // 'R'
            {0x46,0x49,0x49,0x49,0x31}, // 'S'
            {0x01,0x01,0x7F,0x01,0x01}, // 'T'
            {0x3F,0x40,0x40,0x40,0x3F}, // 'U'
            {0x1F,0x20,0x40,0x20,0x1F}, // 'V'
            {0x3F,0x40,0x38,0x40,0x3F}, // 'W'
            {0x63,0x14,0x08,0x14,0x63}, // 'X'
            {0x07,0x08,0x70,0x08,0x07}, // 'Y'
            {0x61,0x51,0x49,0x45,0x43}, // 'Z'
            {0x00,0x7F,0x41,0x41,0x00}, // '['
            {0x02,0x04,0x08,0x10,0x20}, // '\\'
            {0x00,0x41,0x41,0x7F,0x00}, // ']'
            {0x04,0x02,0x01,0x02,0x04}, // '^'
            {0x40,0x40,0x40,0x40,0x40}, // '_'
            {0x00,0x01,0x02,0x04,0x00}, // '`'
            {0x20,0x54,0x54,0x54,0x78}, // 'a'
            {0x7F,0x48,0x44,0x44,0x38}, // 'b'
            {0x38,0x44,0x44,0x44,0x20}, // 'c'
            {0x38,0x44,0x44,0x48,0x7F}, // 'd'
            {0x38,0x54,0x54,0x54,0x18}, // 'e'
            {0x08,0x7E,0x09,0x01,0x02}, // 'f'
            {0x0C,0x52,0x52,0x52,0x3E}, // 'g'
            {0x7F,0x08,0x04,0x04,0x78}, // 'h'
            {0x00,0x44,0x7D,0x40,0x00}, // 'i'
            {0x20,0x40,0x44,0x3D,0x00}, // 'j'
            {0x7F,0x10,0x28,0x44,0x00}, // 'k'
            {0x00,0x41,0x7F,0x40,0x00}, // 'l'
            {0x7C,0x04,0x18,0x04,0x78}, // 'm'
            {0x7C,0x08,0x04,0x04,0x78}, // 'n'
            {0x38,0x44,0x44,0x44,0x38}, // 'o'
            {0x7C,0x14,0x14,0x14,0x08}, // 'p'
            {0x08,0x14,0x14,0x18,0x7C}, // 'q'
            {0x7C,0x08,0x04,0x04,0x08}, // 'r'
            {0x48,0x54,0x54,0x54,0x20}, // 's'
            {0x04,0x3F,0x44,0x40,0x20}, // 't'
            {0x3C,0x40,0x40,0x20,0x7C}, // 'u'
            {0x1C,0x20,0x40,0x20,0x1C}, // 'v'
            {0x3C,0x40,0x30,0x40,0x3C}, // 'w'
            {0x44,0x28,0x10,0x28,0x44}, // 'x'
            {0x0C,0x50,0x50,0x50,0x3C}, // 'y'
            {0x44,0x64,0x54,0x4C,0x44}, // 'z'
            {0x00,0x08,0x36,0x41,0x00}, // '{'
            {0x00,0x00,0x7F,0x00,0x00}, // '|'
            {0x00,0x41,0x36,0x08,0x00}, // '}'
            {0x10,0x08,0x08,0x10,0x08}, // '~'
        };
        int idx = int (static_cast<unsigned char> (ch)) - 32;
        if (idx < 0 || idx >= 95) idx = 0;
        return kFont[idx];
    }

    static inline int textWidth (const std::string& s, int scale = 1)
    {
        if (s.empty()) return 0;
        return int (s.size()) * (6 * scale) - scale;
    }

    inline void drawText (int x, int y, const std::string& text, uint32_t color, int scale = 1, int maxWidth = 0)
    {
        if (!pixels || text.empty() || scale <= 0) return;
        const int step = 6 * scale;
        int cx = x;
        for (size_t i = 0; i < text.size(); ++i)
        {
            if (maxWidth > 0 && (cx - x) + 5 * scale > maxWidth)
                break;
            const uint8_t* colData = glyph5x7 (text[i]);
            for (int col = 0; col < 5; ++col)
            {
                uint8_t bits = colData[col];
                for (int row = 0; row < 7; ++row)
                {
                    if (bits & (1u << row))
                    {
                        if (scale == 1)
                            putPixel (cx + col, y + row, color);
                        else
                            fillRect (cx + col * scale, y + row * scale, scale, scale, color);
                    }
                }
            }
            cx += step;
        }
    }

    inline void drawTextCentered (int cx, int y, const std::string& text, uint32_t color, int scale = 1, int maxWidth = 0)
    {
        std::string clipped = text;
        if (maxWidth > 0)
        {
            int maxChars = std::max (1, (maxWidth + scale) / (6 * scale));
            if (int (clipped.size()) > maxChars)
                clipped = clipped.substr (0, size_t (maxChars));
        }
        int w = textWidth (clipped, scale);
        drawText (cx - w / 2, y, clipped, color, scale);
    }
};

} // namespace hdslave_desktop
