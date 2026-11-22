// MoonBass's sound: one analog-style kick drum voice, run at the host's rate.
//
//   note / Play --> trigger: Gate Time opens the gate; the VCO's phase starts at 0
//   Pitch (+ key, Split and Pitch Mode) + Tune x pitch envelope + LFOs --> VCO: sine, bent toward a cusped wave (Harmonics)
//   gate --> Pulse (the gate's edge through a coupling high pass) + Noise (Metal Noise 1 + 2, during the gate) --> Attack
//   VCO --> VCA (held for the gate, then Decay; Compression cuts the tail) + attack --> EQ (one-pole low pass) --> Volume --> out
//
// Every hit takes Tune, Decay, Pulse, Noise, Attack, both metal noises and Gate Time as they are at the hit, as the
// hardware's analog circuit does (its CV list calls these "not realtime"); Pitch, Harmonics, EQ, Compression, Volume
// and the LFOs move while it sounds.
//
// Cost: no allocation, no locks; the knobs are worked out every 8 frames, the attack section only for the gate and
// the moment after it, and nothing at all once the kick has died away (the LFOs' places are worked out from the time).
#pragma once

#include "params.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace moonbass
{
// --- Fast math ------------------------------------------------------------------------------------------------

// Two branch-free instructions (fmaxnm, fminnm); a NaN comes out as `lo`.
inline float clampf (float x, float lo, float hi) { return std::fmin (std::fmax (x, lo), hi); }

// 2^x to about 3e-6 relative (0.005 cent): the integer part by rounding, so the polynomial only spans [-0.5, 0.5].
// For pitch and the EQ's cutoff, whose octaves stay within [-30, 30]; envelope rates use exp.
inline float exp2Fast (float x)
{
    const float xi = std::rint (x), f = x - xi;
    const float p = 1.0f + f * (0.69314718f + f * (0.24022651f + f * (0.05550411f + f * (0.00961813f + f * 0.00133336f))));
    const int32_t bits = ((int32_t) xi + 127) << 23;
    float scale;
    std::memcpy (&scale, &bits, sizeof (scale));
    return p * scale;
}

// tanh, rational, exact at 0 and +-3 where it reaches +-1 and holds: bounded, smooth, cheap.
inline float tanhFast (float x)
{
    x = clampf (x, -3.0f, 3.0f);
    const float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

// sin(2 pi p) for a phase p in [0, 1]: folded to a quarter wave, then an odd polynomial (Taylor to x^9, under 4e-6).
inline float sinTurns (float p)
{
    float q = p - 0.5f; // sin(2 pi p) = -sin(2 pi q)
    q = q > 0.25f ? 0.5f - q : (q < -0.25f ? -0.5f - q : q);
    const float x = 6.28318531f * q, x2 = x * x;
    return -x * (1.0f + x2 * (-0.166666667f + x2 * (0.00833333333f + x2 * (-0.000198412698f + x2 * 2.75573192e-6f))));
}

// The trapezoidal one-pole's gain G = g / (1 + g), g = tan(x), x in [0, 1.26] (the cutoff's angle a sample): tan as
// a Pade (3,2), folded in so it costs one division.
inline float onePoleGain (float x)
{
    const float x2 = x * x, top = x * (15.0f - x2);
    return top / (15.0f - 6.0f * x2 + top);
}

inline int parity (uint32_t x) { return __builtin_parity (x); }

// --- LFO ------------------------------------------------------------------------------------------------------------
// Its place is a function of the time (frames since prepare): the phase it had at an anchor plus the frames since
// times its rate. So it costs nothing while nothing sounds, and the same frames give the same place whatever the
// host's block size. A new rate, a retrigger, or the song's place (Clock synced, Mode Free) sets a new anchor.
struct Lfo
{
    double phase0 = 0.0;  // its place at the anchor, in cycles
    int64_t t0 = 0;       // the anchor's frame
    double inc = 0.0;     // cycles a frame
    bool once = false;    // One Shot: stops at the end of the cycle
    uint8_t wave = 2;

    double phaseAt (int64_t t) const
    {
        const double p = phase0 + (double) (t - t0) * inc;
        if (once)
            return p < 1.0 ? p : 1.0;
        return p - std::floor (p);
    }
    void anchor (int64_t t, double phase) { phase0 = phase, t0 = t; }
    void setRate (int64_t t, double newInc)
    {
        if (newInc == inc)
            return;
        const double p = phaseAt (t);
        inc = newInc;
        anchor (t, p);
    }

    // The waves, -1..1, each starting where the manual draws it: at the end of a cycle (phase 1) a One Shot holds
    // the value the cycle ends on.
    static float shape (uint8_t wave, float p)
    {
        static constexpr float widths[8] = { 0.60f, 0.70f, 0.80f, 0.88f, 0.40f, 0.30f, 0.20f, 0.12f };
        switch (wave)
        {
            case 0: return 2.0f * p - 1.0f;                        // Saw Up
            case 1: return 1.0f - 2.0f * p;                        // Saw Down
            case 2: return sinTurns (p);                           // Sine
            case 3: return -sinTurns (p);                          // Sine -
            case 4: return p < 0.5f ? 4.0f * p - 1.0f : 3.0f - 4.0f * p; // Triangle /\ from the bottom
            case 5: return p < 0.5f ? 1.0f - 4.0f * p : 4.0f * p - 3.0f; // Triangle - \/
            case 6: return p < 0.5f ? 1.0f : -1.0f;                // Square: the top first
            case 7: return p < 0.5f ? -1.0f : 1.0f;                // Square -
            default: return p < widths[(wave - 8) & 7] ? 1.0f : -1.0f; // Pulse NN: NN % of the cycle on top
        }
    }
    float valueAt (int64_t t) const { return shape (wave, (float) phaseAt (t)); }
};

// --- Metal noise ----------------------------------------------------------------------------------------------------
// Metal Noise 1: Noise is white noise; 2-499 switch on, by the number's bits, nine square waves at unrelated high
// frequencies, combined by exclusive-or (each bit pattern periodic, together aperiodic: metallic, noise-like).
// Metal Noise 2: a 16-bit counter clocked at 20 kHz, its output the parity of (counter AND the number's bits), where
// Noise 2 Mask M01-M16 keeps that many of the counter's low bits: periodic, tone-like patterns, very high with a small
// mask, long with a large one. The number has nine bits: up to M09 the mask cuts it (a number with no bit inside the
// mask is silent; numbers differing only above it sound alike), from M10 it moves up into the mask's top bits, so
// each wider mask reaches slower counter bits (M16: up to 2^16 ticks, 3.3 s). Each edge is band-limited (a polynomial
// BLEP), so the output runs a frame behind the rest of the attack.
constexpr float metalHz[9] = { 587.3f, 845.1f, 1173.7f, 1637.9f, 2245.6f, 3091.2f, 4216.4f, 5803.9f, 7951.3f };
constexpr float metal2ClockHz = 20000.0f;

struct MetalNoise
{
    float phase[9] {};     // the squares' phases; bit i of `state` is square i's level (1: high)
    float dt[9] {};
    uint32_t state = 0;
    float clockPhase = 0.0f, clockDt = 0.0f;
    uint32_t counter = 0;
    uint32_t noiseSeed = 0x2545f491u;
    float held1 = 0.0f, held2 = 0.0f; // the samples still being built (emitted at the next frame)
    float last1 = 0.0f, last2 = 0.0f; // the last naive values (for a change of number)

    void prepare (float rate)
    {
        // Held under half a cycle (and the counter under a tick) a frame, which only bites below 20 kHz.
        for (int i = 0; i < 9; ++i)
            dt[i] = std::min (metalHz[i] / rate, 0.45f);
        clockDt = std::min (metal2ClockHz / rate, 1.0f);
        restart();
    }
    // Where the generators start (activation, and the host's reset).
    void restart()
    {
        state = 0;
        for (int i = 0; i < 9; ++i)
        {
            phase[i] = 0.1f * (float) i;
            state |= (phase[i] >= 0.5f ? 1u : 0u) << i;
        }
        clockPhase = 0.0f;
        counter = 0;
        noiseSeed = 0x2545f491u;
        restartEdges();
    }
    // A new attack: the band-limiting starts afresh (the generators run on).
    void restartEdges() { held1 = held2 = last1 = last2 = 0.0f; }

    static void blep (float h, float d, float& before, float& now)
    {
        before += 0.5f * h * d * d;
        now -= 0.5f * h * (1.0f - d) * (1.0f - d);
    }

    // The counter bits Metal Noise 2 reads, for its number (0 Off) and Noise 2 Mask (1-16).
    static uint32_t selection (int number, int maskWidth)
    {
        const uint32_t bits = (1u << maskWidth) - 1u, n = (uint32_t) number;
        return (maskWidth > 9 ? n << (maskWidth - 9) : n) & bits;
    }

    // One frame of both generators, summed; n1 is Metal Noise 1's number (0 Off, 1 Noise), sel the counter bits Metal
    // Noise 2 reads (0: off or silent).
    float next (int n1, uint32_t sel)
    {
        // Metal Noise 1.
        float before1 = 0.0f, now1 = 0.0f, naive1;
        if (n1 >= 2)
        {
            // The squares that change this frame, earliest first (at most one edge each: the fastest is under half
            // a cycle a frame).
            float when[9];
            uint8_t which[9];
            int flips = 0;
            for (int i = 0; i < 9; ++i)
            {
                const float from = phase[i];
                float to = from + dt[i];
                float d = -1.0f;
                if (from < 0.5f && to >= 0.5f)
                    d = (to - 0.5f) / dt[i];
                else if (to >= 1.0f)
                    d = (to - 1.0f) / dt[i];
                if (to >= 1.0f)
                    to -= 1.0f;
                phase[i] = to;
                if (d >= 0.0f)
                {
                    int k = flips++;
                    for (; k > 0 && when[k - 1] < d; --k) // larger d: longer ago
                        when[k] = when[k - 1], which[k] = which[k - 1];
                    when[k] = d, which[k] = (uint8_t) i;
                }
            }
            const uint32_t bits = (uint32_t) n1;
            float level = parity (state & bits) ? 1.0f : -1.0f;
            if (level != last1) // the number changed since the last frame: a step at its start
                blep (level - last1, 1.0f, before1, now1);
            for (int k = 0; k < flips; ++k)
            {
                state ^= 1u << which[k];
                if (bits & (1u << which[k]))
                    blep (-2.0f * level, when[k], before1, now1), level = -level;
            }
            naive1 = level;
        }
        else
        {
            for (int i = 0; i < 9; ++i) // the squares run on underneath
            {
                float to = phase[i] + dt[i];
                if (to >= 1.0f)
                    to -= 1.0f;
                if ((phase[i] < 0.5f) != (to < 0.5f))
                    state ^= 1u << i;
                phase[i] = to;
            }
            if (n1 == 1)
            {
                noiseSeed ^= noiseSeed << 13, noiseSeed ^= noiseSeed >> 17, noiseSeed ^= noiseSeed << 5;
                naive1 = (float) (int32_t) noiseSeed * (1.0f / 2147483648.0f);
            }
            else
                naive1 = 0.0f;
        }
        last1 = naive1;
        const float out1 = held1 + before1;
        held1 = naive1 + now1;

        // Metal Noise 2.
        float before2 = 0.0f, now2 = 0.0f;
        float to = clockPhase + clockDt;
        float tickAgo = -1.0f;
        if (to >= 1.0f)
            to -= 1.0f, tickAgo = to / clockDt;
        clockPhase = to;
        const float startLevel = sel == 0 ? 0.0f : (parity (counter & sel) ? 1.0f : -1.0f);
        if (startLevel != last2)
            blep (startLevel - last2, 1.0f, before2, now2);
        float naive2 = startLevel;
        if (tickAgo >= 0.0f)
        {
            counter = (counter + 1) & 0xffffu;
            naive2 = sel == 0 ? 0.0f : (parity (counter & sel) ? 1.0f : -1.0f);
            if (naive2 != startLevel)
                blep (naive2 - startLevel, tickAgo, before2, now2);
        }
        last2 = naive2;
        const float out2 = held2 + before2;
        held2 = naive2 + now2;
        return 0.8f * (out1 + out2);
    }
};

// --- Engine -------------------------------------------------------------------------------------------------------
// A jump in the song's place larger than this (beats) is a jump; smaller is CLAP's rounding of the position.
constexpr float ln1000 = 6.90775528f; // a fall of 60 dB in natural-log units: Decay's time
constexpr double jumpSlack = 1.0e-3;

class Engine
{
public:
    void prepare (double hostRate, uint32_t)
    {
        rate = hostRate;
        const float r = (float) hostRate;
        invRate = 1.0f / r;
        pitchFall = std::exp (-1.0f / (tuneDecaySeconds * r));
        gateRise = 1.0f - std::exp (-1.0f / (25.0e-6f * r));            // the gate's edges: about 25 us
        couplingHp = 1.0f - std::exp (-6.28318531f * 150.0f / r);        // the pulse's coupling high pass: 150 Hz
        metal.prepare (r);
        reset();
    }

    void reset()
    {
        phase = 0.0f;
        pitchEnv = vcaEnv = 0.0f;
        gateLeft = 0;
        gate = coupling = eqState = 0.0f;
        attacking = false;
        sounding = false;
        metal.restart();
        now = 0;
        hitFrame = -1;
        for (auto& l : lfo)
            l = Lfo {};
        playing = false;
        expectBeats = -1.0;
        songBeats0 = 0.0;
        songT0 = 0;
        bpm = 120.0;
        lastClock[0] = lastClock[1] = lastMode[0] = lastMode[1] = -1;
        dirty = true;
        retarget();
        for (int i = 0; i < smoothed; ++i)
            smooth[i] = target[i];
        moving = false;
        for (int i = 0; i < 2; ++i)
            if (knob[lfoId (i, lfoModeOf)] >= 1.5f) // a One Shot that has not been started holds its end
                lfo[i].anchor (0, 1.0);
    }

    // A parameter's value, from the frame the next render starts.
    void set (uint32_t id, float v)
    {
        if (id >= ParamId::count || knob[id] == v)
            return;
        knob[id] = v;
        dirty = true;
    }

    void press (uint32_t id)
    {
        if (id == ParamId::play)
            trigger (1.0f, 0.0f, -1); // the panel's Play: full velocity, the pitch as set
    }

    // All sound off (MIDI CC 120, or a choke): the kick stops on this frame (a hit after it on the frame plays).
    void silence()
    {
        sounding = attacking = false;
        vcaEnv = pitchEnv = gate = coupling = eqState = 0.0f;
        gateLeft = 0;
        hitFrame = -1;
    }
    // A choke stops the hit its key played (key -1: whatever plays).
    void choke (int key)
    {
        if (key < 0 || key == hitKey)
            silence();
    }

    // A MIDI note: plays if Split Mode takes its key, the pitch counted from the range's lowest key.
    void note (int key, float velocity)
    {
        const int split = std::min (std::max ((int) knob[ParamId::splitMode], 0), 6);
        const KeyRange range = splitRanges[split];
        if (key < range.low || key > range.high)
            return;
        const int n = split == 0 ? 0 : key - range.low;
        const float octaves = knob[ParamId::pitchMode] >= 0.5f ? (float) n / 12.0f
                                                                : std::log2 (1.0f + (float) n * linearKeyStep); // a semitone at the bottom, then equal Hz
        trigger (clampf (velocity, 0.0f, 1.0f), octaves, key);
    }

    // The host's transport for the block about to play: where the song is (beats) and its tempo. The LFOs whose
    // Clock follows the tempo use it; one running Free keeps in step with the song's beats from Play and after a jump.
    void transport (bool isPlaying, bool placed, double beats, double tempo, uint32_t frames)
    {
        const bool started = isPlaying && placed && (! playing || expectBeats < 0.0 || std::fabs (beats - expectBeats) > jumpSlack);
        playing = isPlaying && placed;
        const bool newTempo = tempo != bpm;
        if (newTempo)
            bpm = tempo, dirty = true; // the synced LFOs' rates, at the next tick
        // The song's place is counted on from Play, a jump or a new tempo, not from each block's (rounded) start, so
        // it is the same at a frame whatever the block size.
        if (started || (playing && newTempo))
        {
            songBeats0 = beats, songT0 = now;
            for (int i = 0; i < 2; ++i) // a new tempo too: in step from this frame, not from the next tick
                alignToSong (i);
        }
        expectBeats = playing ? beats + (double) frames * bpm / 60.0 / rate : -1.0;
    }

    void render (float* left, float* right, uint32_t frames)
    {
        uint32_t i = 0;
        while (i < frames)
        {
            if (! sounding)
            {
                // Nothing sounds: the rest of this run is silence. The knobs need no easing while nothing plays.
                if (dirty)
                    retarget();
                for (int k = 0; k < smoothed; ++k)
                    smooth[k] = target[k];
                moving = false;
                updateEq();
                const uint32_t n = frames - i;
                std::memset (left + i, 0, sizeof (float) * n);
                if (right)
                    std::memset (right + i, 0, sizeof (float) * n);
                now += n;
                return;
            }
            if ((now & 7) == 0) // every 8 frames of the time (not of the block), whatever the block size
            {
                if (dirty)
                    retarget();
                if (moving)
                {
                    // About 1 ms to arrive at 48 kHz: no zipper noise, no lag a hand would notice.
                    float far = 0.0f;
                    for (int k = 0; k < smoothed; ++k)
                    {
                        smooth[k] += (target[k] - smooth[k]) * 0.25f;
                        far = std::max (far, std::fabs (target[k] - smooth[k]));
                    }
                    if (far < 1.0e-5f)
                    {
                        for (int k = 0; k < smoothed; ++k)
                            smooth[k] = target[k];
                        moving = false;
                    }
                    updateEq(); // only the eased EQ moves it
                }
            }
            const float y = voice();
            left[i] = y;
            if (right)
                right[i] = y;
            ++i;
            ++now;
        }
    }

    uint32_t hitCount() const { return hits; }

private:
    uint32_t hits = 0; // screen notification; counts only kicks that pass the note filter, one a frame
    enum Smoothed { pitchKnobS, harmonicsS, eqKnobS, compressionS, gainS, lfo1IntS, lfo2IntS, smoothed };
    enum LfoParam { lfoWaveOf, lfoSpeedOf, lfoIntOf, lfoDestOf, lfoModeOf, lfoClockOf };
    static uint32_t lfoId (int which, LfoParam p) { return (which == 0 ? ParamId::lfo1Wave : ParamId::lfo2Wave) + (uint32_t) p; }

    void updateEq() { eqGain = onePoleGain (std::min (exp2Fast (eqSpanOctaves * smooth[eqKnobS]) * eqOpenHz, 0.4f * (float) rate) * 3.14159265f * invRate); }

    // The CV inputs, summed into each destination: CV n In (0-5 V) times CV n Amt (-128..127, over 128), so 5 V at
    // +127 adds nearly the whole of the destination's range.
    void sumCvs()
    {
        for (float& c : cv)
            c = 0.0f;
        for (int n = 0; n < 4; ++n)
        {
            const uint32_t base = ParamId::cv1Dest + 3u * (uint32_t) n;
            const int dest = (int) knob[base];
            if (dest > CvDest::off && dest < CvDest::count)
                cv[dest] += knob[base + 2] / cvVoltsMax * (knob[base + 1] / 128.0f);
        }
    }
    float withCv (uint32_t id, int dest) const { return clampf (knob[id] + cv[dest], 0.0f, 1.0f); }

    // The knobs' new targets (and the LFOs' rates), after a change.
    void retarget()
    {
        sumCvs();
        target[pitchKnobS] = withCv (ParamId::pitch, CvDest::pitch);
        target[harmonicsS] = withCv (ParamId::harmonics, CvDest::harmonics);
        target[eqKnobS] = withCv (ParamId::eq, CvDest::eq);
        target[compressionS] = withCv (ParamId::compression, CvDest::compression);
        target[gainS] = knob[ParamId::volume] * knob[ParamId::volume];
        target[lfo1IntS] = withCv (ParamId::lfo1Int, CvDest::lfo1Int);
        target[lfo2IntS] = withCv (ParamId::lfo2Int, CvDest::lfo2Int);
        for (int i = 0; i < 2; ++i)
        {
            Lfo& l = lfo[i];
            l.wave = (uint8_t) clampf (knob[lfoId (i, lfoWaveOf)], 0.0f, 15.0f);
            const int clock = (int) clampf (knob[lfoId (i, lfoClockOf)], 0.0f, 10.0f);
            const int mode = (int) knob[lfoId (i, lfoModeOf)];
            if (l.once != (mode == 2))
            {
                // Into One Shot: the cycle under way finishes, then holds. Out of it: on from where it stopped.
                const double at = l.phaseAt (now);
                l.once = mode == 2;
                l.anchor (now, at >= 1.0 ? 0.0 : at);
            }
            const double inc = clock == 0 ? (double) lfoHz (withCv (lfoId (i, lfoSpeedOf), i == 0 ? CvDest::lfo1Rate : CvDest::lfo2Rate)) / rate
                                          : syncedInc (clock);
            l.setRate (now, inc);
            if (clock != lastClock[i] || mode != lastMode[i])
            {
                lastClock[i] = clock, lastMode[i] = mode;
                alignToSong (i);
            }
            lfoDestMetal[i] = knob[lfoId (i, lfoDestOf)] >= 0.5f;
        }
        dirty = false;
        moving = true;
    }

    // Cycles a frame for a Clock that follows the tempo (one expression, so a rate set here and in retarget() is the
    // same to the bit and setRate() sees no change).
    double syncedInc (int clock) const { return bpm / 60.0 / lfoClockBeats[clock] / rate; }

    // An LFO whose Clock follows the tempo and whose Mode is Free sits where the song's beats put it, while the host
    // plays: a cycle starts on each multiple of its note value from the song's top.
    void alignToSong (int i)
    {
        const int clock = (int) knob[lfoId (i, lfoClockOf)];
        if (! playing || clock <= 0 || clock > 10 || (int) knob[lfoId (i, lfoModeOf)] != 0)
            return;
        const double beats = songBeats0 + (double) (now - songT0) * bpm / 60.0 / rate;
        const double cycles = beats / lfoClockBeats[clock];
        lfo[i].inc = syncedInc (clock); // the tempo's rate from here, if it just changed
        lfo[i].anchor (now, cycles - std::floor (cycles));
    }

    // A hit's level: velocity as far as Dynamics lets it, the Accent CV, and how much a gate `ms` long charged the
    // envelope (a short gate puts less energy into it).
    float levelOf (float velocity, float ms) const
    {
        const float dynamics = knob[ParamId::dynamics];
        const float accent = clampf (1.0f + cv[CvDest::accent], 0.0f, 2.0f);
        const float charge = 0.6f + 0.4f * (1.0f - std::exp (-ms / 2.0f));
        return (1.0f - dynamics * (1.0f - velocity)) * accent * charge;
    }

    // A hit, from a note (its key) or Play (-1). Two on one frame (pads struck together, a pad over the sequence: a
    // host may give all of a block's notes its first frame) are one hit: the louder plays as if the other had not come,
    // its key, pitch, gate and level with it (equal, the later); nothing of the softer is taken.
    void trigger (float velocity, float keyOctaves, int key)
    {
        const float ms = gateMs (knob[ParamId::gateTime]);
        if (now == hitFrame)
        {
            if (dirty)
                sumCvs(); // the Accent CV as it is now (retarget() works it out again)
            if (levelOf (velocity, ms) < hitLevel)
                return;
            for (int i = 0; i < 2; ++i) // the LFOs as the hit it replaces found them: this one retriggers its own
                if (retriggered & (1u << i))
                    lfo[i] = lfoBefore[i];
        }
        else
            ++hits, hitFrame = now, hitWasSilent = ! sounding; // the screen counts one hit a frame
        if (dirty)
            retarget();
        hitKey = key;
        // What the hit takes as it is now.
        hitTune = withCv (ParamId::tune, CvDest::tune) * tuneOctavesMax;
        vcaFall = std::exp (-ln1000 / (decaySeconds (withCv (ParamId::decay, CvDest::decay)) * (float) rate));
        hitPulse = withCv (ParamId::pulse, CvDest::pulse);
        hitNoise = withCv (ParamId::noise, CvDest::noise);
        hitAttack = withCv (ParamId::attack, CvDest::attack);
        metal1 = (int) std::lrint (clampf (knob[ParamId::metalNoise1] + cv[CvDest::metal1] * (float) metalPatterns, 0.0f, (float) metalPatterns));
        metal2 = (int) std::lrint (clampf (knob[ParamId::metalNoise2] + cv[CvDest::metal2] * (float) metalPatterns, 0.0f, (float) metalPatterns));
        metal2Bits = MetalNoise::selection (metal2, (int) clampf (knob[ParamId::noise2Mask], 1.0f, 16.0f));
        gateLeft = std::max<int32_t> (1, (int32_t) std::lrint (ms * 0.001 * rate));
        hitLevel = levelOf (velocity, ms);
        keyOct = keyOctaves;

        phase = 0.0f;
        pitchEnv = 1.0f;
        vcaEnv = 1.0f;
        if (! attacking)
            metal.restartEdges();
        attacking = true;
        sounding = true;
        retriggered = 0;
        for (int i = 0; i < 2; ++i)
            if ((int) knob[lfoId (i, lfoModeOf)] != 0)
                lfoBefore[i] = lfo[i], retriggered |= 1u << i, lfo[i].anchor (now, 0.0);
        if (hitWasSilent) // nothing was sounding: the knobs are where they are, from the hit's first frame
        {
            for (int k = 0; k < smoothed; ++k)
                smooth[k] = target[k];
            moving = false;
            updateEq();
        }
    }

    [[gnu::always_inline]] float voice()
    {
        // The LFOs.
        float lfoPitch = 0.0f, lfoMetal = 0.0f;
        for (int i = 0; i < 2; ++i)
        {
            const float depth = smooth[i == 0 ? lfo1IntS : lfo2IntS];
            if (depth <= 0.0f)
                continue;
            const float v = lfo[i].valueAt (now) * depth;
            if (lfoDestMetal[i]) lfoMetal += v;
            else lfoPitch += v;
        }

        // The VCO: Pitch, the key, the Tune envelope and the LFOs, in octaves above pitchLowHz.
        const float octaves = smooth[pitchKnobS] * pitchSpanOctaves + keyOct + hitTune * pitchEnv + lfoPitch * lfoPitchOctaves;
        const float dt = std::min (exp2Fast (octaves) * pitchLowHz * invRate, maxDt);
        pitchEnv *= pitchFall;
        float p = phase + dt;
        if (p >= 1.0f)
            p -= 1.0f;
        phase = p;
        const float sine = sinTurns (p);
        float q = p + 0.25f; // the cusped wave, its fundamental in phase with the sine: kinks at 3/4 of the cycle
        if (q >= 1.0f)
            q -= 1.0f;
        const float c = 2.0f * q - 1.0f;
        const float cusp = 2.46740110f * (0.333333333f - c * c);
        const float body = sine + smooth[harmonicsS] * (cusp - sine);

        // The VCA: held at its peak for the gate, then Decay; Compression takes a threshold off the falling
        // envelope (the decay grows quieter and ends sooner; at the top only a click is left).
        const bool gateOpen = gateLeft > 0; // this frame is one of the gate's (the attack below hears the same)
        if (gateOpen)
            --gateLeft;
        else
            vcaEnv *= vcaFall;
        // Compression takes 0.9 x its value off the envelope and scales back up; at 0 (the usual) there is nothing to
        // take, and no division. Both multiply-adds are fused, as the compiler fused them before this was spelled out.
        const float squeeze = smooth[compressionS];
        float amp = vcaEnv;
        if (squeeze != 0.0f)
            amp = std::fma (-0.9f, squeeze, vcaEnv) / std::fma (-0.9f, squeeze, 1.0f);
        if (amp <= 3.0e-5f)
            amp = 0.0f, vcaEnv = 0.0f;
        float y = body * amp * hitLevel;

        // The attack: the gate's pulse and the noise during it, mixed by Attack.
        if (attacking)
        {
            gate += ((gateOpen ? 1.0f : 0.0f) - gate) * gateRise;
            coupling += (gate - coupling) * couplingHp;
            const int n1 = lfoMetal != 0.0f && metal1 >= 1
                               ? (int) std::lrint (clampf ((float) metal1 + lfoMetal * lfoMetalPatterns, 1.0f, (float) metalPatterns))
                               : metal1;
            const float noise = metal.next (n1, metal2Bits);
            y += hitAttack * (hitPulse * (gate - coupling) + hitNoise * noise * gate) * hitLevel;
            if (gateLeft == 0 && gate < 1.0e-6f && std::fabs (coupling) < 1.0e-6f)
                attacking = false, gate = coupling = 0.0f;
        }

        // EQ, Volume and a soft ceiling.
        const float v = (y - eqState) * eqGain;
        const float lp = v + eqState;
        eqState = lp + v;
        if (vcaEnv == 0.0f && ! attacking && std::fabs (eqState) < 1.0e-7f)
            sounding = false, eqState = 0.0f;
        // tanhFast tops out at exactly 1 (at 3); its multiply-adds can round a step past it there, so the last word
        // is +-1 (two instructions; nothing inside the range moves).
        return clampf (tanhFast (1.5f * lp * smooth[gainS]), -1.0f, 1.0f);
    }

    double rate = 48000.0;
    float invRate = 1.0f / 48000.0f;
    float pitchFall = 0.0f, gateRise = 0.0f, couplingHp = 0.0f;
    static constexpr float maxDt = 0.45f; // the VCO's step a frame at most: under Nyquist

    float knob[ParamId::count] {};
    float cv[CvDest::count] {};
    float target[smoothed] {}, smooth[smoothed] {};
    float eqGain = 0.5f;
    bool dirty = true, moving = false;

    // The hit.
    float hitTune = 0.0f, vcaFall = 0.0f, hitPulse = 0.0f, hitNoise = 0.0f, hitAttack = 0.0f, hitLevel = 0.0f, keyOct = 0.0f;
    int metal1 = 1, metal2 = 0;
    uint32_t metal2Bits = 0;
    int hitKey = -1; // the key that played the hit (-1: Play), for a choke
    int32_t gateLeft = 0;
    int64_t hitFrame = -1;       // the frame the hit started on (-1: none since a silence), for another on it
    bool hitWasSilent = false;   // nothing sounded before it
    uint32_t retriggered = 0;    // the LFOs it retriggered (bit i: LFO i + 1), and their places before
    Lfo lfoBefore[2];

    // The voice.
    float phase = 0.0f, pitchEnv = 0.0f, vcaEnv = 0.0f;
    float gate = 0.0f, coupling = 0.0f, eqState = 0.0f;
    bool attacking = false, sounding = false;
    MetalNoise metal;

    // Time and the LFOs.
    int64_t now = 0;
    Lfo lfo[2];
    bool lfoDestMetal[2] {};
    int lastClock[2] { -1, -1 }, lastMode[2] { -1, -1 };
    bool playing = false;
    double expectBeats = -1.0, songBeats0 = 0.0, bpm = 120.0;
    int64_t songT0 = 0;
};
} // namespace moonbass
