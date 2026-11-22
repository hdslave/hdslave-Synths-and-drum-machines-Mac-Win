// Liquid Light on Melodee's 320 x 218 canvas. All drawing and navigation run on the main thread.
#pragma once

#include "melodee_screen.h"
#include "params.h"
#include "ui/page.h"
#include "liquid_art.h"
#include <array>
#include <atomic>

namespace necronomicon
{
namespace panels
{
using namespace ParamId;
constexpr uint32_t clock[] = { clockDiv, rhythmLogic, run, reset, next, trigger, quantize, seqOct };
constexpr uint32_t first[] = { seq1Step1, seq1Step2, seq1Step3, seq1Step4, seq1Osc, seq1Sub1, seq1Sub2 };
constexpr uint32_t second[] = { seq2Step1, seq2Step2, seq2Step3, seq2Step4, seq2Osc, seq2Sub1, seq2Sub2 };
constexpr uint32_t voice1[] = { vco1Freq, vco1Sub1, vco1Sub2, vco1Wave };
constexpr uint32_t voice2[] = { vco2Freq, vco2Sub1, vco2Sub2, vco2Wave };
constexpr uint32_t levels[] = { vco1Level, vco1Sub1Level, vco1Sub2Level, vco2Level, vco2Sub1Level, vco2Sub2Level };
constexpr uint32_t filter[] = { cutoff, resonance, vcfEgAmt, vcfAttack, vcfDecay };
constexpr uint32_t amp[] = { egMode, vcaAttack, vcaDecay, volume, fineTune };
struct Group { const char* name; const uint32_t* ids; int count; };
template<size_t N> constexpr Group group (const char* name, const uint32_t (&ids)[N]) { return { name, ids, int (N) }; }
constexpr Group groups[] = { { "RHYTHM", nullptr, 4 }, group ("CLOCK", clock), group ("SEQ 1", first), group ("SEQ 2", second),
    group ("VCO 1", voice1), group ("VCO 2", voice2), group ("MIXER", levels), group ("FILTER", filter), group ("AMP", amp) };
}

struct ScreenEdit
{
    uint32_t id = hdui::noParam;
    float value = 0;
    bool press = false;
};

constexpr int paletteSubsteps = 8;
constexpr int colorCycleSteps = int (std::size (art::driftPalette)) * paletteSubsteps;

class Screen
{
    using Raster = hdui::Raster;
    using Ink = hdui::Ink;
    static constexpr int groupCount = int (std::size (panels::groups));
    int group = 0, rows[groupCount] {}, lastGroup = -1, lastRow = -1;
    bool edit = false, lastEdit = false, lastShift = false, fading = false;
    uint64_t lastPos = 0, flashAt = 0;
    int lastFlash = 0, lastColorStep = 0, lastEye = -1;
    melodee_screen_knob_t lastPots[3] {};
    std::array<float, ParamId::count> sampled {};
    hdui::Picture picture;

    static uint32_t rhythmId (int g, int knob)
    {
        constexpr uint32_t first[] = { ParamId::rhythm1, ParamId::rhythm1Seq1, ParamId::rhythm1Seq2 };
        return first[knob] + uint32_t (g);
    }

    static void print (Raster& r, int x, int y, const unsigned char* src, int stride, int width, int height)
    {
        for (int j = 0; j < height; ++j)
            for (int i = 0; i < width; ++i)
            {
                const auto* p = src + 3 * (j * stride + i);
                r.dot (x + i, y + j, 0xff000000u | (uint32_t (p[0]) << 16) | (p[1] << 8) | p[2]);
            }
    }

    // A narrow chamfer leaves the middle of each edge clear for the legends.
    static void plate (Raster& r, int x, int y, int w, int h, Ink face, Ink edge)
    {
        for (int j = 0; j < h; ++j)
        {
            const int left = std::max (0, 3 - j), right = w - 1 - std::max (0, j - (h - 4));
            const bool rim = j == 0 || j == h - 1;
            r.box (x + left, y + j, right - left + 1, 1, rim ? edge : face);
            if (!rim)
            {
                r.dot (x + left, y + j, edge);
                r.dot (x + right, y + j, edge);
            }
        }
    }

    static void big (Raster& r, int x, int y, const char* label, Ink color, int advance)
    {
        for (const char* s = label; *s; ++s, x += advance)
        {
            const auto& glyph = hdui::font::glyphs[(unsigned char) *s - 32];
            for (int j = 0; j < hdui::font::glyphHeight; ++j)
                for (int k = 0; k < 7; ++k)
                    if (glyph[j] & (1 << k)) r.box (x + k * 2, y + j * 2, 2, 2, color);
        }
    }

    static void knobValue (Raster& r, int center, char* value)
    {
        using namespace hdui::ink;
        char* unit = std::strchr (value, ' ');
        if (unit) *unit++ = 0;
        const int count = int (std::strlen (value));
        const int suffix = unit ? 3 + 7 * int (std::strlen (unit)) : 0;
        const int advance = 14 * count + suffix <= 92 ? 14 : 12;
        if (advance * count + suffix <= 92)
        {
            const int x = center - (advance * count + suffix) / 2;
            big (r, x, 16, value, paper, advance);
            if (unit) r.text (x + count * advance + 3, 24, unit, soft);
        }
        else
        {
            if (unit) unit[-1] = ' ';
            r.text (center - int (std::strlen (value)) * 7 / 2, 21, value, paper);
        }
    }

    void formatted (uint32_t id, char (&buf)[48]) const
    {
        formatParam (paramTable[paramIndex (id)], sampled[id], buf, sizeof buf, stepOctaves (sampled[ParamId::seqOct]));
    }

    static void shiftPalette (std::array<Ink, 256>& colors, int colorStep)
    {
        constexpr int steps = paletteSubsteps, keys = int (std::size (art::driftPalette));
        const int key = colorStep / steps % keys, blend = colorStep % steps;
        const auto& a = art::driftPalette[key];
        const auto& b = art::driftPalette[(key + 1) % keys];
        for (size_t i = 0; i < colors.size(); ++i)
        {
            auto channel = [&](int shift) {
                return ((a[i] >> shift & 255) * (steps - blend) + (b[i] >> shift & 255) * blend + steps / 2) / steps;
            };
            colors[i] = 0xff000000u | (channel (16) << 16) | (channel (8) << 8) | channel (0);
        }
    }

    void window (Raster& r, int g, uint64_t pos, int flash, int colorStep) const
    {
        using namespace hdui::ink;
        const int cx = 43 - g * 2, cy = 48;
        const bool chosen = rows[0] == g;
        const int column = g * 80 * 3;
        print (r, 0, 0, art::rhythmDeck + column, 320, 80, 110);
        if (chosen)
            print (r, 0, 78, (edit ? art::editPlate : art::focusPlate) + column, 320, 80, 14);
        const int route = int (sampled[rhythmId (g, 1)]) * 2 + int (sampled[rhythmId (g, 2)]);
        std::array<Ink, 256> shifted;
        const Ink* palette = art::stainedPalette;
        if (route == 0)
        {
            shiftPalette (shifted, colorStep + 11 * g);
            palette = shifted.data();
        }
        const int d = int (sampled[rhythmId (g, 0)]), phase = int ((pos >> 4) % d);
        constexpr int size = 56;
        const auto* lens = art::lens + (d * (d - 1) / 2 + phase) * size * size;
        for (int y = 0; y < size; ++y)
            for (int x = 0; x < size; ++x)
            {
                const auto index = *lens++;
                if (index) r.dot (cx - size / 2 + x, cy - size / 2 + y, palette[index]);
            }
        for (int n = 0; n < d; ++n)
        {
            const double a = n * 6.283185307179586 / d;
            const int x = cx + int (std::lround (26.1 * std::sin (a))), y = cy - int (std::lround (26.1 * std::cos (a)));
            if (n == phase)
            {
                const int length = 3 + flash;
                r.box (x - length, y, 2 * length + 1, 1, paper);
                r.box (x, y - length, 1, 2 * length + 1, paper);
                r.box (x - 1, y - 1, 3, 3, paper);
            }
            else r.box (x - 1, y, 2, 1, 0xffd698f3);
        }
        char s[20];
        std::snprintf (s, sizeof s, "R%d  /%d", g + 1, d);
        r.text (cx - int (std::strlen (s)) * 7 / 2, 80, s, chosen ? bg : paper);
        constexpr const char* dest[] = { "UNROUTED", "SEQ 2", "SEQ 1", "SEQ 1+2" };
        r.text (cx - int (std::strlen (dest[route])) * 7 / 2, 94, dest[route], soft);
    }

    void list (Raster& r, int eye) const
    {
        using namespace hdui::ink;
        const auto& p = panels::groups[group];
        print (r, 0, 0, art::settings, 320, 320, 110);
        const auto& patch = art::eyePatches[eye];
        print (r, 166 + patch.x, 44 + patch.y, art::eyePixels + patch.offset, patch.w, patch.w, patch.h);
        r.text (12, 3, p.name, paper);
        const int row = rows[group], first = std::clamp (row - 3, 0, std::max (0, p.count - 6));
        for (int i = first; i < std::min (p.count, first + 6); ++i)
        {
            const int y = 20 + 15 * (i - first);
            const bool on = i == row;
            const uint32_t id = p.ids[i];
            if (on)
            {
                print (r, 4, y - 2, art::cursor, 312, 312, 14);
                r.box (7, y + 2, 2, 6, edit ? paper : active);
            }
            const int index = paramIndex (id);
            if (index < 0) continue;
            r.text (12, y, paramTable[index].name, on ? paper : soft);
            char val[48]; formatted (id, val);
            r.right (307, y, val, on ? active : soft);
        }
    }

    void footer (Raster& r, const melodee_screen_canvas_t& c) const
    {
        using namespace hdui::ink;
        if (c.shift)
        {
            print (r, 0, 0, art::help, 320, 320, 42);
            r.text (13, 7, group ? "WHEEL: Row    PUSH: Edit / press" : "WHEEL: Rhythm   PUSH: Edit divisor", paper);
            r.text (13, 24, edit ? "WHEEL: Value    -/+: Coarse value" : "-/+: Pages      K1-K3: Turn values", soft);
            return;
        }
        uint32_t ids[3]; knobs (ids);
        for (int k = 0; k < 3; ++k)
        {
            const int x = 3 + 106 * k;
            print (r, x - 2, 0, art::knobFrame, 106, 106, 42);
            r.box (x + 5, 14, 87, 1, 0xff42344d);
            const char tag[] = { 'K', char ('1' + k), 0 };
            r.text (x + 5, 4, tag, ids[k] == hdui::noParam ? 0xffa291ae : 0xffffc59a);
            if (ids[k] == hdui::noParam)
            {
                r.text (x + 48, 21, "-", 0xffa291ae);
                continue;
            }
            const auto& param = paramTable[paramIndex (ids[k])];
            char label[48], val[48]; formatted (ids[k], val);
            if (group == 0)
            {
                if (k == 0) { std::snprintf (label, sizeof label, "R%c DIV", char ('1' + rows[0])); std::snprintf (val, sizeof val, "/%d", int (sampled[ids[k]])); }
                else { std::strcpy (label, k == 1 ? "TO SEQ 1" : "TO SEQ 2"); std::strcpy (val, sampled[ids[k]] ? "ON" : "OFF"); }
            }
            else if (std::strncmp (param.name, "Seq 1 ", 6) == 0 || std::strncmp (param.name, "Seq 2 ", 6) == 0)
            {
                // The page heading already identifies the sequencer.
                const char* detail = param.name + 6;
                if (detail[0] == '>') std::snprintf (label, sizeof label, "To %s", detail + 2);
                else std::snprintf (label, sizeof label, "%s", detail);
            }
            else
            {
                const char* level = std::strstr (param.name, " Level");
                std::snprintf (label, sizeof label, "%.*s", level ? int (level - param.name) : int (std::strlen (param.name)), param.name);
            }
            r.text (x + 26, 4, label, 0xffdfcfe4, std::strlen (label) > 10 ? 6 : 7);
            knobValue (r, x + 51, val);
            if (!c.knobs[k].engaged && c.knobs[k].position >= 0)
            {
                // The target and the physical pot while Melodee waits for pickup.
                const int target = int (std::lround ((sampled[ids[k]] - param.min) / (param.max - param.min) * 84));
                r.box (x + 7, 36, 89, 4, 0xff21192e);
                r.box (x + 9, 37, 85, 1, rule);
                r.box (x + 9 + target, 36, 1, 3, active);
                r.frame (x + 8 + c.knobs[k].position * 84 / 127, 36, 3, 3, paper);
            }
        }
    }

public:
    bool needsAnimation() const { return group != 0 || fading; }
    bool flashing() const { return fading; } // a hit's short flash on the rhythm page, always run to its end

    void knobs (uint32_t ids[3]) const
    {
        const auto& p = panels::groups[group];
        for (int k = 0; k < 3; ++k)
        {
            if (group == 0) { ids[k] = rhythmId (rows[0], k); continue; }
            const int i = rows[group] + k;
            ids[k] = i < p.count && paramTable[paramIndex (p.ids[i])].kind != Kind::button ? p.ids[i] : hdui::noParam;
        }
    }

    bool input (const melodee_screen_input_t& e, const std::atomic<float>* values, ScreenEdit& out)
    {
        if (e.kind > MELODEE_SCREEN_NUDGE) return false;
        const auto& p = panels::groups[group];
        int& row = rows[group];
        if (e.kind == MELODEE_SCREEN_NUDGE && !edit)
        {
            const int next = std::clamp (group + (e.value > 0 ? 1 : -1), 0, groupCount - 1);
            if (next == group) return false;
            group = next; return true;
        }
        if (e.kind == MELODEE_SCREEN_WHEEL && !edit)
        {
            row = int (std::clamp<int64_t> (int64_t (row) + e.value, 0, p.count - 1));
            return true;
        }
        const uint32_t id = group ? p.ids[row] : rhythmId (row, 0);
        const int index = paramIndex (id);
        const auto& param = paramTable[index];
        if (e.kind == MELODEE_SCREEN_PUSH)
        {
            if (param.kind == Kind::button) out = { id, 0, true };
            else edit = !edit;
            return true;
        }
        const float old = values[index].load (std::memory_order_relaxed);
        const float unit = param.kind == Kind::continuous ? (param.max - param.min) /
            (e.kind == MELODEE_SCREEN_NUDGE ? 16.0f : (e.shift ? 1024.0f : 128.0f)) : 1.0f;
        const float value = clampParam (param, old + e.value * unit);
        if (value != old) out = { id, value, false };
        return true;
    }

    bool paint (const melodee_screen_canvas_t& c, const std::atomic<float>* values, uint64_t pos, int colorStep)
    {
        using namespace hdui::ink;
        const bool whole = c.whole || lastGroup < 0, page = whole || group != lastGroup;
        const bool selection = page || rows[group] != lastRow || edit != lastEdit;
        if (!page && !group && pos != lastPos) { flashAt = c.now_ms; fading = true; }
        if (page || c.now_ms - flashAt >= 100) fading = false;
        const int flash = fading ? 2 - int ((c.now_ms - flashAt) / 50) : 0;
        bool params = false, pots = false;
        for (uint32_t i = 0; i < paramCount; ++i)
        {
            const float v = values[i].load (std::memory_order_relaxed);
            float& prior = sampled[paramTable[i].id];
            params |= prior != v; prior = v;
        }
        for (int k = 0; k < 3; ++k)
        {
            pots |= c.knobs[k].engaged != lastPots[k].engaged || c.knobs[k].position != lastPots[k].position;
            lastPots[k] = c.knobs[k];
        }
        bool drew = false;
        if (whole)
            drew |= picture.region<247,36> (c.pixels, int (c.stride), whole, 0, 0, [](Raster& r) {
                print (r, 0, 0, art::titlePrint, 247, 247, 36);
            });
        if (whole || params)
            drew |= picture.region<73,36> (c.pixels, int (c.stride), whole, 247, 0, [&](Raster& r) {
                print (r, 0, 0, art::readout, 73, 73, 36);
                r.text (8, 5, sampled[ParamId::rhythmLogic] ? "LOGIC XOR" : "LOGIC OR", soft, 6);
                r.right (65, 21, clockDivChoices[int (sampled[ParamId::clockDiv])], paper);
            });
        if (whole)
            drew |= picture.region<320,2> (c.pixels, int (c.stride), whole, 0, 36, [&](Raster& r) { print (r, 0, 0, art::seam, 320, 320, 2); });
        if (!group && (selection || params || pos != lastPos || flash != lastFlash || colorStep != lastColorStep))
            for (int g = 0; g < 4; ++g)
                drew |= picture.region<80,110> (c.pixels, int (c.stride), whole, g * 80, 38,
                        [&](Raster& r) { window (r, g, pos, flash, colorStep); });
        if (group)
        {
            const auto frameIndex = (c.now_ms * 30 + 500) / 1000 % std::size (art::eyeTimeline);
            const int eye = art::eyeTimeline[frameIndex];
            if (selection || params || eye != lastEye)
                drew |= picture.region<320,110> (c.pixels, int (c.stride), whole, 0, 38,
                        [&](Raster& r) { list (r, eye); });
            lastEye = eye;
        }
        if (page || ((pos ^ lastPos) & 15))
            drew |= picture.region<320,28> (c.pixels, int (c.stride), whole, 0, 148, [&](Raster& r) {
                for (int s = 0; s < 2; ++s)
                {
                    const int x = 3 + s * 159;
                    print (r, x - 2, 0, art::seqFrame, 159, 159, 28);
                    r.text (x + 7, 9, s ? "SEQ 2" : "SEQ 1", paper);
                    for (int n = 0; n < 4; ++n)
                    {
                        const bool on = int ((pos >> (2*s)) & 3) == n;
                        const int left = x + 51 + n * 25;
                        plate (r, left, 5, 22, 16, on ? 0xffffc59a : 0xff352a43,
                               on ? 0xffffdfae : 0xff62506f);
                        const char number[] = { char ('1' + n), 0 };
                        r.text (left + 7, 9, number, on ? bg : 0xffddcfe5);
                    }
                }
            });
        if (selection || params || pots || c.shift != lastShift)
            drew |= picture.region<320,42> (c.pixels, int (c.stride), whole, 0, 176, [&](Raster& r) { footer (r, c); });
        lastGroup = group; lastRow = rows[group]; lastEdit = edit; lastShift = c.shift;
        lastFlash = flash; lastPos = pos; lastColorStep = colorStep;
        return drew;
    }
};
} // namespace necronomicon
