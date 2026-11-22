// Orbital Impact. Drawing and navigation are confined to Melodee's screen thread.
#pragma once

#include "melodee_screen.h"
#include "params.h"
#include "ui/orbital_art.h"
#include "ui/raster.h"
#include <array>
#include <atomic>
#include <cctype>

namespace moonbass
{
class Screen
{
    using Ink = hdui::Ink;
    using Raster = hdui::Raster;
    static constexpr Ink orange = 0xffff7131, lilac = 0xffbc99f5, paper = 0xfffff4db;
    static constexpr Ink muted = 0xffd0b4e7, grid = 0xff3d3046, panel = 0xff241d32;
    static constexpr int pages = paramCount / 3;
    static_assert (paramCount % 3 == 0, "a page holds three parameters: one added alone would not be reachable");
    hdui::Picture picture;
    std::array<float, ParamId::count> values {};
    int page = 0, lastPage = -1, lastPulse = -1;
    melodee_screen_knob_t lastKnobs[3] {};
    bool lastShift = false, pulsing = false;
    uint32_t seenHits = 0;
    uint64_t hitAt = 0;

    static void sprite (Raster& r, const uint8_t* data, int stride, int srcX, int w, int h,
                        int x, int y, int destW, int destH)
    {
        for (int j = 0; j < destH; ++j)
        {
            const uint8_t* row = data + (j * h / destH) * (stride / 2);
            int source = 0;
            for (int i = 0; i < destW; ++i, source += w)
            {
                const int sx = srcX + (destW == w ? i : source / destW);
                const int colour = (row[sx / 2] >> ((sx & 1) ? 0 : 4)) & 15;
                if (colour) r.dot (x + i, y + j, art::palette[colour]);
            }
        }
    }

    static void upper (const char* s, char (&out)[32])
    {
        int n = 0;
        while (*s && n < 31) out[n++] = (char) std::toupper ((unsigned char) *s++);
        out[n] = 0;
    }

    void header (Raster& r) const
    {
        sprite (r, art::logo, 232, 0, 232, 40, 0, 0, 232, 40);
        char text[32];
        upper (paramTable[page * 3].module, text);
        r.right (312, 10, text, paper);
        std::snprintf (text, sizeof (text), "%02d / %02d", page + 1, pages);
        r.right (312, 26, text, lilac);
        r.box (5, 43, 310, 1, 0xff69536e);
    }

    static void globe (Raster& r, int pulse)
    {
        for (int x = 6; x < 159; x += 15) r.box (x, 5, 1, 110, 0xff352c56);
        for (int y = 7; y < 115; y += 15) r.box (5, y, 154, 1, 0xff352c56);
        // A struck shell widens and flattens, then lands back on its native pixels.
        sprite (r, art::moon, 156, 0, 156, 118, 5 - pulse, 2 + pulse, 156 + 2 * pulse, 118 - 2 * pulse);
    }

    float withCv (uint32_t id, int dest) const
    {
        float sum = 0;
        for (uint32_t base = ParamId::cv1Dest; base <= ParamId::cv4Dest; base += 3)
            if ((int) values[base] == dest)
                sum += values[base + 2] / cvVoltsMax * (values[base + 1] / 128.0f);
        return std::clamp (values[id] + sum, 0.0f, 1.0f);
    }

    // Envelopes of the patch at its base pitch. The pitch graph shows the excursion above
    // the resting note; the amp graph includes the gate hold and compression threshold.
    void envelope (Raster& r, bool pitch) const
    {
        const Ink colour = pitch ? orange : lilac;
        const float tune = withCv (ParamId::tune, CvDest::tune) * tuneOctavesMax;
        const float hz = pitchHz (withCv (ParamId::pitch, CvDest::pitch));
        const float decayMs = 1000.0f * decaySeconds (withCv (ParamId::decay, CvDest::decay));
        const float gate = gateMs (values[ParamId::gateTime]);
        const float cut = 0.9f * withCv (ParamId::compression, CvDest::compression);
        const int span = pitch ? 100 : (decayMs + gate <= 400 ? 400 : (decayMs + gate <= 1000 ? 1000 : 2100));
        const float rangeHz = std::max (150.0f, hz * (std::exp2 (tune) - 1.0f));

        r.text (7, 6, pitch ? "PITCH" : "AMP", colour);
        char label[20];
        std::snprintf (label, sizeof (label), "0-%d ms", span);
        r.right (148, 6, label, 0xffd9cce5);
        constexpr int x0 = 7, y0 = 20, w = 139, h = 32;
        for (int k = 0; k <= 4; ++k) r.box (x0 + w * k / 4, y0, 1, h + 1, grid);
        for (int k = 0; k <= 2; ++k) r.box (x0, y0 + h * k / 2, w + 1, 1, grid);
        int previous = 0;
        for (int i = 0; i <= w; ++i)
        {
            const float ms = (float) (i * span) / w;
            float level;
            if (pitch)
                level = hz * (std::exp2 (tune * std::exp (-ms / (1000.0f * tuneDecaySeconds))) - 1.0f) / rangeHz;
            else
                level = std::max (0.0f, (std::exp (-6.90775528f * std::max (ms - gate, 0.0f) / decayMs) - cut) / (1.0f - cut));
            const int y = y0 + h - 1 - (int) std::lround (level * (h - 2));
            if (i) r.line (x0 + i - 1, previous, x0 + i, y, colour);
            else r.box (x0 - 1, y - 1, 3, 3, colour);
            previous = y;
        }
    }

    static void readout (Raster& r, int x, const char* text)
    {
        const char* glyphs = "+-.0123456789";
        const size_t n = std::strspn (text, glyphs);
        if (n == 0 || (text[n] && text[n] != ' '))
        {
            r.text (x, 28, text, paper);
            return;
        }
        for (size_t i = 0; i < n; ++i)
        {
            const int index = (int) (std::strchr (glyphs, text[i]) - glyphs);
            sprite (r, art::digits, 208, 16 * index, 16, 24, x, 18, 16, 24);
            x += text[i] == '.' ? 6 : (text[i] == '-' ? 8 : 12);
        }
        if (text[n]) r.text (x + 3, 31, text + n + 1, muted);
    }

    void footer (Raster& r, const melodee_screen_canvas_t& c) const
    {
        r.box (0, 0, 320, 51, panel);
        r.box (0, 0, 320, 1, 0xff66507d);
        if (c.shift)
        {
            r.text (8, 9, "-/+  Page", paper);
            r.text (119, 9, "Push  Play kick", paper);
            r.text (8, 28, "Wheel  K1", muted);
            r.text (119, 28, "Shift+wheel  Fine", muted);
            return;
        }
        for (int k = 0; k < 3; ++k)
        {
            const int x = 8 + k * 106;
            const auto& p = paramTable[page * 3 + k];
            if (k) r.box (x - 7, 7, 1, 36, 0xff55415d);
            char key[] = { 'K', (char) ('1' + k), 0 };
            r.text (x, 7, key, muted);
            const char* name = p.name;
            const size_t prefix = std::strlen (p.module);
            if (std::strncmp (name, p.module, prefix) == 0 && name[prefix] == ' ') name += prefix + 1;
            if (p.id == ParamId::metalNoise1) name = "Metal 1";
            if (p.id == ParamId::metalNoise2) name = "Metal 2";
            if (p.id == ParamId::noise2Mask) name = "Mask 2";
            char label[32], text[32];
            upper (name, label);
            r.text (x + 18, 7, label, paper);
            if (p.kind == Kind::button)
            {
                r.text (x, 27, "Push to play", paper);
                continue;
            }
            const float v = values[p.id];
            formatParam (p, v, text, sizeof (text));
            readout (r, x, text);
            const int at = (int) std::lround ((v - p.min) / (p.max - p.min) * 87.0f);
            r.box (x, 42, 88, 2, 0xff49364f);
            r.box (x, 42, at + 1, 2, k == 2 ? lilac : orange);
            if (!c.knobs[k].engaged && c.knobs[k].position >= 0)
                r.frame (x + c.knobs[k].position * 87 / 127 - 2, 45, 5, 3, paper);
        }
    }

public:
    int firstRow() const { return page * 3; }
    void navigate (int delta)
    {
        page = std::clamp (page + std::clamp (delta, -pages, pages), 0, pages - 1);
    }
    void knobs (clap_id ids[3]) const
    {
        for (int k = 0; k < 3; ++k)
        {
            const auto& p = paramTable[firstRow() + k];
            ids[k] = p.kind == Kind::button ? CLAP_INVALID_ID : p.id;
        }
    }
    bool animating() const { return pulsing; }

    bool paint (const melodee_screen_canvas_t& c, const std::atomic<float>* params, uint32_t hits)
    {
        const bool whole = c.whole || lastPage < 0;
        const bool moved = lastPage != page;
        bool pitchChanged = false, ampChanged = false, cardsChanged = false, knobsMoved = false;
        for (uint32_t i = 0; i < paramCount; ++i)
        {
            const float v = params[i].load (std::memory_order_relaxed);
            const uint32_t id = paramTable[i].id;
            if (values[id] == v) continue;
            values[id] = v;
            const bool cv = id >= ParamId::cv1Dest && id <= ParamId::cv4In;
            pitchChanged |= cv || id == ParamId::pitch || id == ParamId::tune;
            ampChanged |= cv || id == ParamId::gateTime || id == ParamId::decay || id == ParamId::compression;
            cardsChanged |= (int) (i / 3) == page;
        }
        for (int k = 0; k < 3; ++k)
        {
            knobsMoved |= c.knobs[k].position != lastKnobs[k].position || c.knobs[k].engaged != lastKnobs[k].engaged;
            lastKnobs[k] = c.knobs[k];
        }
        // A hit while the page was away is counted, not shown: a whole paint opens without a pulse.
        if (seenHits != hits) { seenHits = hits; if (! whole) hitAt = c.now_ms, pulsing = true; }
        const uint64_t age = c.now_ms - hitAt;
        pulsing = pulsing && age < 180;
        const int pulse = pulsing ? 6 - (int) (age / 30) : 0;
        bool drew = false;
        if (whole || moved)
            drew |= picture.region<320,45> (c.pixels, c.stride, whole, 0, 0, [&](Raster& r) { header (r); });
        if (whole || lastPulse != pulse)
            drew |= picture.region<164,122> (c.pixels, c.stride, whole, 0, 45, [&](Raster& r) { globe (r, pulse); });
        if (whole || pitchChanged)
            drew |= picture.region<156,61> (c.pixels, c.stride, whole, 164, 45, [&](Raster& r) { envelope (r, true); });
        if (whole || ampChanged)
            drew |= picture.region<156,61> (c.pixels, c.stride, whole, 164, 106, [&](Raster& r) { envelope (r, false); });
        if (whole || moved || cardsChanged || knobsMoved || c.shift != lastShift)
            drew |= picture.region<320,51> (c.pixels, c.stride, whole, 0, 167, [&](Raster& r) { footer (r, c); });
        lastPulse = pulse;
        lastPage = page;
        lastShift = c.shift;
        return drew;
    }
};
} // namespace moonbass
