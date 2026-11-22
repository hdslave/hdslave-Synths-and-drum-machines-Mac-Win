// Nexus's engine: eight Plaits voices, played as a polyphonic, mono or legato synthesizer with unison, two envelopes,
// two LFOs, a cycling envelope, a random source, a filter and two modulation matrices.
//
// Time: Plaits is written for 48 kHz and blocks of 12 samples, so the engine runs there whatever the host's rate:
// everything (notes, parameters, envelopes, LFOs) moves on that clock, a block at a time, and the 48 kHz output is
// resampled to the host's rate (a windowed-sinc polyphase filter; at 48 kHz the samples pass as they are). Host events
// are stamped with the 48 kHz time they stand for plus a fixed delay a little longer than the resampler's reach, so
// the sound does not depend on how the host cuts its blocks.
#pragma once

#include "params.h"
#include "plaits_voice.h"

#include "stmlib/utils/random.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

namespace nexus
{
constexpr int maxVoices = 8;
constexpr int block = (int) plaits::kBlockSize; // 12
constexpr uint32_t internalRate = 48000;
constexpr float controlRate = (float) internalRate / (float) block; // 4 kHz
// One voice's level in the mix, against Plaits' full scale (the plug-in Nexus is modelled on, measured).
constexpr float voiceGain = 0.356f;

// The output stage, at the host's rate (after the resampler, which may overshoot): samples up to 0.8 (about -2 dBFS)
// pass as they are; above, a knee (a rational tanh, slope 1 where it starts and 0 where it ends) bends them smoothly to
// full scale, reached at 1.4 (about +3 dBFS). Eight resonant voices at full volume never pass +-1.
constexpr float limiterKnee = 0.8f;
inline float softLimit (float x)
{
    const float a = std::fabs (x);
    if (a <= limiterKnee) return x;
    const float u = std::min ((a - limiterKnee) * (1.0f / (1.0f - limiterKnee)), 3.0f), u2 = u * u;
    const float y = std::min (limiterKnee + (1.0f - limiterKnee) * (u * (27.0f + u2) / (27.0f + 9.0f * u2)), 1.0f);
    return x < 0.0f ? -y : y;
}

// What the audio thread tells the page.
struct Telemetry
{
    static constexpr int scopeSize = 2048;
    std::atomic<uint32_t> sounding { 0 }, gated { 0 }; // a bit a voice
    std::atomic<int> keys[maxVoices];
    std::atomic<float> levels[maxVoices];
    std::atomic<float> lfo[2], cycle { 0 }, random { 0 }, env1 { 0 }, env2 { 0 }, wheel { 0 }, pressure { 0 };
    std::atomic<float> cutoff { 1 }; // the newest voice's, after modulation
    std::atomic<uint32_t> notes { 0 };
    std::atomic<float> scope[scopeSize];
    std::atomic<uint32_t> scopeAt { 0 };
    Telemetry()
    {
        for (int i = 0; i < maxVoices; ++i) keys[i].store (-1, std::memory_order_relaxed), levels[i].store (0, std::memory_order_relaxed);
        lfo[0].store (0), lfo[1].store (0);
        for (auto& s : scope) s.store (0.0f, std::memory_order_relaxed);
    }
};

// A deterministic generator for the modulators (Plaits' engines keep their own).
struct Rng
{
    uint32_t s = 0x9e3779b9u;
    uint32_t next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
    float bipolar() { return (float) (next() >> 8) * (2.0f / 16777216.0f) - 1.0f; }
};

inline float onePole (float ms) // the share a 4 kHz step moves toward a target with time constant `ms`
{
    return ms <= 0.25f ? 1.0f : 1.0f - std::exp (-1000.0f / (ms * controlRate));
}

// ADSR at the control rate. Attack: an analog-style charge toward 1.5, reaching 1 at the attack time. Decay and
// release: exponential with the time as their time constant.
struct Adsr
{
    enum Stage : uint8_t { idle, attack, decay, release } stage = idle;
    float value = 0.0f;
    struct Coefficients { float attack, decay, sustain, release; };
    void gateOn (bool restart)
    {
        if (restart) value = 0.0f;
        stage = attack;
    }
    void gateOff()
    {
        if (stage != idle) stage = release;
    }
    float tick (const Coefficients& c)
    {
        switch (stage)
        {
            case idle: value = 0.0f; break;
            case attack:
                value += (1.5f - value) * c.attack;
                if (value >= 1.0f) value = 1.0f, stage = decay;
                break;
            case decay: value += (c.sustain - value) * c.decay; break;
            case release:
                value -= value * c.release;
                if (value < 1e-5f) value = 0.0f, stage = idle;
                break;
        }
        return value;
    }
    static Coefficients coefficients (float attackMs, float decayMs, float sustain, float releaseMs)
    {
        // The charge toward 1.5 reaches 1 after ln 3 time constants.
        const float a = attackMs <= 0.25f ? 1.0f : 1.0f - std::exp (-1000.0f * std::log (3.0f) / (attackMs * controlRate));
        return { a, onePole (decayMs), sustain, onePole (releaseMs) };
    }
};

// An LFO's state (one for the plug-in, or one a voice).
struct LfoState
{
    float phase = 0.0f, held = 0.0f, from = 0.0f, to = 0.0f;
    void restart (Rng& rng)
    {
        phase = 0.0f;
        from = to, to = rng.bipolar(), held = rng.bipolar();
    }
    void wrap (Rng& rng) { from = to, to = rng.bipolar(), held = rng.bipolar(); }
    float value (int shape, float offset) const
    {
        float p = phase + offset;
        p -= std::floor (p);
        switch (shape)
        {
            case LfoShape::sine: return std::sin (6.28318531f * p);
            case LfoShape::triangle: return p < 0.5f ? -1.0f + 4.0f * p : 3.0f - 4.0f * p;
            case LfoShape::saw: return -1.0f + 2.0f * p;
            case LfoShape::square: return p < 0.5f ? 1.0f : -1.0f;
            case LfoShape::random: return held;
            default: return from + (to - from) * phase;
        }
    }
};

// The plug-in's TPT state-variable filter (Zavalishin), a voice each.
struct Svf
{
    float s1 = 0.0f, s2 = 0.0f;
    float a1 = 1.0f, a2 = 0.0f, a3 = 0.0f, k = 2.0f;
    void set (float hz, float q)
    {
        const float g = std::tan (3.14159265f * std::min (hz, 0.49f * (float) internalRate) / (float) internalRate);
        k = 1.0f / q;
        a1 = 1.0f / (1.0f + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }
    template <int mode> float process (float x)
    {
        const float v3 = x - s2;
        const float v1 = a1 * s1 + a2 * v3;
        const float v2 = s2 + a2 * s1 + a3 * v3;
        s1 = 2.0f * v1 - s1;
        s2 = 2.0f * v2 - s2;
        if (mode == 0) return v2;
        if (mode == 1) return v1;
        return x - k * v1 - v2;
    }
    void clear() { s1 = s2 = 0.0f; }
};

// 48 kHz to the host's rate: a polyphase windowed sinc, its phases interpolated. It reads a mirrored ring (each sample
// written twice, a ring apart) so a window's taps are always contiguous, four at a time.
class Resampler
{
public:
    typedef float Four __attribute__ ((vector_size (16)));

    void prepare (uint32_t hostRate)
    {
        rate = std::max<uint32_t> (hostRate, 1);
        identity = rate == internalRate;
        num = 0;
        index = 0;
        if (identity)
        {
            half = taps = 0;
            table.clear();
            return;
        }
        const double ratio = std::min (1.0, (double) rate / (double) internalRate); // < 1 when going down
        const double fc = 0.5 * ratio * 0.92;                                        // cycles a 48 kHz sample
        half = (int) std::ceil (16.0 / ratio);
        taps = (2 * half + 3) & ~3; // in fours; the extra taps (right of the window) weigh nothing
        table.assign ((size_t) (phases + 1) * (size_t) taps, 0.0f);
        for (int p = 0; p <= phases; ++p)
        {
            const double frac = (double) p / phases;
            double sum = 0.0;
            float* row = &table[(size_t) p * (size_t) taps];
            for (int j = 0; j < 2 * half; ++j)
            {
                const double d = (double) (j - half + 1) - frac; // from the output's place to the tap
                const double x = 2.0 * fc * d;
                const double sinc = std::fabs (x) < 1e-9 ? 1.0 : std::sin (M_PI * x) / (M_PI * x);
                const double w = std::fabs (d) >= half ? 0.0 : kaiser (d / half);
                row[j] = (float) (2.0 * fc * sinc * w);
                sum += row[j];
            }
            for (int j = 0; j < 2 * half; ++j)
                row[j] = (float) (row[j] / sum);
        }
    }
    bool isIdentity() const { return identity; }
    // How far right of the output's place the window reads, and its whole width.
    int ahead() const { return identity ? 0 : taps - half; }
    int width() const { return taps; }
    // The highest 48 kHz sample the next output needs.
    uint64_t needs() const { return index + (uint64_t) ahead(); }
    // The next output from a mirrored ring of N samples (2 N floats a side). `silentBefore`: the ring holds nothing but
    // zeros from that sample on (no voice sounded since).
    template <size_t N2> void next (const float (&ringL)[N2], const float (&ringR)[N2], uint64_t silentFrom, float& l, float& r)
    {
        constexpr uint64_t mask = N2 / 2 - 1;
        if (identity)
        {
            l = ringL[index & mask], r = ringR[index & mask];
            ++index;
            return;
        }
        const uint64_t first = index - (uint64_t) (half - 1);
        if (first >= silentFrom)
            l = r = 0.0f;
        else
        {
            const float pos = (float) num / (float) rate * (float) phases;
            const int p = std::min ((int) pos, phases - 1);
            const float t = pos - (float) p;
            const float* a = &table[(size_t) p * (size_t) taps];
            const float* b = a + taps;
            const float* xl = &ringL[first & mask];
            const float* xr = &ringR[first & mask];
            Four accL = { 0, 0, 0, 0 }, accR = { 0, 0, 0, 0 };
            for (int j = 0; j < taps; j += 4)
            {
                Four va, vb, sl, sr;
                std::memcpy (&va, a + j, 16), std::memcpy (&vb, b + j, 16);
                std::memcpy (&sl, xl + j, 16), std::memcpy (&sr, xr + j, 16);
                const Four w = va + (vb - va) * t;
                accL += w * sl, accR += w * sr;
            }
            l = (accL[0] + accL[1]) + (accL[2] + accL[3]);
            r = (accR[0] + accR[1]) + (accR[2] + accR[3]);
        }
        num += internalRate;
        while (num >= rate) num -= rate, ++index;
    }
    void reset() { num = 0, index = 0; }

private:
    static double kaiser (double x) // beta 9
    {
        auto i0 = [] (double v) {
            double sum = 1.0, term = 1.0;
            for (int k = 1; k < 30; ++k) term *= (v / (2.0 * k)) * (v / (2.0 * k)), sum += term;
            return sum;
        };
        constexpr double beta = 9.0;
        return i0 (beta * std::sqrt (std::max (0.0, 1.0 - x * x))) / i0 (beta);
    }
    static constexpr int phases = 128;
    uint32_t rate = internalRate;
    bool identity = true;
    int half = 0, taps = 0;
    uint64_t num = 0, index = 0;
    std::vector<float> table;
};

class Engine
{
public:
    explicit Engine (Telemetry* telemetry) : tel (telemetry)
    {
        for (int i = 0; i < paramCount; ++i) value[i] = paramTable[i].def;
        std::fill (keyVelocity, keyVelocity + 128, 1.0f);
        for (int v = 0; v < maxVoices; ++v)
        {
            stmlib::BufferAllocator allocator (voices[v].ram, plaits::kNexusVoiceRam);
            voices[v].plaits.Init (&allocator);
            voices[v].random = 0x21u + 0x9e3779b9u * (uint32_t) v;
        }
        derive();
    }

    // [main thread, not active] The host's rate; the engine starts from silence.
    void prepare (double hostRate)
    {
        hostHz = (uint32_t) std::lround (std::clamp (hostRate, 8000.0, 384000.0));
        resampler.prepare (hostHz);
        delay = resampler.isIdentity() ? (uint64_t) block : (uint64_t) (resampler.ahead() + block + 1);
        reset();
        prepared = true;
    }
    // [main thread] Events still queued are dropped: the plug-in's own copy of the values holds them, and it sets them
    // all again before the next prepare().
    void deactivate()
    {
        prepared = false;
        head = tail = 0;
        lastStamp = 0;
    }

    void reset()
    {
        // Values still waiting in the queue are the host's already: they hold across a reset.
        for (uint32_t i = head; i != tail; i = (i + 1) % queueSize)
            if (events[i].type == Ev::param) value[events[i].index] = events[i].v, dirty = true;
        if (dirty) derive();
        for (auto& v : voices)
        {
            v.plaits.Silence();
            v.active = v.gate = v.sustained = v.releaseDue = v.fading = false;
            v.env1 = Adsr {}, v.env2 = Adsr {};
            v.filter.clear();
            v.key = -1;
        }
        std::memset (ringL, 0, sizeof (ringL));
        std::memset (ringR, 0, sizeof (ringR));
        resampler.reset();
        hostFrame = 0;
        rendered = 0;
        silentFrom = 0;
        head = tail = 0;
        lastStamp = 0;
        stackSize = 0;
        pedal = legatoPedal = false;
        portamentoFrom = -1;
        bend = wheel = channelPressure = 0.0f;
        quietBlocks = 1 << 20;
        lfoGlobal[0] = lfoGlobal[1] = LfoState {};
        cyclePhase = 0.0f, cycleFalling = false;
        rndPhase = 0.0f, rndHeld = rndPrev = rndValue = rndDrift = rndDriftTarget = 0.0f;
        rng = Rng {};
        tempo = 120.0, beatsAt = 0.0, beatsSample = 0, playing = false;
        lastKey = -1;
        noteCounter = 0;
        std::fill (keyVelocity, keyVelocity + 128, 1.0f);
    }

    // --- Events: stamped at `frame` of the block being processed ------------------------------------------------------
    void stamp (uint32_t frame) { stampFrame = frame; }
    void set (uint32_t id, float v)
    {
        if (id >= (uint32_t) paramCount) return;
        if (! prepared) { value[id] = v, derive(); return; }
        push (Ev { 0, Ev::param, 0, (uint16_t) id, v, 0.0 });
    }
    void noteOn (int key, float velocity) { push (Ev { 0, Ev::noteOn, (uint8_t) std::clamp (key, 0, 127), 0, velocity, 0.0 }); }
    void noteOff (int key) { push (Ev { 0, Ev::noteOff, (uint8_t) (key < 0 || key > 127 ? 255 : key), 0, 0.0f, 0.0 }); }
    void choke (int key) { push (Ev { 0, Ev::choke, (uint8_t) (key < 0 || key > 127 ? 255 : key), 0, 0.0f, 0.0 }); }
    void controller (int number, int amount) { push (Ev { 0, Ev::cc, (uint8_t) (number & 127), (uint16_t) (amount & 127), 0.0f, 0.0 }); }
    void pitchBend (float amount) { push (Ev { 0, Ev::bend, 0, 0, std::clamp (amount, -1.0f, 1.0f), 0.0 }); }
    void polyPressure (int key, float amount) { push (Ev { 0, Ev::polyAt, (uint8_t) (key & 127), 0, std::clamp (amount, 0.0f, 1.0f), 0.0 }); }
    void pressure (float amount) { push (Ev { 0, Ev::chanAt, 0, 0, std::clamp (amount, 0.0f, 1.0f), 0.0 }); }
    // The host's transport at the block's start (beats from the song's top).
    void transport (bool isPlaying, double beats, double bpm)
    {
        push (Ev { 0, Ev::clock, (uint8_t) (isPlaying ? 1 : 0), 0, (float) bpm, beats });
    }

    // Renders the host block (its events were stamped before).
    void render (float* left, float* right, uint32_t frames)
    {
        for (uint32_t n = 0; n < frames; ++n)
        {
            while (rendered <= resampler.needs())
                tick();
            float l, r;
            resampler.next (ringL, ringR, silentFrom, l, r);
            left[n] = softLimit (l);
            if (right != nullptr) right[n] = softLimit (r);
        }
        hostFrame += frames;
        stampFrame = 0;
    }

    // Nothing sounds, nothing waits: the host may let the plug-in sleep.
    bool quiet() const
    {
        if ((long) quietBlocks * block <= resampler.width() + 4 * block) return false;
        // The transport's updates may wait (they are taken in order when the host calls again); anything else may not.
        for (uint32_t i = head; i != tail; i = (i + 1) % queueSize)
            if (events[i].type != Ev::clock) return false;
        return true;
    }
    uint32_t hostRate() const { return hostHz; }
    float valueOf (uint32_t id) const { return value[id]; }

private:
    struct Ev
    {
        enum Type : uint8_t { param, noteOn, noteOff, choke, cc, bend, polyAt, chanAt, clock };
        uint64_t at;
        Type type;
        uint8_t key;
        uint16_t index;
        float v;
        double d;
    };

    struct Voice
    {
        plaits::NexusVoice plaits;
        char ram[plaits::kNexusVoiceRam];
        uint32_t random = 0x21;
        bool active = false, gate = false, sustained = false;
        // A key let go before the voice has played a block plays that block first (a note shorter than 0.25 ms still
        // strikes); `rendered`: it has played one since it started.
        bool releaseDue = false, rendered = false, filterOpen = true, fading = false;
        float lastIn = 0.0f; // the filter's last input, to take it up again from where the sound is
        int key = -1;
        uint32_t note = 0, age = 0; // the note it plays (voices of one note share it), when it started
        int unisonIndex = 0, unisonCount = 1;
        float velocity = 0.0f, velocityAmount = 0.0f, pressure = 0.0f;
        float pitch = 0.0f, target = 0.0f, glide = 1.0f; // the key being glided, in semitones
        Adsr env1, env2;
        LfoState lfo[2];
        double trigClock = 0.0;
        int triggerLow = 0;
        bool retrigEdge = false;
        Svf filter;
        int silent = 0;
        float vca = 0.0f, panL = 0.70710678f, panR = 0.70710678f;
        float peak = 0.0f;
        float out[block], aux[block];
    };

    void push (Ev e)
    {
        if (! prepared) return;
        const uint64_t frame = hostFrame + stampFrame;
        e.at = frame * internalRate / hostHz + delay;
        e.at = std::max (e.at, lastStamp);
        lastStamp = e.at;
        const uint32_t next = (tail + 1) % queueSize;
        if (next == head) // full: the oldest goes now
            apply (events[head]), head = (head + 1) % queueSize;
        events[tail] = e;
        tail = next;
    }

    // --- Parameters -------------------------------------------------------------------------------------------------
    float v (uint32_t id) const { return value[id]; }
    int iv (uint32_t id) const { return (int) value[id]; }
    void derive()
    {
        env1c = Adsr::coefficients (envMs (v (ParamId::env1Attack)), envMs (v (ParamId::env1Decay)), v (ParamId::env1Sustain),
                                    envMs (v (ParamId::env1Release)));
        env2c = Adsr::coefficients (envMs (v (ParamId::env2Attack)), envMs (v (ParamId::env2Decay)), v (ParamId::env2Sustain),
                                    envMs (v (ParamId::env2Release)));
        routeCount = 0;
        for (int s = 0; s < routeSources; ++s)
            for (int t = 0; t < routeTargets; ++t)
            {
                const float amount = v (ParamId::route (s, t));
                if (v (ParamId::route (s, t) + 1) >= 0.5f && amount != 0.0f)
                    routes[routeCount++] = { (uint8_t) routeSourceOf[s], (uint8_t) routeTargetOf[t], depth (amount) };
            }
        for (int i = 0; i < assignSlots; ++i)
        {
            const int source = iv (ParamId::assignSource0 + (uint32_t) i);
            if (source == Source::off) continue;
            for (int j = 0; j < assignSlots; ++j)
            {
                const int target = iv (ParamId::assignTarget0 + (uint32_t) j);
                const float amount = v (ParamId::assign (i, j));
                if (target != Target::off && v (ParamId::assign (i, j) + 1) >= 0.5f && amount != 0.0f)
                    routes[routeCount++] = { (uint8_t) source, (uint8_t) target, depth (amount) };
            }
        }
        glideCoef = onePole (glideMs (v (ParamId::glide)));
        slideCoef = onePole (glideMs (v (ParamId::glide)) > 0.0f ? glideMs (v (ParamId::glide)) : 60.0f);
        dirty = false;
    }

    // --- Notes ------------------------------------------------------------------------------------------------------
    int unisonNow() const { return std::clamp (iv (ParamId::unison), 1, maxVoices); }
    int mode() const { return iv (ParamId::voiceMode); }
    float curvedVelocity (float velocity) const { return std::pow (std::clamp (velocity, 0.0f, 1.0f), v (ParamId::velCurve)); }
    float curvedPressure (float x) const
    {
        const float c = v (ParamId::atCurve), e = 1.0f + 3.0f * std::fabs (c);
        x = std::clamp (x, 0.0f, 1.0f);
        if (c < 0.0f) return std::pow (x, e);
        if (c > 0.0f) return 1.0f - std::pow (1.0f - x, e);
        return x;
    }

    // Starts (or restarts) voice `i` on a note. `retrigger`: a new attack; else the voice carries on at the new key.
    void startVoice (Voice& vc, int key, float velocity, uint32_t note, int unisonIndex, int unisonCount, bool retrigger, float fromPitch,
                     float coef)
    {
        const bool wasActive = vc.active;
        vc.active = true;
        vc.gate = true;
        vc.sustained = false;
        vc.key = key;
        vc.note = note;
        vc.unisonIndex = unisonIndex, vc.unisonCount = unisonCount;
        vc.target = (float) key;
        vc.pitch = fromPitch;
        vc.glide = coef;
        vc.silent = 0;
        vc.releaseDue = false;
        vc.fading = false;
        if (! retrigger) return;
        vc.rendered = false;
        vc.age = ++ages;
        vc.velocity = velocity;
        vc.velocityAmount = curvedVelocity (velocity);
        vc.pressure = 0.0f;
        vc.env1.gateOn (v (ParamId::env1Retrig) >= 0.5f);
        vc.env2.gateOn (v (ParamId::env2Retrig) >= 0.5f);
        for (int l = 0; l < 2; ++l)
            if (v (ParamId::lfo1Retrig + 7u * (uint32_t) l) >= 0.5f || ! wasActive)
                vc.lfo[l].restart (rng);
        vc.trigClock = 0.0;
        // A trigger that is already high goes low for a block, so the module sees an edge.
        vc.triggerLow = wasActive ? 1 : 0;
        if (! wasActive)
        {
            vc.filter.clear();
            vc.lastIn = 0.0f;
            vc.vca = 0.0f;
        }
    }

    void noteStarted (int key)
    {
        tel->notes.fetch_add (1, std::memory_order_relaxed);
        // The plug-in's own modulators that restart on a note.
        for (int l = 0; l < 2; ++l)
            if (v (ParamId::lfo1Retrig + 7u * (uint32_t) l) >= 0.5f && v (ParamId::lfo1PerVoice + 7u * (uint32_t) l) < 0.5f)
                lfoGlobal[l].restart (rng);
        if (v (ParamId::cycRetrig) >= 0.5f) cyclePhase = 0.0f, cycleFalling = false;
        if (v (ParamId::rndRetrig) >= 0.5f)
        {
            rndPhase = 0.0f;
            rndPrev = rndValue;
            rndHeld = rng.bipolar();
            if (iv (ParamId::rndMode) != RndMode::smooth) rndValue = rndHeld;
        }
        lastKey = key;
    }

    // The notes held (keys down or the pedal), not counting key `except`; `victim`: the one to let go first, the oldest
    // the pedal holds, else the oldest key.
    int heldNotes (uint32_t* victim, int except) const
    {
        uint32_t seen[maxVoices];
        int count = 0;
        uint32_t heldAge = 0, pedalAge = 0, heldNote = 0, pedalNote = 0;
        bool anyHeld = false, anyPedal = false; // (a voice let go for a new note has age 0)
        for (const auto& vc : voices)
        {
            if (! vc.active || ! vc.gate || vc.key == except) continue;
            bool known = false;
            for (int k = 0; k < count; ++k) known |= seen[k] == vc.note;
            if (known) continue;
            seen[count++] = vc.note;
            if (vc.sustained) { if (! anyPedal || vc.age < pedalAge) pedalAge = vc.age, pedalNote = vc.note, anyPedal = true; }
            else if (! anyHeld || vc.age < heldAge) heldAge = vc.age, heldNote = vc.note, anyHeld = true;
        }
        *victim = anyPedal ? pedalNote : heldNote;
        return count;
    }

    // Lets a note's voices go at once (they are about to be taken).
    void letGo (uint32_t note)
    {
        for (auto& vc : voices)
            if (vc.active && vc.gate && vc.note == note)
            {
                vc.gate = vc.sustained = vc.releaseDue = false;
                vc.env1.gateOff(), vc.env2.gateOff();
                vc.age = 0;
            }
    }

    // Stops a voice within a block (its gain ramps to nothing), then frees it.
    void fadeOut (Voice& vc)
    {
        vc.gate = vc.sustained = vc.releaseDue = false;
        vc.fading = true;
        vc.age = 0;
    }

    void release (Voice& vc)
    {
        if (! vc.rendered)
        {
            vc.releaseDue = true;
            vc.sustained = false;
            return;
        }
        vc.gate = false;
        vc.sustained = false;
        vc.env1.gateOff();
        vc.env2.gateOff();
    }

    // A slide or tie (CC 84 named `from`, still held): the voices on `from` go on to `to` with no new attack.
    bool slide (int to)
    {
        const int from = portamentoFrom;
        portamentoFrom = -1;
        if (from < 0) return false;
        bool any = false;
        const float coef = legatoPedal ? 1.0f : slideCoef;
        for (auto& vc : voices)
            if (vc.active && vc.gate && vc.key == from)
            {
                vc.key = to, vc.target = (float) to, vc.glide = coef, vc.sustained = false;
                any = true;
            }
        if (! any) return false;
        if (mode() != VoiceMode::poly)
        {
            removeKey (from);
            pushKey (to);
        }
        lastKey = to;
        return true;
    }

    void pushKey (int key)
    {
        removeKey (key);
        if (stackSize == stackCapacity) std::memmove (stack, stack + 1, sizeof (int) * (stackCapacity - 1)), --stackSize;
        stack[stackSize++] = key;
    }
    void removeKey (int key)
    {
        int w = 0;
        for (int r = 0; r < stackSize; ++r)
            if (stack[r] != key) stack[w++] = stack[r];
        stackSize = w;
    }

    void noteOnNow (int key, float velocity)
    {
        if (velocity <= 0.0f) { noteOffNow (key); return; }
        if (slide (key)) return;
        const int u = unisonNow();
        if (mode() == VoiceMode::poly)
        {
            // The same key again: the voices on it start over (held or still ringing), so two voices never play one
            // pitch against each other. Past NOTES (other keys held), the oldest note lets go.
            const int maxNotes = std::max (1, std::min (iv (ParamId::notes), maxVoices / u));
            uint32_t victim = 0;
            const int others = heldNotes (&victim, key);
            if (others >= maxNotes)
                for (auto& vc : voices)
                    if (vc.active && vc.gate && vc.note == victim) release (vc), vc.age = 0;
            const bool legato = others > 0 && lastKey >= 0;
            const uint32_t note = ++noteCounter;
            int chosen[maxVoices];
            int count = 0;
            for (int i = 0; i < maxVoices; ++i)
                if (voices[i].active && voices[i].key == key)
                {
                    if (count < u) chosen[count++] = i;
                    else fadeOut (voices[i]); // more than unison asks now: the rest stop (not two voices on one pitch)
                }
            // Then free voices, then released ones (oldest first); a held voice only when none is left, and then its
            // whole note goes.
            while (count < u)
            {
                int best = -1;
                long score = 0;
                for (int i = 0; i < maxVoices; ++i)
                {
                    const Voice& vc = voices[i];
                    if (std::find (chosen, chosen + count, i) != chosen + count || (vc.active && vc.gate)) continue;
                    const long sc = ! vc.active ? -1 : (long) vc.age;
                    if (best < 0 || sc < score) best = i, score = sc;
                }
                if (best >= 0) { chosen[count++] = best; continue; }
                uint32_t oldest = 0;
                if (heldNotes (&oldest, key) == 0) break;
                letGo (oldest);
            }
            const bool glides = legato && glideMs (v (ParamId::glide)) > 0.0f;
            for (int i = 0; i < count; ++i)
                startVoice (voices[chosen[i]], key, velocity, note, i, count, true, glides ? (float) lastKey : (float) key, glideCoef);
            noteStarted (key);
            return;
        }
        // Mono and Legato: one note, `u` voices (the first ones).
        keyVelocity[key] = velocity;
        const bool overlap = stackSize > 0;
        pushKey (key);
        const bool retrigger = mode() == VoiceMode::mono || ! overlap || ! voices[0].active || ! voices[0].gate;
        const uint32_t note = retrigger ? ++noteCounter : voices[0].note;
        for (int i = 0; i < maxVoices; ++i)
        {
            Voice& vc = voices[i];
            if (i >= u)
            {
                if (vc.active && vc.gate) release (vc);
                continue;
            }
            const bool glides = overlap && vc.active;
            const float from = glides ? vc.pitch : (float) key;
            startVoice (vc, key, velocity, note, i, u, retrigger || ! vc.active, from, glides ? glideCoef : 1.0f);
        }
        if (retrigger) noteStarted (key);
        else lastKey = key;
    }

    void noteOffNow (int key)
    {
        if (key == 255) // every key
        {
            stackSize = 0;
            for (auto& vc : voices)
                if (vc.active && vc.gate) pedal ? (void) (vc.sustained = true) : release (vc);
            return;
        }
        if (mode() == VoiceMode::poly)
        {
            for (auto& vc : voices)
                if (vc.active && vc.gate && vc.key == key && ! vc.sustained)
                    pedal ? (void) (vc.sustained = true) : release (vc);
            return;
        }
        const bool wasTop = stackSize > 0 && stack[stackSize - 1] == key;
        removeKey (key);
        if (! wasTop) return;
        if (stackSize > 0)
        {
            // Back to the newest key still held: Mono plays it again, Legato glides there.
            const int back = stack[stackSize - 1];
            const bool retrigger = mode() == VoiceMode::mono;
            const uint32_t note = retrigger ? ++noteCounter : voices[0].note;
            const int u = unisonNow();
            for (int i = 0; i < maxVoices; ++i)
            {
                Voice& vc = voices[i];
                if (i >= u)
                {
                    if (vc.active && vc.gate) release (vc);
                    continue;
                }
                startVoice (vc, back, keyVelocity[back], note, i, u, retrigger || ! vc.active, vc.active ? vc.pitch : (float) back,
                            glideCoef);
            }
            if (retrigger) noteStarted (back);
            else lastKey = back;
            return;
        }
        for (auto& vc : voices)
            if (vc.active && vc.gate) pedal ? (void) (vc.sustained = true) : release (vc);
    }

    void kill (Voice& vc)
    {
        vc.active = vc.gate = vc.sustained = vc.releaseDue = vc.fading = false;
        vc.env1 = Adsr {}, vc.env2 = Adsr {};
        vc.plaits.Silence();
        vc.filter.clear();
        vc.key = -1;
    }

    void apply (const Ev& e)
    {
        // A value set at the same time as a note is in place for it.
        if (dirty && e.type != Ev::param) derive();
        switch (e.type)
        {
            case Ev::param:
                if (value[e.index] != e.v)
                {
                    const int old = (int) value[e.index];
                    value[e.index] = e.v;
                    dirty = true;
                    // A new voice mode starts clean (held notes are let go).
                    if (e.index == ParamId::voiceMode && old != (int) e.v)
                    {
                        stackSize = 0;
                        for (auto& vc : voices)
                            if (vc.active && vc.gate) release (vc);
                    }
                }
                break;
            case Ev::noteOn: noteOnNow (e.key, e.v); break;
            case Ev::noteOff: noteOffNow (e.key == 255 ? 255 : e.key); break;
            // A choke and All Sound Off stop their voices within a block (0.25 ms), not in one sample: no step.
            case Ev::choke:
                for (auto& vc : voices)
                    if (vc.active && (e.key == 255 || vc.key == e.key)) fadeOut (vc);
                if (e.key == 255) stackSize = 0;
                else removeKey (e.key);
                break;
            case Ev::cc:
                switch (e.key)
                {
                    case 1: wheel = (float) e.index / 127.0f; break;
                    case 64:
                        pedal = e.index >= 64;
                        if (! pedal)
                            for (auto& vc : voices)
                                if (vc.sustained) release (vc);
                        break;
                    case 68: legatoPedal = e.index >= 64; break;
                    case 84: portamentoFrom = e.index; break;
                    // Channel modes (value 0): all sound off, reset controllers, all notes off. They never move a parameter.
                    case 120:
                        if (e.index == 0)
                        {
                            for (auto& vc : voices)
                                if (vc.active) fadeOut (vc);
                            stackSize = 0;
                        }
                        break;
                    case 121: // Reset All Controllers (RP-015): bend, wheel, pressure, the pedals
                        if (e.index == 0)
                        {
                            bend = 0.0f, wheel = 0.0f, channelPressure = 0.0f, legatoPedal = false, portamentoFrom = -1;
                            pedal = false;
                            for (auto& vc : voices)
                            {
                                vc.pressure = 0.0f;
                                if (vc.sustained) release (vc);
                            }
                        }
                        break;
                    case 123:
                        if (e.index == 0)
                        {
                            const bool held = pedal;
                            pedal = false;
                            noteOffNow (255);
                            pedal = held;
                        }
                        break;
                    default: break;
                }
                break;
            case Ev::bend: bend = e.v; break;
            case Ev::polyAt:
                for (auto& vc : voices)
                    if (vc.active && vc.key == e.key) vc.pressure = e.v;
                break;
            case Ev::chanAt: channelPressure = e.v; break;
            case Ev::clock:
                playing = e.key != 0;
                tempo = std::clamp ((double) e.v, 1.0, 1000.0);
                beatsAt = e.d;
                beatsSample = e.at;
                break;
        }
    }

    // --- One 12-sample block at 48 kHz ------------------------------------------------------------------------------
    double beatsNow() const
    {
        return beatsAt + (double) (blockStart - beatsSample) / (double) internalRate * tempo / 60.0;
    }

    void advanceLfo (LfoState& s, int l, float hz, bool locked, double divisionBeats)
    {
        if (locked)
        {
            double p = beatsNow() / divisionBeats + 0.0;
            p -= std::floor (p);
            if ((float) p < s.phase) s.wrap (rng);
            s.phase = (float) p;
            return;
        }
        (void) l;
        s.phase += hz / controlRate;
        if (s.phase >= 1.0f)
        {
            s.phase -= std::floor (s.phase);
            s.wrap (rng);
        }
    }

    float lfoHz (int l) const
    {
        const uint32_t o = 7u * (uint32_t) l;
        if (v (ParamId::lfo1Sync + o) >= 0.5f)
            return (float) (tempo / 60.0 / divBeats[std::clamp (iv (ParamId::lfo1Div + o), 0, 10)]);
        return rateHz (v (ParamId::lfo1Rate + o));
    }

    void tickModulators()
    {
        // LFOs (the plug-in's own; voices advance theirs).
        for (int l = 0; l < 2; ++l)
        {
            const uint32_t o = 7u * (uint32_t) l;
            const bool synced = v (ParamId::lfo1Sync + o) >= 0.5f;
            const bool locked = synced && playing && v (ParamId::lfo1Retrig + o) < 0.5f;
            advanceLfo (lfoGlobal[l], l, lfoHz (l), locked, divBeats[std::clamp (iv (ParamId::lfo1Div + o), 0, 10)]);
            lfoNow[l] = lfoGlobal[l].value (iv (ParamId::lfo1Shape + o), v (ParamId::lfo1Phase + o));
        }
        // Cycle: rise then fall, again and again.
        {
            const float ms = cycleFalling ? cycleMs (v (ParamId::cycFall)) : cycleMs (v (ParamId::cycRise));
            cyclePhase += 1000.0f / (ms * controlRate);
            while (cyclePhase >= 1.0f)
            {
                cyclePhase -= 1.0f;
                cycleFalling = ! cycleFalling;
                if (cyclePhase >= 1.0f) cyclePhase = 0.0f;
            }
            const float p = cyclePhase;
            float u;
            switch (iv (ParamId::cycShape))
            {
                case 1: u = cycleFalling ? (1.0f - p) * (1.0f - p) : p * p; break;
                case 2: u = cycleFalling ? 1.0f - p * p : 1.0f - (1.0f - p) * (1.0f - p); break;
                default: u = cycleFalling ? 1.0f - p : p; break;
            }
            cycleNow = v (ParamId::cycBipolar) >= 0.5f ? 2.0f * u - 1.0f : u;
        }
        // Random.
        {
            const bool synced = v (ParamId::rndSync) >= 0.5f;
            const float hz = synced ? (float) (tempo / 60.0 / divBeats[std::clamp (iv (ParamId::rndDiv), 0, 10)]) : rateHz (v (ParamId::rndRate));
            rndPhase += hz / controlRate;
            const int m = iv (ParamId::rndMode);
            if (rndPhase >= 1.0f)
            {
                rndPhase -= std::floor (rndPhase);
                rndPrev = rndValue;
                rndHeld = rng.bipolar();
                rndDriftTarget = std::clamp (rndDriftTarget + 0.5f * rng.bipolar(), -1.0f, 1.0f);
            }
            if (m == RndMode::sampleHold) rndValue = rndHeld;
            else if (m == RndMode::smooth)
            {
                // From the last value to the new one over Slew's share of the period (no slew: a step).
                const float slew = v (ParamId::rndSlew);
                const float t = slew <= 0.0f ? 1.0f : std::min (1.0f, rndPhase / slew);
                const float s = t * t * (3.0f - 2.0f * t);
                rndValue = rndPrev + (rndHeld - rndPrev) * s;
            }
            else
            {
                // Drift: a wandering walk, as slow as the rate.
                const float coef = hz <= 0.0f ? 0.0f : 1.0f - std::exp (-hz / controlRate * 2.0f);
                rndDrift += (rndDriftTarget - rndDrift) * coef;
                rndValue = rndDrift;
            }
        }
    }

    void tick()
    {
        blockStart = rendered;
        while (head != tail && events[head].at <= blockStart)
        {
            apply (events[head]);
            head = (head + 1) % queueSize;
        }
        if (dirty) derive();
        tickModulators();

        float mixL[block] {}, mixR[block] {};
        bool any = false;
        uint32_t sounding = 0, gated = 0;
        int newest = -1;
        for (int i = 0; i < maxVoices; ++i)
        {
            Voice& vc = voices[i];
            if (! vc.active) continue;
            renderVoice (vc, mixL, mixR);
            if (vc.active)
            {
                any = true;
                sounding |= 1u << i;
                if (vc.gate) gated |= 1u << i;
                if (newest < 0 || vc.age > voices[newest].age) newest = i;
            }
        }
        for (int n = 0; n < block; ++n)
        {
            const uint64_t at = (rendered + (uint64_t) n) & ringMask;
            ringL[at] = ringL[at + ringSize] = mixL[n], ringR[at] = ringR[at + ringSize] = mixR[n];
        }
        rendered += block;
        if (any) silentFrom = rendered;
        quietBlocks = any ? 0 : std::min (quietBlocks + 1, 1 << 20);

        // The page's view.
        tel->sounding.store (sounding, std::memory_order_relaxed);
        tel->gated.store (gated, std::memory_order_relaxed);
        for (int i = 0; i < maxVoices; ++i)
        {
            tel->keys[i].store (voices[i].active ? voices[i].key : -1, std::memory_order_relaxed);
            tel->levels[i].store (voices[i].active ? voices[i].peak : 0.0f, std::memory_order_relaxed);
        }
        tel->lfo[0].store (lfoNow[0], std::memory_order_relaxed), tel->lfo[1].store (lfoNow[1], std::memory_order_relaxed);
        tel->cycle.store (cycleNow, std::memory_order_relaxed), tel->random.store (rndValue, std::memory_order_relaxed);
        tel->wheel.store (wheel, std::memory_order_relaxed), tel->pressure.store (channelPressure, std::memory_order_relaxed);
        if (newest >= 0)
        {
            tel->env1.store (voices[newest].env1.value, std::memory_order_relaxed);
            tel->env2.store (voices[newest].env2.value, std::memory_order_relaxed);
            if (v (ParamId::lfo1PerVoice) >= 0.5f) tel->lfo[0].store (voiceLfo[newest][0], std::memory_order_relaxed);
            if (v (ParamId::lfo2PerVoice) >= 0.5f) tel->lfo[1].store (voiceLfo[newest][1], std::memory_order_relaxed);
            tel->cutoff.store (voiceCutoff[newest], std::memory_order_relaxed);
        }
        else
            tel->env1.store (0.0f, std::memory_order_relaxed), tel->env2.store (0.0f, std::memory_order_relaxed),
                tel->cutoff.store (v (ParamId::cutoff), std::memory_order_relaxed);
        if (any)
        {
            const uint32_t s = tel->scopeAt.load (std::memory_order_relaxed);
            for (int n = 0; n < block; ++n)
                tel->scope[(s + (uint32_t) n) % Telemetry::scopeSize].store (0.5f * (mixL[n] + mixR[n]), std::memory_order_relaxed);
            tel->scopeAt.store (s + block, std::memory_order_release);
        }
    }

    void renderVoice (Voice& vc, float* mixL, float* mixR)
    {
        const int index = (int) (&vc - voices);
        // The voice's own modulators.
        const float e1 = vc.env1.tick (env1c);
        const float e2 = vc.env2.tick (env2c);
        float lfo[2];
        for (int l = 0; l < 2; ++l)
        {
            const uint32_t o = 7u * (uint32_t) l;
            if (v (ParamId::lfo1PerVoice + o) >= 0.5f)
            {
                advanceLfo (vc.lfo[l], l, lfoHz (l), false, 1.0);
                lfo[l] = vc.lfo[l].value (iv (ParamId::lfo1Shape + o), v (ParamId::lfo1Phase + o));
            }
            else
                lfo[l] = lfoNow[l];
            voiceLfo[index][l] = lfo[l];
        }
        float source[Source::count];
        source[Source::off] = 0.0f;
        source[Source::lfo1] = lfo[0], source[Source::lfo2] = lfo[1];
        source[Source::env1] = e1, source[Source::env2] = e2;
        source[Source::cycle] = cycleNow, source[Source::random] = rndValue;
        source[Source::velocity] = vc.velocityAmount;
        source[Source::aftertouch] = curvedPressure (std::max (vc.pressure, channelPressure));
        source[Source::wheel] = wheel;
        float mod[Target::count] {};
        for (int r = 0; r < routeCount; ++r)
            mod[routes[r].target] += routes[r].depth * source[routes[r].source];

        // Pitch: the key glided, Frequency, the bend, unison's spread, the modulations.
        vc.pitch += (vc.target - vc.pitch) * vc.glide;
        if (std::fabs (vc.target - vc.pitch) < 1e-4f) vc.pitch = vc.target;
        const float centre = 0.5f * (float) (vc.unisonCount - 1);
        const float spreadAt = vc.unisonCount > 1 ? ((float) vc.unisonIndex - centre) / centre : 0.0f; // -1..1
        const float detuneSemis = ((float) vc.unisonIndex - centre) * 0.6f * v (ParamId::detune);
        const float note = vc.pitch + v (ParamId::frequency) + bend * (float) iv (ParamId::bendRange) + detuneSemis + pitchRange * mod[Target::pitch];

        // Retrigger at the trig rate while the key is held.
        const int rate = iv (ParamId::trigRate);
        bool gateLevel = vc.gate;
        if (rate > 0 && vc.gate)
        {
            const double period = trigRateBeats[rate] * 60.0 / tempo * (double) internalRate;
            vc.trigClock += block;
            if (vc.trigClock >= period)
            {
                vc.trigClock -= period * std::floor (vc.trigClock / period);
                vc.env1.gateOn (v (ParamId::env1Retrig) >= 0.5f);
                vc.env2.gateOn (v (ParamId::env2Retrig) >= 0.5f);
                vc.triggerLow = 1;
            }
            gateLevel = vc.trigClock < 0.5 * period;
        }

        const int amp = iv (ParamId::ampMode);
        plaits::NexusPatch patch;
        patch.note = 0.0f;
        patch.harmonics = v (ParamId::harmonics) + mod[Target::harmonics];
        patch.timbre = std::clamp (v (ParamId::timbre) + mod[Target::timbre], 0.0f, 1.0f);
        patch.morph = std::clamp (v (ParamId::morph) + mod[Target::morph], 0.0f, 1.0f);
        patch.frequency_modulation_amount = std::clamp (v (ParamId::fmAtt) + mod[Target::fm], -1.0f, 1.0f);
        patch.timbre_modulation_amount = v (ParamId::timbreAtt);
        patch.morph_modulation_amount = v (ParamId::morphAtt);
        patch.engine = iv (ParamId::model);
        patch.decay = std::clamp (v (ParamId::lpgDecay) + mod[Target::lpgDecay], 0.0f, 1.0f);
        patch.lpg_colour = std::clamp (v (ParamId::lpgColor) + mod[Target::lpgColor], 0.0f, 1.0f);

        plaits::NexusModulations m;
        m.note = note;
        const bool triggerHigh = vc.triggerLow > 0 ? false : (amp == Amp::gate ? gateLevel : vc.gate);
        if (vc.triggerLow > 0) --vc.triggerLow;
        m.trigger = triggerHigh ? 1.0f : 0.0f;
        m.trigger_patched = amp != Amp::drone;
        m.level_patched = amp == Amp::gate || amp == Amp::env;
        float vca = 1.0f;
        switch (amp)
        {
            case Amp::gate: m.level = (gateLevel ? 1.0f : 0.0f) * vc.velocityAmount; break;
            case Amp::env: m.level = e1 * vc.velocityAmount; break;
            case Amp::ping: m.level = 0.0f, vca = vc.velocityAmount; break;
            default: m.level = 0.0f, vca = e1 * vc.velocityAmount; break;
        }

        stmlib::Random::Seed (vc.random);
        vc.plaits.Render (patch, m, vc.out, vc.aux, block);
        vc.random = stmlib::Random::state();
        vc.rendered = true;

        // Out / aux, the filter, the VCA, volume and pan.
        const float aux = std::clamp (v (ParamId::auxMix) + mod[Target::aux], 0.0f, 1.0f);
        const float cut = std::clamp (v (ParamId::cutoff) + mod[Target::cutoff], 0.0f, 1.0f);
        const float reso = std::clamp (v (ParamId::resonance) + mod[Target::resonance], 0.0f, 1.0f);
        voiceCutoff[index] = cut;
        const int filter = iv (ParamId::filterMode);
        const bool open = filter == 0 && cut >= 0.999f && reso <= 0.001f;
        if (! open)
        {
            // Back in after being fully open: from where the sound is (a low-pass's resting state for it), not from
            // what it held before.
            if (vc.filterOpen) vc.filter.s1 = 0.0f, vc.filter.s2 = vc.lastIn;
            vc.filter.set (cutoffHz (cut), resonanceQ (reso));
        }
        vc.filterOpen = open;
        const float level = std::clamp (v (ParamId::volume) * 0.5f + mod[Target::volume], 0.0f, 1.0f) * 2.0f;
        const float p = std::clamp (v (ParamId::pan) + spreadAt * v (ParamId::spread) + mod[Target::pan], -1.0f, 1.0f);
        const float angle = (p + 1.0f) * 0.78539816f;
        const float targetL = std::cos (angle), targetR = std::sin (angle);
        const float gainTo = vc.fading ? 0.0f : vca * level * voiceGain;
        const float gainStep = (gainTo - vc.vca) / (float) block;
        const float lStep = (targetL - vc.panL) / (float) block, rStep = (targetR - vc.panR) / (float) block;
        float g = vc.vca, gl = vc.panL, gr = vc.panR, peak = 0.0f;
        for (int n = 0; n < block; ++n)
        {
            float x = vc.out[n] + (vc.aux[n] - vc.out[n]) * aux;
            vc.lastIn = x;
            if (! open)
                x = filter == 0 ? vc.filter.process<0> (x) : (filter == 1 ? vc.filter.process<1> (x) : vc.filter.process<2> (x));
            g += gainStep, gl += lStep, gr += rStep;
            x *= g;
            peak = std::max (peak, std::fabs (x));
            mixL[n] += x * gl, mixR[n] += x * gr;
        }
        vc.vca = gainTo, vc.panL = targetL, vc.panR = targetR;
        vc.peak = std::max (peak, vc.peak * 0.995f);
        if (vc.fading)
        {
            kill (vc);
            vc.peak = 0.0f;
            return;
        }
        // A key let go before this block: it has played it, now it goes.
        if (vc.releaseDue)
            vc.releaseDue = false, release (vc);

        // A voice let go ends when it has been silent a while (the gate shut, the envelope done).
        const bool envelopeDone = (amp != Amp::drone && amp != Amp::env) || vc.env1.stage == Adsr::idle;
        if (! vc.gate && envelopeDone && peak < 1e-5f)
        {
            if (++vc.silent > 64)
            {
                // Ended: the envelopes too (a next note with Retrig off would otherwise rise from what was left).
                vc.active = false;
                vc.key = -1;
                vc.peak = 0.0f;
                vc.env1 = Adsr {}, vc.env2 = Adsr {};
            }
        }
        else
            vc.silent = 0;
    }

    Telemetry* tel;
    float value[paramCount];
    bool dirty = true, prepared = false;
    Adsr::Coefficients env1c {}, env2c {};
    struct Route { uint8_t source, target; float depth; };
    Route routes[routeSources * routeTargets + assignSlots * assignSlots] {};
    int routeCount = 0;
    float glideCoef = 1.0f, slideCoef = 1.0f;

    Voice voices[maxVoices];
    float voiceLfo[maxVoices][2] {};
    float voiceCutoff[maxVoices] {};
    uint32_t ages = 0, noteCounter = 0;
    static constexpr int stackCapacity = 16;
    int stack[stackCapacity] {};
    int stackSize = 0;
    int lastKey = -1, portamentoFrom = -1;
    float keyVelocity[128] {};
    bool pedal = false, legatoPedal = false;
    float bend = 0.0f, wheel = 0.0f, channelPressure = 0.0f;

    LfoState lfoGlobal[2];
    float lfoNow[2] {};
    float cyclePhase = 0.0f, cycleNow = 0.0f;
    bool cycleFalling = false;
    float rndPhase = 0.0f, rndHeld = 0.0f, rndPrev = 0.0f, rndValue = 0.0f, rndDrift = 0.0f, rndDriftTarget = 0.0f;
    Rng rng;

    double tempo = 120.0, beatsAt = 0.0;
    uint64_t beatsSample = 0;
    bool playing = false;

    static constexpr uint32_t queueSize = 4096;
    Ev events[queueSize];
    uint32_t head = 0, tail = 0;
    uint64_t lastStamp = 0;

    Resampler resampler;
    uint32_t hostHz = internalRate;
    uint64_t delay = block;
    uint64_t hostFrame = 0, rendered = 0, blockStart = 0;
    uint32_t stampFrame = 0;
    int quietBlocks = 1 << 20;
    static constexpr uint64_t ringSize = 2048, ringMask = ringSize - 1;
    float ringL[2 * ringSize] {}, ringR[2 * ringSize] {}; // mirrored: each sample at its place and a ring later
    uint64_t silentFrom = 0; // nothing but zeros in the ring from here on
};
} // namespace nexus
