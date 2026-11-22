// Foundry's drum page. The indexed sheet holds the panel artwork and four strike
// intensities for each family (drums, snares and metals). Everything that reads out is live.
#pragma once
#include "melodee_screen.h"
#include "params.h"
#include "ui/font.h"
#include "screen-art.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cctype>

namespace foundry
{
inline uint64_t clockMs()
{
    using namespace std::chrono;
    return duration_cast<milliseconds> (steady_clock::now().time_since_epoch()).count();
}

// One writer on the audio thread. Each slot is a complete observation, even if a reader
// falls more than a ring behind. The sound never waits for a paint, including while hidden.
struct HitMeter
{
    static constexpr unsigned capacity = 512;
    std::atomic<uint64_t> hits[capacity] {};
    std::atomic<unsigned> end { 0 };
    static_assert (std::atomic<uint64_t>::is_always_lock_free);
    void strike (int voice, float vel)
    {
        const unsigned n = end.load (std::memory_order_relaxed);
        const unsigned level = (unsigned) std::clamp (std::lround (vel * 127), 0l, 127l);
        hits[n % capacity].store ((clockMs() << 12) | (level << 4) | (unsigned) voice, std::memory_order_relaxed);
        end.store (n + 1, std::memory_order_release);
    }
};

// Multiple taps between two audio callbacks retain their order and their original pad.
struct Auditions
{
    struct Tap { uint64_t when; int voice; };
    static constexpr unsigned capacity = 32;
    Tap taps[capacity] {};
    std::atomic<unsigned> read { 0 }, written { 0 };
    void push (int voice)
    {
        const unsigned w = written.load (std::memory_order_relaxed);
        if (w - read.load (std::memory_order_acquire) == capacity) return;
        taps[w % capacity] = { clockMs(), voice };
        written.store (w + 1, std::memory_order_release);
    }
    void discard() { read.store (written.load (std::memory_order_acquire), std::memory_order_release); }
    template <typename Play> void take (Play play)
    {
        unsigned r = read.load (std::memory_order_relaxed);
        const unsigned w = written.load (std::memory_order_acquire);
        if (w == r) return;
        const uint64_t now = clockMs();
        while (r != w)
        {
            const Tap tap = taps[r++ % capacity];
            if (now - tap.when <= 500) play (tap.voice);
        }
        read.store (w, std::memory_order_release);
    }
};

struct Paper
{
    uint32_t* pixels;
    uint32_t stride;
    void box (int x, int y, int w, int h, uint32_t ink) const
    {
        h = std::min (h, MELODEE_SCREEN_HEIGHT - y);
        for (int j = 0; j < h; ++j) std::fill_n (pixels + (y+j)*stride + x, w, ink);
    }
    void print (int x, int y, int w, int h, int sx, int sy) const
    {
        for (int j = 0; j < h; ++j)
        {
            const auto* src = art::pixels + (sy + j) * 364 + sx;
            auto* dst = pixels + (y + j) * stride + x;
            for (int i = 0; i < w; ++i) dst[i] = art::palette[src[i]];
        }
    }
    void text (int x, int y, const char* s, uint32_t ink, int limit = 320, bool upper = false,
               int advance = hdui::font::glyphWidth) const
    {
        constexpr int glyphWidth = hdui::font::glyphWidth;
        const int natural = int (std::strlen (s)) * advance;
        const int width = std::min (natural, limit);
        for (int dx = 0; dx < width; ++dx)
        {
            const int column = dx * natural * glyphWidth / (width * advance);
            int ch = (unsigned char) s[column / glyphWidth];
            if (upper) ch = std::toupper (ch);
            const auto& glyph = hdui::font::glyphs[(ch >= 32 && ch <= 127 ? ch : '?') - 32];
            for (int row = 0; row < hdui::font::glyphHeight; ++row)
                if (glyph[row] & (1 << (column % glyphWidth)))
                {
                    const int top = row * advance / glyphWidth;
                    box (x + dx, y + top, 1, (row + 1) * advance / glyphWidth - top, ink);
                }
        }
    }
};

struct Screen
{
    static constexpr uint32_t night = 0xff0c1318, acid = 0xff80e1f2, paper = 0xffeff5f6, quiet = 0xffb9cbd1;
    static constexpr int span = 2000;
    int pad = 0, bank = 0;
    bool dirty = true, moving = false, shifted = false;
    int previousPot[3] { -2, -2, -2 };
    uint64_t lastPaint = 0, canvasOrigin = 0, meterOrigin = 0;
    bool clockSet = false;
    int voice() const { return padVoice[pad]; }
    int voiceRows() const { return sharedVoiceControls + typeSpec (voiceSpecs[voice()].type).slots; }
    int voiceBanks() const { return (voiceRows() + 2) / 3; }
    int bankCount() const { return voiceBanks() + (M::count + 2) / 3; }

    void knobs (clap_id ids[3]) const
    {
        const int first = paramIndex (voiceId (voice(), V::level));
        for (int i = 0; i < 3; ++i)
        {
            const int row = bank * 3 + i;
            if (bank < voiceBanks())
                ids[i] = row < voiceRows() ? table().rows[first + row].id : CLAP_INVALID_ID;
            else
            {
                const unsigned kit = (bank - voiceBanks()) * 3 + i;
                ids[i] = kit < M::count ? kit : CLAP_INVALID_ID;
            }
        }
    }
    bool move (uint32_t kind, int delta)
    {
        if (kind == MELODEE_SCREEN_WHEEL)
        {
            pad = (pad + delta % voiceCount + voiceCount) % voiceCount;
            bank = 0;
        }
        else if (kind == MELODEE_SCREEN_NUDGE)
        {
            const int n = bankCount();
            bank = (bank + delta % n + n) % n;
        }
        else return false;
        dirty = true;
        return true;
    }

    struct Mark { int pad, age, strength; };
    struct History
    {
        std::array<Mark, HitMeter::capacity> marks;
        int count = 0;
        bool flash[voiceCount] {};
        int lanes[4] {};
        int frame = -1;
    };
    void readHits (History& h, const HitMeter& meter, uint64_t now) const
    {
        const unsigned end = meter.end.load (std::memory_order_acquire);
        int lanes = 0;
        auto addLane = [&] (int p) {
            if (lanes < 4 && std::find (h.lanes, h.lanes + lanes, p) == h.lanes + lanes) h.lanes[lanes++] = p;
        };
        addLane (pad);
        float strongest = 0;
        for (unsigned i = 1; i <= HitMeter::capacity; ++i)
        {
            const auto event = meter.hits[(end-i) % HitMeter::capacity].load (std::memory_order_relaxed);
            const uint64_t stamp = event >> 12;
            const uint64_t age = now - std::min (now, stamp);
            if (!event || age >= span) break;
            const int v = event & 15, p = padOf (v), level = (event >> 4) & 127;
            h.marks[h.count++] = { p, int (age), level };
            addLane (p);
            h.flash[p] |= age < 135;
            const Type t = voiceSpecs[v].type;
            const int family = t == Type::kick || t == Type::tom ? 0 :
                               t == Type::snare || t == Type::clap || t == Type::rim || t == Type::clave ? 1 : 2;
            constexpr int release[] = { 290, 160, 130 };
            const float env = std::max (0.0f, 1.0f - float (age) / release[family]);
            const float amount = (level / 127.0f) * env * env;
            if (amount > strongest && amount > 0.04f)
            {
                strongest = amount;
                h.frame = 4 * family + (amount > 0.8f ? 3 : amount > 0.5f ? 2 : amount > 0.24f ? 1 : 0);
            }
        }
        for (int p = 0; p < 4; ++p) addLane (p);
        std::sort (h.lanes, h.lanes + 4);
    }

    void panel (const Paper& g, const History& h, int model) const
    {
        char number[16];
        std::snprintf (number, sizeof number, "A%02d", pad+1);
        g.text (7, 41, number, acid);
        g.text (32, 41, voiceSpecs[voice()].name, paper, 99, true);
        for (int p = 0; p < voiceCount; ++p)
        {
            const int x = 7 + (p%4)*31, y = 54 + (3-p/4)*18;
            const uint32_t fill = p == pad ? acid : h.flash[p] ? 0xff3e626f : 0xff26343d;
            for (int j = 0; j < 15; ++j)
            {
                int left = std::max (0, 3-j), right = std::max (0, j-11);
                g.box (x+left, y+j, 28-left-right, 1, fill);
            }
            g.text (x+7, y+4, voiceSpecs[padVoice[p]].tag, p == pad ? night : paper);
            if (h.flash[p]) g.box (x+4, y+13, 20, 1, p == pad ? night : quiet);
        }
        std::snprintf (number, sizeof number, "MODEL  %s", modelChoices[model]);
        g.text (7, 129, number, quiet);
        if (h.frame >= 0) g.print (133, 32, 182, 109, (h.frame % 2)*182, 218 + (h.frame/2)*109);
        for (int k = 0; k < 4; ++k)
        {
            const int y = 149+k*9, lane = h.lanes[k];
            g.text (7, y-4, voiceSpecs[padVoice[lane]].tag, lane == pad ? acid : quiet);
            const uint32_t ink = lane == pad ? acid : (k%2 ? 0xff68c9f2 : 0xffa9c5d2);
            for (int i = 0; i < h.count; ++i)
            {
                const auto& mark = h.marks[i];
                if (mark.pad != lane) continue;
                const int x = 263 - mark.age*238/span, height = 1 + (mark.strength*3 + 63)/127;
                g.box (x, y-height, 1, height*2, ink);
                g.box (x-1, y-1, 3, 2, ink);
            }
        }
    }
    void cards (const Paper& g, const std::atomic<float>* values, const melodee_screen_canvas& c) const
    {
        clap_id ids[3];
        knobs (ids);
        for (int k = 0; k < 3; ++k)
        {
            if (ids[k] == CLAP_INVALID_ID) continue;
            const int idx = paramIndex (ids[k]), x = 10 + k*106;
            const auto& p = table().rows[idx];
            const float value = values[idx].load (std::memory_order_relaxed);
            const char* name = p.name;
            if (ids[k] >= voiceIdBase) name += std::strlen (voiceSpecs[voice()].tag) + 1;
            const char key[] = { 'K', char ('1'+k), 0 };
            // Keep four clear rows (194–197) between the badges and values. Even a
            // descender in "Ping-Pong" clears the pot rail and Melodee's bottom place bar.
            g.box (x, 185, 17, 9, night);
            g.text (x+1, 186, key, c.knobs[k].engaged ? acid : paper);
            g.text (x+21, 185, name, night, 73, true);
            char buf[48];
            formatParam (p, value, buf, sizeof buf);
            if (bank == 0 && std::round (value) == value && (p.unit == Unit::db || p.unit == Unit::semitones) && value > silentDb)
                std::snprintf (buf, sizeof buf, "%.0f %s", value, p.unit == Unit::db ? "dB" : "st");
            g.text (x, 198, buf, night, 91, false, 10);
            if (c.knobs[k].position >= 0)
            {
                g.box (x, 214, 91, 1, 0xff8596a0);
                const int knob = std::clamp<int> (c.knobs[k].position, 0, 127);
                g.box (x+knob*89/127, 213, 2, 2, c.knobs[k].engaged ? night : 0xff477c93);
            }
        }
    }

    bool paint (const melodee_screen_canvas& c, const std::atomic<float>* values, const HitMeter& meter, bool changed)
    {
        for (int i = 0; i < 3; ++i)
        {
            const int pot = (c.knobs[i].position + 1)*2 + c.knobs[i].engaged;
            if (pot != previousPot[i]) dirty = true;
            previousPot[i] = pot;
        }
        dirty |= shifted != c.shift || changed;
        shifted = c.shift;
        if (!c.whole && !dirty && (!moving || c.now_ms-lastPaint < 40)) return false;
        History history;
        // The canvas clock and the audio meter clock are both monotonic but have different origins.
        if (!clockSet || c.now_ms < lastPaint)
        {
            clockSet = true;
            canvasOrigin = c.now_ms;
            meterOrigin = clockMs();
        }
        readHits (history, meter, meterOrigin + (c.now_ms - canvasOrigin));
        moving = history.count > 0;
        Paper g { c.pixels, c.stride };
        g.print (0, 0, 320, 218, 0, 0);
        int model = int (values[paramIndex (voiceId (voice(), V::model))].load (std::memory_order_relaxed));
        panel (g, history, model);
        cards (g, values, c);
        if (shifted)
        {
            g.box (5, 143, 310, 38, night);
            g.text (8, 146, "WHEEL  SELECT PAD     PUSH  AUDITION", paper);
            g.text (8, 162, "-/+    VOICE AND KIT KNOB BANKS", acid);
        }
        dirty = false;
        lastPaint = c.now_ms;
        return true;
    }
};
}
