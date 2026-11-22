// Necronomicon's sound and sequencers. One voice, run at twice the host's rate and filtered back down:
//
//   VCO 1 ──► its SUB 1 and SUB 2 (VCO 1's frequency divided by 1 to 16)      the same for VCO 2
//   VCO 1, its subs, VCO 2, its subs ──► mixer (six levels; overdrives when hot)
//     ──► 4-pole ladder low pass (VCF EG moves the cutoff) ──► VCA (VCA EG) ──► Volume ──► out
//
//   4 rhythm generators (the clock divided by 1 to 16) ──OR / XOR──► SEQ 1 and SEQ 2 (4 steps each)
//   SEQ 1 ──► VCO 1's pitch and/or its subs' divisors; SEQ 2 the same for VCO 2
//   every step of either sequencer ──► both EGs (EG On)
//
// The clock is the host's: one every Clock Div from the song's top while the host plays. Rhythm generator n fires
// on every Rhythm n-th clock from the top (or from Reset), so all four line up on the downbeat.
//
// Cost: no allocation, no locks; the knobs are worked out every 8 frames, the oscillators and filter only while the
// VCA is open.
#pragma once

#include "params.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace necronomicon
{
// --- Fast math ------------------------------------------------------------------------------------------------

// Two branch-free instructions (fmaxnm, fminnm); a NaN comes out as `lo`.
inline float clampf (float x, float lo, float hi) { return std::fmin (std::fmax (x, lo), hi); }

// 2^x to about 3e-6 relative (0.005 cent): the integer part by rounding, so the polynomial only spans [-0.5, 0.5]
// and two octaves meet without a step. For pitch and cutoff, whose octaves the parameter ranges keep within
// [-30, 30] (no clamp here); envelope rates use exp (they sit so near 1 that this error would change a decay).
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

// The trapezoidal one-pole's gain G = g / (1 + g), g = tan(x), x in [0, 1.2] (the cutoff's angle a sample): tan as
// a Pade (3,2), under 0.5 % off at the top, folded in so it costs one division.
inline float onePoleGain (float x)
{
    const float x2 = x * x, top = x * (15.0f - x2);
    return top / (15.0f - 6.0f * x2 + top);
}

// --- Quantize -----------------------------------------------------------------------------------------------------
// The four scales, as octave fractions from C (both ends included). 8-ET and 8-JI are the eight steps of a major
// scale, C to C; 12-JI is the 5-limit chromatic scale (1/1 16/15 9/8 6/5 5/4 4/3 45/32 3/2 8/5 5/3 16/9 15/8 2/1).
inline constexpr float scale8Et[] = { 0.0f, 2.0f / 12.0f, 4.0f / 12.0f, 5.0f / 12.0f, 7.0f / 12.0f, 9.0f / 12.0f, 11.0f / 12.0f, 1.0f };
inline constexpr float scale12Ji[] = { 0.000000000f, 0.093109404f, 0.169925001f, 0.263034406f, 0.321928095f, 0.415037499f, 0.491853096f,
                                       0.584962501f, 0.678071905f, 0.736965594f, 0.830074999f, 0.906890596f, 1.000000000f };
inline constexpr float scale8Ji[] = { 0.000000000f, 0.169925001f, 0.321928095f, 0.415037499f, 0.584962501f, 0.736965594f, 0.906890596f, 1.000000000f };

// A pitch in octaves above middle C, moved to the nearest note of the scale (Quantize: 0 Off, 1 12-ET, 2 8-ET,
// 3 12-JI, 4 8-JI). The scales' root is C.
inline float quantizePitch (float octaves, int mode)
{
    if (mode <= 0)
        return octaves;
    if (mode == 1)
        return std::round (octaves * 12.0f) / 12.0f;
    const float* table = mode == 2 ? scale8Et : mode == 3 ? scale12Ji : scale8Ji;
    const int n = mode == 3 ? 13 : 8;
    const float whole = std::floor (octaves), part = octaves - whole;
    float best = table[0];
    for (int i = 1; i < n; ++i)
        if (std::fabs (table[i] - part) < std::fabs (best - part))
            best = table[i];
    return whole + best;
}

// --- Rhythm generators --------------------------------------------------------------------------------------------
// The generators routed to a sequencer, as their divisors (up to four). Clock n (n = 0 at the song's top or at Reset)
// is a pulse of generator d when d divides n. The sequencer advances on a clock when OR: any of its generators
// pulses; XOR: exactly one does (two together cancel).
struct Routing
{
    int divisor[4];
    int count = 0;
    bool exclusive = false; // XOR
};

inline bool pulseAt (const Routing& r, int64_t n)
{
    int hits = 0;
    for (int i = 0; i < r.count; ++i)
        hits += n % r.divisor[i] == 0;
    return r.exclusive ? hits == 1 : hits > 0;
}

// How many of the clocks 0 .. last advance the sequencer (0 for last < 0), counted by inclusion and exclusion over
// the generators' common multiples rather than clock by clock: a jump to anywhere in a song costs the same.
inline int64_t pulsesUpTo (const Routing& r, int64_t last)
{
    if (last < 0 || r.count == 0)
        return 0;
    int64_t total = 0;
    for (int mask = 1; mask < (1 << r.count); ++mask)
    {
        int64_t lcm = 1;
        int bits = 0;
        for (int i = 0; i < r.count; ++i)
            if (mask & (1 << i))
            {
                int64_t a = lcm, b = r.divisor[i];
                while (b != 0)
                {
                    const int64_t t = a % b;
                    a = b, b = t;
                }
                lcm = lcm / a * r.divisor[i];
                ++bits;
            }
        const int64_t multiples = last / lcm + 1; // 0 is one of them
        // OR: the union; XOR: those in exactly one set, sum over k of (-1)^(k+1) k S_k.
        total += ((bits & 1) ? 1 : -1) * (r.exclusive ? bits : 1) * multiples;
    }
    return total;
}

// --- Envelope -----------------------------------------------------------------------------------------------------
// The panel's AD envelopes. A trigger charges the envelope from wherever it is to the top, as a capacitor through a
// resistor would (aimed past the top and stopped at it), then it decays exponentially. A trigger during the attack
// is ignored (the panel's EGs restart only once the attack is done). While `gate` is held (a MIDI note, EG: Hold)
// the envelope stays at the top, and decays when the gate ends.
struct Envelope
{
    enum Stage : uint8_t { rest, rising, top, falling };
    float value = 0.0f;
    float attack = 1.0f; // charge a sample
    float decay = 0.0f;  // multiplier a sample
    Stage stage = rest;
    bool gate = false;

    void trigger()
    {
        if (stage != rising)
            stage = rising;
    }
    bool resting() const { return stage == rest; }
    float next()
    {
        switch (stage)
        {
            case rising:
                value += (1.25f - value) * attack;
                if (value >= 1.0f)
                    value = 1.0f, stage = gate ? top : falling;
                break;
            case top:
                if (! gate)
                    stage = falling;
                break;
            case falling:
                value *= decay;
                if (value < 1.6e-5f) // -96 dB, a 16-bit step: done, and exactly zero so the VCA can close
                    value = 0.0f, stage = rest;
                break;
            case rest: break;
        }
        return value;
    }
};

// --- Oscillators --------------------------------------------------------------------------------------------------
// A VCO and its two subharmonics. A sub counts the VCO's cycles: its phase is (cycles counted + the VCO's phase) /
// divisor, so it stays locked to the VCO whatever the VCO does, and every one of its jumps falls on a VCO cycle's
// start (or, for a square, on its own half). A new divisor is taken at the VCO's next cycle.
// Square, pulse and sawtooth are band-limited with polynomial BLEP residuals over two samples; the mix runs one
// sample behind so an edge can correct the sample before it too.
struct Vco
{
    float phase = 0.0f;
    int count[2] {};       // the VCO's cycles into each sub's cycle
    int divisor[2] { 1, 1 };
    int wanted[2] { 1, 1 }; // a divisor to take at the next cycle
    float inv[2] { 1.0f, 1.0f };
    float width = 0.5f;     // the pulse's width as the last sample ended (it moves with SUB 1 in the middle position)

    void takeDivisors()
    {
        for (int k = 0; k < 2; ++k)
        {
            divisor[k] = wanted[k];
            inv[k] = 1.0f / (float) divisor[k];
            if (count[k] >= divisor[k])
                count[k] = 0;
        }
    }
};

// A step of `h` `d` samples before now: shares of it for the held sample and for the current one.
[[gnu::always_inline]] inline void blep (float h, float d, float& before, float& now)
{
    before += 0.5f * h * d * d;
    now -= 0.5f * h * (1.0f - d) * (1.0f - d);
}

// One sample of a VCO and its subs at increment dt (cycles a sample): returns the sum of the three outputs at their
// levels (level[0] the VCO, level[1] and [2] its subs) and adds their edges' corrections to `before` and `now`.
// wave: 0 all square; 1 the VCO a pulse whose width SUB 1's sawtooth sweeps, the subs sawtooth; 2 all sawtooth.
[[gnu::always_inline]] inline float stepVco (Vco& o, int wave, float dt, const float* level, float& before, float& now)
{
    const float p0 = o.phase;
    float p1 = p0 + dt;
    const bool wrapped = p1 >= 1.0f;
    float ago = 0.0f; // samples since the VCO's cycle began, when one began this sample
    if (wrapped)
        p1 -= 1.0f, ago = p1 / dt;
    o.phase = p1;
    const bool subSquare = wave == 0;
    float out = 0.0f, saw1 = 0.0f;
    for (int k = 0; k < 2; ++k)
    {
        const float L = level[k + 1];
        const float inv = o.inv[k];
        const float s0 = ((float) o.count[k] + p0) * inv;
        float s1;
        if (wrapped)
        {
            // Up to the VCO's new cycle, then on from it (a new divisor, or the sub's own new cycle, may jump).
            const float end = (float) (o.count[k] + 1) * inv; // the sub's phase as the VCO's cycle ends (1: its own ends)
            int c = o.count[k] + 1;
            if (o.wanted[k] != o.divisor[k])
                o.divisor[k] = o.wanted[k], o.inv[k] = 1.0f / (float) o.divisor[k];
            if (c >= o.divisor[k])
                c = 0;
            o.count[k] = c;
            const float inv2 = o.inv[k];
            const float start = (float) c * inv2;
            s1 = ((float) c + p1) * inv2;
            if (L != 0.0f)
            {
                if (subSquare)
                {
                    const float wasLevel = (end <= 0.5f) ? 1.0f : -1.0f, isLevel = start < 0.5f ? 1.0f : -1.0f;
                    if (s0 < 0.5f && end > 0.5f)
                        blep (-2.0f * L, (end - 0.5f) / (dt * inv) + ago, before, now);
                    if (isLevel != wasLevel)
                        blep ((isLevel - wasLevel) * L, ago, before, now);
                    if (start < 0.5f && s1 >= 0.5f)
                        blep (-2.0f * L, (s1 - 0.5f) / (dt * inv2), before, now);
                }
                else if (start != end)
                    blep (2.0f * (start - end) * L, ago, before, now);
            }
        }
        else
        {
            s1 = ((float) o.count[k] + p1) * inv;
            if (L != 0.0f && subSquare && s0 < 0.5f && s1 >= 0.5f)
                blep (-2.0f * L, (s1 - 0.5f) / (dt * inv), before, now);
        }
        if (k == 0)
            saw1 = 2.0f * s1 - 1.0f;
        if (L != 0.0f)
            out += L * (subSquare ? (s1 < 0.5f ? 1.0f : -1.0f) : 2.0f * s1 - 1.0f);
    }
    // Square, or the pulse: high from the cycle's start to the width, then low. The width moves (with SUB 1), so the
    // fall is where the phase passes it between the last sample's place and this one's, the two lines crossing.
    const float L = level[0];
    const float w0 = o.width, w1 = wave == 1 ? 0.5f + pwmDepth * saw1 : 0.5f;
    o.width = w1;
    if (L != 0.0f)
    {
        if (wave == 2)
        {
            if (wrapped)
                blep (-2.0f * L, ago, before, now);
            out += L * (2.0f * p1 - 1.0f);
        }
        else
        {
            if (wrapped)
            {
                // Low by the cycle's end (the width is at most 95 %), high again from its start: the fall before the
                // wrap if the last sample was still high, the rise at it, a fall after it if the new width is passed.
                if (p0 < w0)
                    blep (-2.0f * L, (p0 + dt - w0) / dt, before, now);
                blep (2.0f * L, ago, before, now);
                if (p1 >= w1)
                    blep (-2.0f * L, (p1 - w1) / dt, before, now);
            }
            else if (p0 < w0 && p1 >= w1)
                blep (-2.0f * L, (p1 - w1) / ((p1 - w1) - (p0 - w0)), before, now);
            out += L * (p1 < w1 ? 1.0f : -1.0f);
        }
    }
    return out;
}

// --- Ladder filter -----------------------------------------------------------------------------------------------
// Four one-pole stages in trapezoidal form with the feedback solved without delay, and one tanh on the input, which
// keeps self-oscillation bounded and gives the ladder's overdrive. The low pass is the fourth stage.
struct Ladder
{
    float s[4] {};

    float process (float x, float G, float k)
    {
        const float B = 1.0f - G;
        // y4 = G^4 u + S: S from the stages' states, u the stage input after the feedback.
        const float S = B * (s[3] + G * (s[2] + G * (s[1] + G * s[0])));
        const float G2 = G * G;
        const float u = tanhFast ((x - k * S) / (1.0f + k * G2 * G2));
        float y = u;
        for (int i = 0; i < 4; ++i)
        {
            const float v = (y - s[i]) * G;
            y = v + s[i];
            s[i] = y + v;
        }
        return y;
    }
};

// --- Half-band decimator ------------------------------------------------------------------------------------------
// From twice the host's rate to its rate: a 47-tap Kaiser-windowed half-band FIR. Its even taps are zero but the
// centre, so a frame costs 12 multiplies: flat to about 19 kHz at 48 kHz, about 80 dB down from 29 kHz.
//   y[m] = 0.5 a[m-11] + sum_i h(2i+1) (b[m-11+i] + b[m-12-i]),  a = x[2m], b = x[2m+1]
struct Decimator
{
    static constexpr int half = 12;
    float coeff[half] {};
    float odd[4 * half] {};  // the last 2*half b samples, written twice so a window reads straight
    float even[half] {};     // the last half a samples
    int at = 0, evenAt = 0, quiet = 0;

    void design()
    {
        const double beta = 8.0;
        auto bessel = [] (double x) {
            double sum = 1.0, term = 1.0;
            for (int k = 1; k < 40; ++k)
                term *= (x / (2.0 * k)) * (x / (2.0 * k)), sum += term;
            return sum;
        };
        const int edge = 4 * half - 1; // the outermost tap's distance is 2*half-1; the window spans +-(2*half)
        double total = 0.0, h[half];
        for (int i = 0; i < half; ++i)
        {
            const int k = 2 * i + 1;
            const double t = (double) k / (double) (edge + 1) * 2.0;
            const double w = bessel (beta * std::sqrt (std::max (0.0, 1.0 - t * t))) / bessel (beta);
            h[i] = std::sin (M_PI * k / 2.0) / (M_PI * k) * w;
            total += 2.0 * h[i];
        }
        for (int i = 0; i < half; ++i) // the odd taps sum to exactly 0.5: unity at DC
            coeff[i] = (float) (h[i] * 0.5 / total);
        clear();
    }
    void clear()
    {
        std::memset (odd, 0, sizeof (odd));
        std::memset (even, 0, sizeof (even));
        at = evenAt = 0;
        quiet = 0;
    }
    bool silent() const { return quiet >= 2 * half + 1; }
    float push (float a, float b)
    {
        constexpr int len = 2 * half;
        odd[at] = odd[at + len] = b;
        even[evenAt] = a;
        quiet = (a == 0.0f && b == 0.0f) ? std::min (quiet + 1, 2 * half + 1) : 0; // held at silent(): never overflows
        const float* w = odd + at + 1; // w[0] .. w[len-1]: b[m-len+1] .. b[m]
        float sum = 0.0f;
        for (int i = 0; i < half; ++i)
            sum += coeff[i] * (w[half + i] + w[half - 1 - i]);
        evenAt = evenAt + 1 == half ? 0 : evenAt + 1;
        const float centre = even[evenAt]; // the oldest: a[m-11]
        at = at + 1 == len ? 0 : at + 1;
        return sum + 0.5f * centre;
    }
};

// --- Engine -------------------------------------------------------------------------------------------------------
// A clock less than this many frames away is due now. CLAP gives the song's place in steps of 2^-31 beats, which
// at 48 kHz is about 1e-5 frames: a clock that falls on a block's first frame could otherwise come a frame late, by
// how the host's rounding fell for that block, so the sound would depend on the block size.
constexpr double clockSlack = 1.0e-3;

class Engine
{
public:
    void prepare (double hostRate, uint32_t)
    {
        rate = hostRate;
        inner = (float) (2.0 * hostRate);
        invInner = 1.0f / inner;
        maxDt = std::min (0.45f, vcoHighestHz * invInner);
        decimator.design();
        reset();
    }

    void reset()
    {
        vcfEg = vcaEg = Envelope {};
        vco[0] = vco[1] = Vco {};
        vco[1].phase = 0.37f; // the two VCOs never start in step
        filter = Ladder {};
        decimator.clear();
        mixHeld = 0.0f;
        dtFrom[0] = dtFrom[1] = gainFrom = std::nanf (""); // nothing worked out yet (and the rate may have changed)
        tick = 0;
        playing = regridWanted = resetWanted = false;
        lastClock = -1;
        rhythmPosition = 0;
        origin = 0;
        countedGrid = clockBeats();
        now = 0;
        clockedAt = -(int64_t (1) << 40); // no clock played yet
        clockedLoud = false;
        for (int s = 0; s < 2; ++s)
            step[s] = 0, fresh[s] = true;
        held = 0;
        transpose = 0.0f;
        gated = false;
        retarget();
        for (int i = 0; i < smoothed; ++i)
            smooth[i] = target[i];
        moving = false;
        vco[0].takeDivisors(), vco[1].takeDivisors();
        applyGate();
    }

    // A parameter's value, from the frame the next render starts.
    void set (uint32_t id, float v)
    {
        if (id >= ParamId::count || knob[id] == v)
            return;
        knob[id] = v;
        dirty = true;
        if (id == ParamId::clockDiv)
            regridWanted = true;
        if (id == ParamId::egMode)
            applyGate(); // Hold: both EGs to the top and there they stay; out of Hold, they decay
    }

    // Reset: both sequencers to step 1 and the rhythm generators to their start, from the next clock. Next: both
    // sequencers on a step, without the EGs. Trigger: both EGs, without moving (not in EG: Hold).
    void press (uint32_t id)
    {
        if (id == ParamId::reset)
        {
            for (int s = 0; s < 2; ++s)
                step[s] = 0, fresh[s] = true;
            rhythmPosition = 0;
            resetWanted = playing; // the generators count again from the next clock (on the grid it falls on)
            snapPitch();
        }
        else if (id == ParamId::next)
        {
            for (int s = 0; s < 2; ++s)
                step[s] = (step[s] + 1) & 3;
            snapPitch();
        }
        else if (egMode() != 2)
            vcfEg.trigger(), vcaEg.trigger();
    }

    // A MIDI note: both VCOs move by its distance from middle C (the panel's 1 V/octave input, normalled to both) and
    // stay there after it ends; the EGs start and hold at the top while any note is down. The last note held wins.
    void noteOn (int key)
    {
        noteOff (key);
        if (held == maxHeld)
            std::memmove (keys, keys + 1, sizeof (int) * (maxHeld - 1)), --held;
        keys[held++] = key;
        transpose = (float) (key - middleC);
        snapPitch();
        vcfEg.trigger(), vcaEg.trigger();
        applyGate();
    }
    void noteOff (int key)
    {
        int kept = 0;
        for (int i = 0; i < held; ++i)
            if (keys[i] != key)
                keys[kept++] = keys[i];
        if (kept == held)
            return;
        held = kept;
        if (held > 0 && transpose != (float) (keys[held - 1] - middleC))
        {
            transpose = (float) (keys[held - 1] - middleC);
            snapPitch();
        }
        applyGate();
    }
    void allNotesOff()
    {
        held = 0;
        applyGate();
    }

    // A single snapshot for the screen. Both sequencers may advance on the same clock.
    uint64_t screenPosition() const
    {
        return (uint64_t (rhythmPosition) << 4) | uint64_t ((step[1] << 2) | step[0]);
    }

    // Where the sequence's own count is: for a host that gives no song position (0 when it last stopped).
    double ownPosition() const { return playing ? position : 0.0; }

    // The host's transport at the start of a process call of `frames`.
    void transport (bool isPlaying, double beats, double bpm, uint32_t frames)
    {
        perFrame = bpm / 60.0 / rate;
        const double window = (double) std::max (frames, lastBlock); // how late a wrap inside a block can be seen
        lastBlock = frames;
        if (! isPlaying)
        {
            playing = false;
            return;
        }
        // A Play, or a place other than where the last block ended by more than a block (a jump, a loop going
        // round): line up again. Less than that is drift (a tempo change inside the last block), followed as it is:
        // the clocks already played are not played again, one passed is played at once.
        const double moved = std::fabs (beats - position);
        if (! playing)
            relocate (beats, window, false);
        else if (moved > 0.5 * perFrame && moved >= window * perFrame)
            relocate (beats, window, true);
        playing = true;
        position = beats;
    }

    void render (float* left, float* right, uint32_t frames)
    {
        while (frames > 0)
        {
            uint32_t run = frames;
            if (playing)
            {
                if (regridWanted)
                    regrid();
                const double next = (double) (lastClock + 1) * clockBeats();
                const double ahead = (next - position) / perFrame;
                if (ahead <= clockSlack)
                {
                    ++lastClock;
                    clock (lastClock);
                    continue;
                }
                const double untilClock = std::ceil (ahead - clockSlack);
                if (untilClock < (double) run)
                    run = (uint32_t) untilClock;
            }
            sound (left, right, run);
            now += run;
            if (playing)
                position += run * perFrame;
            left += run;
            if (right)
                right += run;
            frames -= run;
        }
    }

private:
    // --- Sequencers ---
    double clockBeats() const { return clockDivBeats[(int) knob[ParamId::clockDiv]]; }
    int egMode() const { return (int) knob[ParamId::egMode]; }

    // The generators routed to sequencer s, and how they combine.
    Routing routing (int s) const
    {
        Routing r;
        const uint32_t to = s == 0 ? ParamId::rhythm1Seq1 : ParamId::rhythm1Seq2;
        for (int g = 0; g < 4; ++g)
            if (knob[to + g] >= 0.5f)
                r.divisor[r.count++] = std::max (1, (int) knob[ParamId::rhythm1 + g]);
        r.exclusive = knob[ParamId::rhythmLogic] >= 0.5f;
        return r;
    }

    // Clock k from the song's top: each sequencer one of whose generators fires (by the logic) moves on a step, the
    // first after a Play, a jump or Reset playing the step it is on; then the EGs start (EG: On).
    void clock (int64_t k)
    {
        if (resetWanted)
            origin = k, resetWanted = false;
        clockedAt = (int64_t) now, clockedLoud = false;
        const int64_t n = k - origin;
        if (knob[ParamId::run] < 0.5f || n < 0)
            return;
        rhythmPosition = n;
        bool moved = false;
        for (int s = 0; s < 2; ++s)
            if (pulseAt (routing (s), n))
            {
                if (fresh[s])
                    fresh[s] = false;
                else
                    step[s] = (step[s] + 1) & 3;
                moved = true;
            }
        if (! moved)
            return;
        snapPitch();
        if (egMode() == 1)
            vcfEg.trigger(), vcaEg.trigger(), clockedLoud = true;
    }

    void relocate (double beats, double window, bool jumped)
    {
        const double grid = clockBeats();
        const int64_t b = (int64_t) std::floor (beats / grid + 1.0e-9);
        const double late = (beats - (double) b * grid) / perFrame; // frames since the host's clock b
        origin = 0;
        regridWanted = resetWanted = false;
        countedGrid = grid;
        // A jump whose clock b fell on the very frame a clock played here: a loop gone round inside a block (the host
        // tells only at the next one, so the clocks ran on past the loop's end; its end is its start). That clock
        // was b: the steps line up with b, and the EGs are not started again, unless the clock played then started
        // nothing and b does.
        if (jumped && std::fabs ((double) ((int64_t) now - clockedAt) - late) <= 2.0)
        {
            lastClock = b;
            if (knob[ParamId::run] < 0.5f)
                return;
            rhythmPosition = std::max<int64_t> (0, b);
            bool sounds = false;
            for (int s = 0; s < 2; ++s)
            {
                const Routing r = routing (s);
                const int64_t c = pulsesUpTo (r, b);
                fresh[s] = c == 0;
                step[s] = c == 0 ? 0 : (int) ((c - 1) & 3);
                sounds |= b >= 0 && pulseAt (r, b);
            }
            snapPitch();
            if (sounds && ! clockedLoud && egMode() == 1)
                vcfEg.trigger(), vcaEg.trigger(), clockedLoud = true;
            return;
        }
        // Just past a clock (less than a block: a Play landing a hair late, a wrap seen early enough): that clock
        // plays at once; further in, the next clock is the first.
        lastClock = b - (late < window ? 1 : 0);
        if (knob[ParamId::run] < 0.5f) // stopped by Run, the sequencers hold their steps through the host's moves
            return;
        rhythmPosition = std::max<int64_t> (0, lastClock);
        // The steps the song's place puts each sequencer on, as if it had played from the top.
        for (int s = 0; s < 2; ++s)
        {
            const int64_t c = pulsesUpTo (routing (s), lastClock);
            fresh[s] = c == 0;
            step[s] = c == 0 ? 0 : (int) ((c - 1) & 3);
        }
        snapPitch();
    }

    // Clock Div changed: count clocks on the new grid from here, without a burst of them. A clock of the new grid
    // due on this frame (in the last frame's time) still plays; a Reset keeps its moment: the generators count from
    // the first clock of the new grid at or after it.
    void regrid()
    {
        const double grid = clockBeats();
        lastClock = (int64_t) std::floor ((position - perFrame * (1.0 - clockSlack)) / grid + 1.0e-9);
        if (origin != 0)
            origin = (int64_t) std::ceil ((double) origin * countedGrid / grid - 1.0e-9);
        countedGrid = grid;
        regridWanted = false;
    }

    void applyGate()
    {
        const bool g = egMode() == 2 || held > 0;
        if (g && ! gated)
            vcfEg.trigger(), vcaEg.trigger();
        gated = g;
        vcfEg.gate = vcaEg.gate = g;
    }

    // The step's pitch and divisors from now: a step lands on its note (no glide into it).
    void snapPitch()
    {
        retarget();
        smooth[pitch1] = target[pitch1];
        smooth[pitch2] = target[pitch2];
    }

    // A decay's multiplier a sample: -60 dB over its time. An attack's charge a sample: at the top in `seconds`
    // (charging toward 1.25 times it, the top is reached after ln 5 time constants).
    float decayOf (float seconds) const { return (float) std::exp (-6.907755 / ((double) seconds * inner)); }
    float attackOf (float seconds) const { return (float) (1.0 - std::exp (-1.609438 / ((double) seconds * inner))); }

    // --- Knobs ---
    // What the knobs and the sequencers' steps ask for; the sound eases toward it every 8 frames.
    enum Smoothed { pitch1, pitch2, level1, sub11, sub12, level2, sub21, sub22, cut, res, vcfAmt, gain, smoothed };

    void retarget()
    {
        using namespace ParamId;
        const float octaves = stepOctaves (knob[seqOct]);
        const int mode = (int) knob[quantize];
        const float fine = knob[fineTune] * fineTuneCents / 1200.0f;
        for (int n = 0; n < 2; ++n)
        {
            const uint32_t at = n == 0 ? 0u : 1u; // sequencer n plays VCO n
            const float stepped = knob[(at ? seq2Step1 : seq1Step1) + step[n]] * octaves;
            // The VCO: its knob, the step (when assigned) and the note, quantized together; then Fine Tune.
            const float pitch = knob[at ? vco2Freq : vco1Freq] * vcoOctaves + (knob[at ? seq2Osc : seq1Osc] >= 0.5f ? stepped : 0.0f)
                              + transpose * (1.0f / 12.0f);
            target[pitch1 + n] = std::log2 (vcoLowHz) + quantizePitch (pitch, mode) + fine;
            // Its subs: the knob's divisor, moved by the step when assigned (up: a smaller divisor).
            for (int k = 0; k < 2; ++k)
            {
                int d = (int) knob[(at ? vco2Sub1 : vco1Sub1) + k];
                if (knob[(at ? seq2Sub1 : seq1Sub1) + k] >= 0.5f)
                    d -= (int) std::lround (divisorsPerOctave * stepped);
                vco[n].wanted[k] = std::clamp (d, 1, 16);
            }
            wave[n] = (int) knob[at ? vco2Wave : vco1Wave];
        }
        const uint32_t levels[6] = { vco1Level, vco1Sub1Level, vco1Sub2Level, vco2Level, vco2Sub1Level, vco2Sub2Level };
        for (int i = 0; i < 6; ++i)
            target[level1 + i] = knob[levels[i]] * knob[levels[i]];
        target[cut] = std::log2 (cutoffLowHz) + knob[cutoff] * cutoffOctaves;
        target[res] = knob[resonance] * maxFeedback;
        target[vcfAmt] = knob[vcfEgAmt] * vcfEgOctaves;
        target[gain] = knob[volume] * knob[volume];
        vcfEg.attack = attackOf (attackSeconds (knob[vcfAttack]));
        vcfEg.decay = decayOf (decaySeconds (knob[vcfDecay]));
        vcaEg.attack = attackOf (attackSeconds (knob[vcaAttack]));
        vcaEg.decay = decayOf (decaySeconds (knob[vcaDecay]));
        dirty = false;
        moving = true;
    }

    void sound (float* left, float* right, uint32_t frames)
    {
        uint32_t i = 0;
        while (i < frames)
        {
            if (vcaEg.resting() && vcfEg.resting() && decimator.silent())
            {
                // Nothing sounds and nothing is running down: the rest of this run is silence. The knobs need no
                // easing while nothing plays (a step lands on them), so they are taken as they are.
                if (dirty)
                    retarget();
                for (int k = 0; k < smoothed; ++k)
                    smooth[k] = target[k];
                moving = false;
                const uint32_t n = frames - i;
                std::memset (left + i, 0, sizeof (float) * n);
                if (right)
                    std::memset (right + i, 0, sizeof (float) * n);
                tick += n;
                return;
            }
            if ((tick++ & 7) == 0)
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
                }
            }
            float y;
            if (vcaEg.resting() && decimator.silent())
            {
                // The VCA is closed: the VCF EG runs down; the oscillators and filter wait.
                vcfEg.next(), vcfEg.next();
                y = 0.0f;
            }
            else
            {
                const float a = voice(), b = voice();
                // A soft ceiling, so a hot patch rounds off instead of clipping.
                y = tanhFast (decimator.push (a, b)) * 0.98f;
            }
            left[i] = y;
            if (right)
                right[i] = y;
            ++i;
        }
    }

    [[gnu::always_inline]] float voice()
    {
        const float eVcf = vcfEg.next();
        if (vcaEg.resting())
            return 0.0f;
        const float eVca = vcaEg.next();
        // The two VCOs and their subs, summed at their levels; the sum runs a sample behind, finished by the edges'
        // corrections. A VCO's increment, and the filter's gain below, are worked out again only when what they come
        // from moves (the pitch eases only every 8 frames; the cutoff stands still while the VCF EG rests or holds).
        for (int n = 0; n < 2; ++n)
            if (smooth[pitch1 + n] != dtFrom[n])
                dtFrom[n] = smooth[pitch1 + n], dt[n] = clampf (exp2Fast (dtFrom[n]) * invInner, 1.0e-6f, maxDt);
        float before = 0.0f, now = 0.0f;
        const float sum = stepVco (vco[0], wave[0], dt[0], &smooth[level1], before, now) + stepVco (vco[1], wave[1], dt[1], &smooth[level2], before, now);
        const float out = mixHeld + before;
        mixHeld = sum + now;
        // The mixer: six levels into one stage that overdrives when they are all up.
        const float mix = tanhFast (0.75f * out);
        // The ladder, its cutoff moved by the VCF EG.
        const float octaves = smooth[cut] + eVcf * smooth[vcfAmt];
        if (octaves != gainFrom)
            gainFrom = octaves, G = onePoleGain (clampf ((float) M_PI * exp2Fast (octaves) * invInner, 0.0005f, 1.2f));
        const float y = filter.process (mix, G, smooth[res]);
        // The VCA (its EG) and Volume; the soft ceiling follows the decimator.
        return y * eVca * smooth[gain] * 1.6f;
    }

    static constexpr int maxHeld = 16;
    double rate = 48000.0, perFrame = 0.0, position = 0.0;
    float inner = 96000.0f, invInner = 1.0f / 96000.0f, maxDt = 0.1f;
    float knob[ParamId::count] {};
    float target[smoothed] {}, smooth[smoothed] {};
    float mixHeld = 0.0f, transpose = 0.0f;
    float dt[2] {}, dtFrom[2] {}, G = 0.0f, gainFrom = 0.0f; // the VCOs' increments and the filter's gain, and what they came from
    int wave[2] {};
    bool playing = false, regridWanted = false, resetWanted = false, dirty = true, moving = false, gated = false;
    bool clockedLoud = false; // the last clock played started the EGs
    bool fresh[2] { true, true };
    int step[2] {};
    int keys[maxHeld] {};
    int held = 0;
    int64_t lastClock = -1, origin = 0, rhythmPosition = 0;
    double countedGrid = 0.25;          // the grid lastClock and origin count on
    uint64_t now = 0;                   // frames rendered since prepare()
    int64_t clockedAt = 0;              // the frame the last clock played on
    uint32_t tick = 0, lastBlock = 0;
    Envelope vcfEg, vcaEg;
    Vco vco[2];
    Ladder filter;
    Decimator decimator;
};
} // namespace necronomicon
