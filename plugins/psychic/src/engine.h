// Psychic's sound and sequencers. One voice, run at twice the host's rate and filtered back down:
//
//   SEQ 1 CV --> VCO pitch (VCO Seq 1 Amt; QTZ: in tune) and Fold (VCW Seq 1 Amt)
//   SEQ 2 CV --> Mod VCO pitch (Mod VCO Seq 2 Amt) and Cutoff (VCF Seq 2 Amt)
//   SEQ 1 and SEQ 2 triggers --> EG Trig Mix --> EG1 and EG2 (their peaks)
//   EG1 --> VCO and Mod VCO pitch, Fold and Cutoff (each by its own bipolar amount)
//   Mod VCO (triangle) --through-zero FM--> VCO (sine)
//   VCO + VCO x Mod VCO (ring) + Mod VCO + noise (its tone) --> mixer (overdrives)
//   --> VCW (wavefolder: Fold, Bias) and VCF (2-pole state variable, LP to BP), in parallel or one into the other
//   --> two VCAs (EG2) --> Blend --> Volume --> out
//
// The sequencers: 8 bits each (Length plays fewer), on or off, each with a CV between -5 and +5 V. An on bit under
// the play head triggers and its CV is held; Corrupt rolls dice at the write head every step (a new CV; past the
// middle, a flipped bit too). Chain Seq makes one ring of both, with both play heads on it. The clock is the host's.
//
// Cost: no allocation, no locks; the knobs are worked out every 8 frames, the voice only while a VCA is open.
#pragma once

#include "params.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace psychic
{
// --- Fast math ------------------------------------------------------------------------------------------------

// Two branch-free instructions (fmaxnm, fminnm); a NaN comes out as `lo`.
inline float clampf (float x, float lo, float hi) { return std::fmin (std::fmax (x, lo), hi); }

// 2^x to about 3e-6 relative: the integer part by rounding, so the polynomial only spans [-0.5, 0.5]. For pitch and
// cutoff, whose octaves the parameter ranges keep within [-40, 40]; envelope rates use exp.
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

// sin(2 pi t) for any t in turns, to about 1e-6: t folded into a quarter turn either side of 0, then an odd
// polynomial.
inline float sinTurns (float t)
{
    const float r = t - std::rint (t); // [-0.5, 0.5]
    const float q = std::copysign (0.25f - std::fabs (0.25f - std::fabs (r)), r); // [-0.25, 0.25], the same sine
    const float x = 6.28318531f * q, x2 = x * x;
    return x * (1.0f + x2 * (-0.166666667f + x2 * (0.00833333310f + x2 * (-0.000198408740f + x2 * 2.75255616e-6f))));
}

// tan(x) for x in [0, 1.2] (the cutoff's angle a sample): a Pade (3,2), under 0.6 % off at the top.
inline float tanPade (float x)
{
    const float x2 = x * x;
    return x * (15.0f - x2) / (15.0f - 6.0f * x2);
}

// --- Envelope ---------------------------------------------------------------------------------------------------
// The panel's EGs are decay-only: a trigger charges the envelope from wherever it is toward the trigger's level (its
// peak) in about a millisecond, then it decays exponentially. A softer trigger than where a tail still is does not
// cut it: the tail goes on falling. Nor does a softer trigger cut a louder one's attack: the charge goes on to the
// louder peak.
struct Envelope
{
    float value = 0.0f;
    float peak = 0.0f;
    float attack = 1.0f; // charge a sample
    float decay = 0.0f;  // multiplier a sample
    bool attacking = false;

    void trigger (float level)
    {
        peak = attacking ? std::max (peak, level) : level;
        attacking = peak > value;
    }
    bool idle() const { return ! attacking && value == 0.0f; }
    float next()
    {
        if (attacking)
        {
            value += (1.25f * peak - value) * attack;
            if (value >= peak)
                value = peak, attacking = false;
        }
        else
        {
            value *= decay;
            if (value < 1.6e-5f) // -96 dB: done, and exactly zero so the VCAs can close
                value = 0.0f;
        }
        return value;
    }
};

// --- Triangle oscillator (the Mod VCO) ----------------------------------------------------------------------------
// A triangle from a phase in [0, 1), its corners band-limited with polynomial BLAMP residuals over two samples. The
// output runs one sample behind so a corner can correct the sample before it too.
struct Triangle
{
    float phase = 0.0f;
    float held = 0.0f; // the sample still being built (emitted at the next step)

    static float shape (float p) { return p < 0.5f ? 4.0f * p - 1.0f : 3.0f - 4.0f * p; }
    static void blamp (float s, float d, float& before, float& now)
    {
        const float e = 1.0f - d;
        before += s * d * d * d * (1.0f / 6.0f);
        now += s * e * e * e * (1.0f / 6.0f);
    }
    // One sample at increment dt (cycles a sample, below 0.5): the finished previous sample.
    float step (float dt)
    {
        float before = 0.0f, now = 0.0f;
        const float to = phase + dt;
        if (phase < 0.5f && to >= 0.5f)
            blamp (-8.0f * dt, (to - 0.5f) / dt, before, now);
        if (to >= 1.0f)
            blamp (8.0f * dt, (to - 1.0f) / dt, before, now);
        phase = to >= 1.0f ? to - 1.0f : to;
        const float out = held + before;
        held = shape (phase) + now;
        return out;
    }
};

// --- State-variable filter ----------------------------------------------------------------------------------------
// Two integrators in trapezoidal form (Simper's), the feedback solved without delay: low pass and band pass at once.
// Its low pass keeps its level as the resonance rises, as the panel's does.
struct Svf
{
    float ic1 = 0.0f, ic2 = 0.0f;

    [[gnu::always_inline]] float process (float x, float g, float k, float mode)
    {
        const float a1 = 1.0f / (1.0f + g * (g + k)), a2 = g * a1, a3 = g * a2;
        const float v3 = x - ic2;
        const float v1 = a1 * ic1 + a2 * v3;
        const float v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.0f * v1 - ic1;
        ic2 = 2.0f * v2 - ic2;
        return v2 + mode * (v1 - v2); // LP, BP, or between
    }
};

// --- Half-band decimator ------------------------------------------------------------------------------------------
// From twice the host's rate to its rate: a 47-tap Kaiser-windowed half-band FIR. Its even taps are zero but the
// centre, so a frame costs 12 multiplies: flat to about 19 kHz at 48 kHz, about 80 dB down from 29 kHz.
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
        const int edge = 4 * half - 1;
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
        quiet = 2 * half + 1; // all zeros: silent from the start, so knobs set before Play are in place for it
    }
    bool silent() const { return quiet >= 2 * half + 1; }
    float push (float a, float b)
    {
        constexpr int len = 2 * half;
        odd[at] = odd[at + len] = b;
        even[evenAt] = a;
        quiet = (a == 0.0f && b == 0.0f) ? std::min (quiet + 1, 2 * half + 1) : 0; // held at silent(): never overflows
        const float* w = odd + at + 1;
        float sum = 0.0f;
        for (int i = 0; i < half; ++i)
            sum += coeff[i] * (w[half + i] + w[half - 1 - i]);
        evenAt = evenAt + 1 == half ? 0 : evenAt + 1;
        const float centre = even[evenAt];
        at = at + 1 == len ? 0 : at + 1;
        return sum + 0.5f * centre;
    }
};

// --- Quantizer ----------------------------------------------------------------------------------------------------
// The nearest note of a mode to `semitones` above the root (ties go down). Unquantized passes it as it is.
inline float quantizeSemitones (float semitones, int mode)
{
    const uint16_t mask = scaleMask[mode];
    if (mask == 0)
        return semitones;
    const float octave = std::floor (semitones / 12.0f);
    const float within = semitones - 12.0f * octave; // [0, 12)
    float best = 0.0f, distance = within;            // the root is in every mode
    for (int d = 1; d <= 12; ++d)
        if (d == 12 || (mask >> d) & 1)
        {
            const float far = std::fabs (within - (float) d);
            if (far < distance)
                best = (float) d, distance = far;
        }
    return 12.0f * octave + best;
}

// --- Engine -------------------------------------------------------------------------------------------------------
// A clock less than this many frames away is due now. CLAP gives the song's place in steps of 2^-31 beats, which
// at 48 kHz is about 1e-5 frames: a clock that falls on a block's first frame could otherwise come a frame late, by
// how the host's rounding fell for that block, so the sound would depend on the block size.
constexpr double clockSlack = 1.0e-3;

class Engine
{
public:
    void prepare (double hostRate)
    {
        rate = hostRate;
        inner = (float) (2.0 * hostRate);
        invInner = 1.0f / inner;
        decimator.design();
        dcPole = (float) std::exp (-2.0 * M_PI * 8.0 / inner); // the folder's DC blocker: 8 Hz
        attackRate = attackOf (attackSeconds);
        rated = true; // the rate moved: the decays and the noise's split again
        reset();
    }

    // Sound and clocks back to rest; the sequences (they are parameters) stay as they are.
    void reset()
    {
        eg1 = eg2 = Envelope {};
        mod = Triangle {};
        mod.phase = 0.25f; // the Mod VCO starts at its middle, rising
        vcoPhase = 0.0f;
        svf = Svf {};
        dcIn = dcOut = 0.0f;
        noiseLow = 0.0f;
        decimator.clear();
        noise = 0x9e3779b9u;
        tick = 0;
        now = 0;
        playing = false;
        for (auto& c : clocks)
            c = Clock {};
        for (auto& l : lanes)
            l.play = -1, l.offset = 0, l.held = 0.0f, l.aligned = false, l.hit = false;
        dirty = true;
        retarget();
        for (int i = 0; i < smoothed; ++i)
            smooth[i] = target[i];
        moving = false;
    }

    // A parameter's value, from the frame the next render starts.
    void set (uint32_t id, float v)
    {
        if (id >= ParamId::count || knob[id] == v)
            return;
        knob[id] = v;
        dirty = true;
        if (id == ParamId::eg1Decay || id == ParamId::eg2Decay || id == ParamId::noiseTone)
            rated = true;
        using namespace ParamId;
        switch (id)
        {
            case clockDiv:
                clocks[0].regridWanted = true;
                clocks[1].regridWanted = ! ownClock2(); // Clock 2 on its own grid keeps it
                break;
            case clock2: clocks[1].regridWanted = true; break;
            case bits1: lanes[0].bits = (uint8_t) v; break;
            case bits2: lanes[1].bits = (uint8_t) v; break;
            case length1: lanes[0].length = std::max (1, (int) v); break;
            case length2: lanes[1].length = std::max (1, (int) v); break;
            case writeHead1: lanes[0].ahead = (int) v; break;
            case writeHead2: lanes[1].ahead = (int) v; break;
            case chain: setChained (v >= 0.5f); break;
            default:
                if (id >= cv1First && id <= cv1Last) lanes[0].cv[id - cv1First] = v;
                if (id >= cv2First && id <= cv2Last) lanes[1].cv[id - cv2First] = v;
                break;
        }
    }

    // The buttons.
    void press (uint32_t id)
    {
        switch (id)
        {
            case ParamId::trigger: strikeLevel (1.0f); break;
            case ParamId::advance:
                // Moves both play heads (and their write heads) on a bit, without playing; not while running.
                if (! running())
                {
                    for (int s = 0; s < 2; ++s)
                        moveOn (s, false);
                    stepped();
                }
                break;
            case ParamId::reset:
                // Both play heads back to bit 1: the next clock plays it. The write heads keep their distance.
                for (auto& l : lanes)
                    l.play = -1, l.aligned = true;
                break;
            case ParamId::bitFlip1: flipAtWriteHead (0); break;
            case ParamId::bitFlip2: flipAtWriteHead (1); break;
            case ParamId::bitShift1: shift (0); break;
            case ParamId::bitShift2: shift (1); break;
            case ParamId::save:
                for (uint32_t i = 0; i < seqStateCount; ++i)
                    buffer[i] = knob[seqStateIds[i]];
                bufferFull = true;
                bufferSaved = true;
                break;
            case ParamId::recall:
                if (bufferFull)
                    for (uint32_t i = 0; i < seqStateCount; ++i)
                        change (seqStateIds[i], buffer[i]);
                break;
            default: break;
        }
    }

    // A MIDI note: transposes the quantized sequences (the note's distance from middle C); while the sequencers are
    // not running it also triggers both envelopes, at its velocity.
    void note (int key, float velocity)
    {
        transpose = (float) (key - transposeNote);
        dirty = true;
        retarget();
        snapSequenced();
        if (! running())
            strikeLevel (velocity);
    }

    // A value the engine set itself (Bit Flip, Bit Shift, Corrupt, Recall), once each: the host must hear them as the
    // parameters' values.
    bool takeChange (uint32_t& id, float& value)
    {
        if (changed == 0)
            return false;
        const int i = __builtin_ctz (changed);
        changed &= changed - 1;
        id = seqStateIds[i];
        value = knob[id];
        return true;
    }

    // The buffer (Save and Recall), for the plug-in's state: whether there is one, and its values in seqStateIds'
    // order.
    bool takeBufferSaved() { const bool was = bufferSaved; bufferSaved = false; return was; }
    const float* bufferValues() const { return buffer; }
    bool hasBuffer() const { return bufferFull; }
    void setBuffer (bool full, const float* values)
    {
        bufferFull = full;
        for (uint32_t i = 0; i < seqStateCount; ++i)
            buffer[i] = values[i];
    }

    // Physical cell on the two sequencer lanes (0..7, 8..15). A reset head has no played cell.
    int screenCell (int s, bool write) const
    {
        const int place = write ? writePlace (s) : lanes[s].play;
        if (place < 0) return 16;
        int lane, bit;
        cellOf (s, place % ringLength (s), lane, bit);
        return lane * 8 + bit;
    }
    int64_t screenHitFrame (int s) const
    {
        return running() && lanes[s].hit ? clocks[s == 1 && ownClock2() ? 1 : 0].at : -1;
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
        // A Play lines the sequences up with the song's place, from bit 1 at the top. A place other than where the last
        // block ended by under half a step is the host's count drifting from ours (its tempo moved inside the block):
        // the clocks carry on, so none plays twice and none is lost. Further is a jump, or a loop going round: line up
        // again, the play heads keeping how far they are from the song's place (after a Reset, an Advance, Bit
        // Shift 2).
        const double moved = std::fabs (beats - position);
        if (! playing)
            relocate (beats, window, true);
        else if (moved > 0.5 * perFrame && moved >= 0.5 * std::min (grid (0), grid (1)))
            relocate (beats, window, false);
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
                for (int c = 0; c < 2; ++c)
                    if (clocks[c].regridWanted)
                        regrid (c);
                const bool own = ownClock2();
                const double ahead1 = (clockBeat (0, clocks[0].last + 1) - position) / perFrame;
                const double ahead2 = own ? (clockBeat (1, clocks[1].last + 1) - position) / perFrame : 1.0e300;
                const bool due1 = ahead1 <= clockSlack, due2 = own && ahead2 <= clockSlack;
                if (due1 || due2)
                {
                    // Steps on this frame: the sequences move on (with Run off they hold, silent), and the triggers
                    // that fall together reach the envelopes together, through EG Trig Mix.
                    const bool go = knob[ParamId::run] >= 0.5f;
                    bool hit1 = false, hit2 = false;
                    if (due1)
                    {
                        ++clocks[0].last;
                        clocks[0].at = (int64_t) now;
                        if (go)
                        {
                            hit1 = lanes[0].hit = step (0, clocks[0].last);
                            if (! own)
                                hit2 = lanes[1].hit = step (1, clocks[0].last);
                        }
                    }
                    if (due2)
                    {
                        ++clocks[1].last;
                        clocks[1].at = (int64_t) now;
                        if (go)
                            hit2 = lanes[1].hit = step (1, clocks[1].last);
                    }
                    if (go)
                    {
                        stepped();
                        const float mix = knob[ParamId::egTrigMix];
                        strikeLevel ((hit1 ? 1.0f - mix : 0.0f) + (hit2 ? mix : 0.0f));
                    }
                    continue;
                }
                const double untilStep = std::ceil (std::min (ahead1, ahead2) - clockSlack);
                if (untilStep < (double) run)
                    run = (uint32_t) untilStep;
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
    struct Lane
    {
        uint8_t bits = 0;
        float cv[8] {};
        int length = 8;
        int ahead = 0;      // the write head, this many bits after the play head
        int play = -1;      // the bit the play head is on (in its ring); -1: before bit 1 (after a Reset)
        int offset = 0;     // the play head's place less the song's (its clock count), round its ring
        float held = 0.0f;  // the CV of the last on bit the play head met: the sequencer's CV output
        bool aligned = false; // false after a Play or a jump: the next clock places the play head by the song
        bool hit = false;   // its last step was on (it triggered)
    };
    struct Clock
    {
        int64_t last = -1; // the number (from the song's top) of the clock last played
        int64_t at = -(int64_t (1) << 40); // the frame (`now`) it played on
        bool regridWanted = false;
    };

    bool running() const { return playing && knob[ParamId::run] >= 0.5f; }
    bool ownClock2() const { return knob[ParamId::clock2] >= 0.5f; }

    // A sequencer's ring: its own bits up to its Length, or with Chain Seq both sequences, SEQ 1's then SEQ 2's.
    int ringLength (int s) const { return chained ? lanes[0].length + lanes[1].length : lanes[s].length; }
    // The lane and bit at place p of sequencer s's ring.
    void cellOf (int s, int p, int& lane, int& bit) const
    {
        if (! chained)
            lane = s, bit = p;
        else if (p < lanes[0].length)
            lane = 0, bit = p;
        else
            lane = 1, bit = p - lanes[0].length;
    }
    int writePlace (int s) const
    {
        const int n = ringLength (s);
        return (std::max (lanes[s].play, 0) + lanes[s].ahead) % n;
    }

    void setChained (bool on)
    {
        if (on == chained)
            return;
        chained = on;
        if (on)
            lanes[1].play = lanes[0].play; // both play heads together on the one ring
        else
            for (int s = 0; s < 2; ++s)
                if (lanes[s].play >= 0)
                    lanes[s].play %= lanes[s].length;
    }

    // A value the engine sets itself: as set(), and the host is told.
    void change (uint32_t id, float v)
    {
        set (id, v);
        const int i = seqStateIndex (id);
        if (i >= 0)
            changed |= 1u << i;
    }
    void setBit (int lane, int bit, bool on)
    {
        const uint8_t bits = (uint8_t) (on ? lanes[lane].bits | (1u << bit) : lanes[lane].bits & ~(1u << bit));
        change (lane == 0 ? ParamId::bits1 : ParamId::bits2, (float) bits);
    }
    void setCv (int lane, int bit, float v) { change ((lane == 0 ? ParamId::cv1First : ParamId::cv2First) + (uint32_t) bit, v); }

    // A number in [0, 1), the sequencers' dice (xorshift64*): the same from the same start, whatever the blocks.
    float unit()
    {
        dice ^= dice >> 12, dice ^= dice << 25, dice ^= dice >> 27;
        return (float) ((dice * 0x2545f4914f6cdd1dull) >> 40) * (1.0f / 16777216.0f);
    }
    // A new random CV for a bit: anywhere in +-5 V, as flipping a bit on gives it.
    float randomCv() { return 2.0f * unit() - 1.0f; }

    // Bit Flip: the bit under the write head turns over; turned on, it takes a new random CV.
    void flipAtWriteHead (int s)
    {
        int lane, bit;
        cellOf (s, writePlace (s), lane, bit);
        const bool on = ! ((lanes[lane].bits >> bit) & 1);
        setBit (lane, bit, on);
        if (on)
            setCv (lane, bit, randomCv());
    }

    // Bit Shift: the sequence turns one bit to the right, the last bit coming round to bit 1. Chained, Bit Shift 1
    // turns the whole ring, and Bit Shift 2 moves SEQ 2's play head on a bit (the distance between the two heads).
    void shift (int s)
    {
        if (chained && s == 1)
        {
            lanes[1].play = (lanes[1].play + 1) % ringLength (1);
            return;
        }
        const int n = ringLength (s);
        bool on[16];
        float cv[16];
        for (int p = 0; p < n; ++p)
        {
            int lane, bit;
            cellOf (s, p, lane, bit);
            on[p] = (lanes[lane].bits >> bit) & 1;
            cv[p] = lanes[lane].cv[bit];
        }
        for (int p = 0; p < n; ++p)
        {
            const int from = p == 0 ? n - 1 : p - 1;
            int lane, bit;
            cellOf (s, p, lane, bit);
            if (((lanes[lane].bits >> bit) & 1) != on[from])
                setBit (lane, bit, on[from]);
            if (lanes[lane].cv[bit] != cv[from])
                setCv (lane, bit, cv[from]);
        }
    }

    // The play head on to its next bit (the first after a Reset; round the ring if it shrank under the head), or to
    // the song's place plus its offset; an on bit there gives the CV output its value. True if that bit is on.
    bool moveOn (int s, bool placeBySong, int64_t clock = 0)
    {
        Lane& l = lanes[s];
        const int n = ringLength (s);
        if (placeBySong)
            l.play = (int) ((((clock + l.offset) % n) + n) % n);
        else
            l.play = (l.play + 1) % n;
        int lane, bit;
        cellOf (s, l.play, lane, bit);
        if (! ((lanes[lane].bits >> bit) & 1))
            return false;
        l.held = lanes[lane].cv[bit];
        return true;
    }

    // One clock of sequencer s: it moves on (after a Play or a jump, to the song's place: the clock's number from
    // the top, round its ring), plays what is there, then Corrupt rolls at the write head: up to the middle of its
    // turn a growing chance the bit's CV is new; past it, a growing chance the bit flips too.
    bool step (int s, int64_t clock)
    {
        Lane& l = lanes[s];
        const bool hit = moveOn (s, ! l.aligned, clock);
        l.aligned = true;
        const int n = ringLength (s);
        l.offset = (int) ((((int64_t) l.play - clock) % n + n) % n);
        const float corrupt = knob[s == 0 ? ParamId::corrupt1 : ParamId::corrupt2];
        if (corrupt > 0.0f)
        {
            int lane, bit;
            cellOf (s, writePlace (s), lane, bit);
            const float newCv = unit(), flip = unit();
            const bool on = (lanes[lane].bits >> bit) & 1;
            if (on && newCv < 0.5f * corrupt)
                setCv (lane, bit, randomCv());
            if (flip < corrupt - 0.5f)
            {
                setBit (lane, bit, ! on);
                if (! on)
                    setCv (lane, bit, randomCv());
            }
        }
        return hit;
    }

    // Swing: the second clock of each pair comes later, at Swing's share of the pair (50 %: straight). Each clock its
    // own grid: Clock Div, and Clock 2's own or Clock 1's.
    double grid (int c) const
    {
        if (c == 1 && ownClock2())
            return clockDivBeats[(int) knob[ParamId::clock2] - 1];
        return clockDivBeats[(int) knob[ParamId::clockDiv]];
    }
    double clockBeat (int c, int64_t k) const
    {
        const double g = grid (c), swing = knob[ParamId::swing];
        if (swing == 0.5 || (k & 1) == 0)
            return (double) k * g;
        return (double) (k - 1) * g + 2.0 * g * swing;
    }
    // The last clock at or before `beats`.
    int64_t clockAtOrBefore (int c, double beats) const
    {
        const double g = grid (c);
        const int64_t b = (int64_t) std::floor (beats / g + 1.0e-9);
        if ((b & 1) == 0 || knob[ParamId::swing] == 0.5f)
            return b;
        return beats / g + 1.0e-9 < clockBeat (c, b) / g ? b - 1 : b; // between the grid line and the late clock
    }

    // `fresh`: a Play, which starts the play heads at the song's place itself (offsets cleared).
    void relocate (double beats, double window, bool fresh)
    {
        bool stood[2] = {};
        int64_t b[2] = {};
        for (int c = 0; c < 2; ++c)
        {
            b[c] = clockAtOrBefore (c, beats);
            const double late = (beats - clockBeat (c, b[c])) / perFrame; // frames since the host's clock b began
            // A jump whose clock b began on the very frame a clock played here: a loop gone round inside a block (a
            // host such as Melodee tells of it only at the next block, so the clocks ran on past the loop's end; its
            // end is its start). That clock was b, and b does not play again.
            stood[c] = ! fresh && std::fabs ((double) ((int64_t) now - clocks[c].at) - late) <= 2.0;
            // Otherwise, just past a step's start (less than a block: a wrap seen early enough, a Play landing a hair
            // late): that step plays at once; further in, the next step is the first.
            clocks[c].last = stood[c] ? b[c] : b[c] - (late < window ? 1 : 0);
            clocks[c].regridWanted = false;
        }
        position = beats;
        if (knob[ParamId::run] < 0.5f) // stopped by Run, the sequences hold their places through the host's moves
            return;
        for (auto& l : lanes)
            l.aligned = false, l.offset = fresh ? 0 : l.offset;
        // The sequences on a clock that stood for b go to where b puts them, as if b had played there: the CV is b's
        // bit's; and if the step played then was silent and b's bit is on, b sounds now (a block late at most). It
        // sounds as b does, with every sequence whose bit there is on: one that already struck on that clock (its
        // old bit on too) still has its share in b's level, or b would come in softer than it plays elsewhere.
        bool on[2] = {}, struck[2] = {}, newlyOn = false;
        for (int s = 0; s < 2; ++s)
        {
            const int c = s == 1 && ownClock2() ? 1 : 0;
            if (! stood[c])
                continue;
            struck[s] = lanes[s].hit; // it sounded on the clock that played here
            on[s] = moveOn (s, true, b[c]);
            lanes[s].aligned = true;
            newlyOn |= on[s] && ! lanes[s].hit;
            lanes[s].hit |= on[s];
        }
        if (stood[0] || stood[1])
        {
            stepped();
            // b strikes only if it is louder than what that clock already struck: a sequence newly on that EG Trig
            // Mix gives no share (Mix at 0 or 1) adds nothing, so the downbeat does not strike twice.
            const float mix = knob[ParamId::egTrigMix];
            const float level = (on[0] ? 1.0f - mix : 0.0f) + (on[1] ? mix : 0.0f);
            const float already = (struck[0] ? 1.0f - mix : 0.0f) + (struck[1] ? mix : 0.0f);
            if (newlyOn && level > already)
                strikeLevel (level);
        }
    }

    // A grid changed: count clocks on the new one from here, without a burst of them. A clock on this very frame has
    // not played (a frame's events come before its clocks), so it still does.
    void regrid (int c)
    {
        int64_t b = clockAtOrBefore (c, position);
        if ((position - clockBeat (c, b)) / perFrame < clockSlack)
            --b;
        clocks[c].last = b;
        clocks[c].regridWanted = false;
    }

    // The envelopes, toward `level`: EG1 and EG2 together (EG Trig Mix's sum, a Trigger, a note).
    void strikeLevel (float level)
    {
        if (level <= 0.0f)
            return;
        eg1.trigger (level);
        eg2.trigger (level);
    }

    // A step moved the sequences: their CVs land on the voice at once (no glide into a step).
    void stepped()
    {
        dirty = true;
        retarget();
        snapSequenced();
    }
    void snapSequenced()
    {
        smooth[vcoPitch] = target[vcoPitch];
        smooth[modPitch] = target[modPitch];
        smooth[foldAt] = target[foldAt];
        smooth[cut] = target[cut];
    }

    // A sequencer's CV output in octaves (1 V/octave): its held CV through CV Range and the quantizer, transposed
    // by the last MIDI note when quantized.
    float seqCv (int s) const
    {
        const float v = lanes[s].held * knob[s == 0 ? ParamId::cvRange1 : ParamId::cvRange2] * seqOctaves;
        const int mode = (int) knob[ParamId::quantize];
        if (scaleMask[mode] == 0)
            return v;
        return (quantizeSemitones (v * 12.0f, mode) + transpose) * (1.0f / 12.0f);
    }

    // An EG's multiplier a sample: -60 dB over its time. The attack's charge a sample: at the peak in 1 ms (charging
    // toward 1.25 times it, the peak is reached after ln 5 time constants).
    float decayOf (float k) const { return (float) std::exp (-6.907755 / ((double) decaySeconds (k) * inner)); }
    float attackOf (float seconds) const { return (float) (1.0 - std::exp (-1.609438 / ((double) seconds * inner))); }

    // --- Knobs ---
    // What the knobs and the sequences ask for; the sound eases toward it every 8 frames.
    enum Smoothed { vcoPitch, vcoEg, modPitch, modEg, fm, levelVco, levelRing, levelMod, levelNoise, noiseLowShare, noiseHighShare,
                    foldAt, biasAt, vcwEg, modFoldAt, cut, damping, vcfEg, modCut, mode, blendAt, gain, smoothed };

    void retarget()
    {
        using namespace ParamId;
        const float seq1 = seqCv (0), seq2 = seqCv (1);
        target[vcoPitch] = std::log2 (vcoLowHz) + knob[vcoFreq] * vcoOctaves + seq1 * seqAmtScale (knob[vcoSeq1Amt]);
        target[vcoEg] = knob[vcoEg1Amt] * egPitchOctaves;
        target[modPitch] = std::log2 (modLowHz) + knob[modFreq] * modOctaves + seq2 * seqAmtScale (knob[modSeq2Amt]);
        target[modEg] = knob[modEg1Amt] * egPitchOctaves;
        target[fm] = knob[fmAmt] * knob[fmAmt] * fmIndex; // the knob's taper
        // The mixer: unity at 12 o'clock, overdrive past it (3 times at full).
        target[levelVco] = 3.0f * knob[vcoLevel] * knob[vcoLevel];
        target[levelRing] = 3.0f * knob[ringLevel] * knob[ringLevel];
        target[levelMod] = 3.0f * knob[modLevel] * knob[modLevel];
        target[levelNoise] = 3.0f * knob[noiseLevel] * knob[noiseLevel];
        // Noise Tone: low (a low pass) to the left, white at 12 o'clock, high (a high pass) to the right; the split
        // between them moves up with the knob, 300 Hz to 9.6 kHz.
        target[noiseLowShare] = std::min (1.0f, 2.0f * (1.0f - knob[noiseTone]));
        target[noiseHighShare] = std::min (1.0f, 2.0f * knob[noiseTone]);
        // The wavefolder: Fold (and SEQ 1 at its amount, in its +-5 V as a turn each way), Bias, EG1's amount.
        target[foldAt] = knob[fold] + seq1 * (1.0f / seqOctaves) * knob[vcwSeq1Amt];
        target[biasAt] = knob[bias];
        target[vcwEg] = knob[vcwEg1Amt];
        target[modFoldAt] = knob[modFold] * modFoldSpan;
        // The filter: Cutoff, SEQ 2 at its amount (1 V/octave at full), EG1, the Mod VCO patched in.
        target[cut] = std::log2 (cutoffLowHz) + knob[cutoff] * cutoffOctaves + seq2 * knob[vcfSeq2Amt];
        target[damping] = 2.0f - knob[resonance] * (2.0f - dampingMin);
        target[vcfEg] = knob[vcfEg1Amt] * egCutoffOctaves;
        target[modCut] = knob[modCutoff] * modCutoffOctaves;
        target[mode] = knob[filterMode];
        target[blendAt] = knob[blend];
        target[gain] = knob[volume] * knob[volume];

        order = (int) knob[ParamId::order];
        droning = knob[drone] >= 0.5f;
        // The rates (exp and pow): only when their knobs or the sample rate moved, so turning another knob stays cheap.
        if (rated)
        {
            decay1 = decayOf (knob[eg1Decay]);
            decay2 = decayOf (knob[eg2Decay]);
            noisePole = (float) std::exp (-2.0 * M_PI * 300.0 * std::exp2 (5.0 * knob[noiseTone]) / inner);
            rated = false;
        }
        eg1.decay = decay1;
        eg2.decay = decay2;
        eg1.attack = eg2.attack = attackRate;
        dirty = false;
        moving = true;
    }

    void sound (float* left, float* right, uint32_t frames)
    {
        uint32_t i = 0;
        while (i < frames)
        {
            if (eg1.idle() && eg2.idle() && decimator.silent())
            {
                // Nothing is running down. The knobs need no easing while nothing plays (a step lands on them), so they
                // are taken as they are; then, unless Drone holds the VCAs open, the rest of this run is silence.
                if (dirty)
                    retarget();
                for (int k = 0; k < smoothed; ++k)
                    smooth[k] = target[k];
                moving = false;
            }
            if (eg1.idle() && eg2.idle() && ! droning && decimator.silent())
            {
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
            if (eg2.idle() && ! droning && decimator.silent())
            {
                // The VCAs are closed: EG1 runs down; the oscillators and filter wait.
                eg1.next(), eg1.next();
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

    // The wavefolder: Bias offsets the signal, then a sine folds it. Gently turned (gain below a quarter turn) it is
    // scaled back so a full-scale input still reaches full scale: at 0 it is clean.
    [[gnu::always_inline]] float vcw (float x, float foldShare)
    {
        const float k = std::max (foldShare * foldMax, 1.0e-4f);
        const float u = x + smooth[biasAt];
        const float folded = sinTurns (k * u * (1.0f / 6.28318531f));
        const float scale = k < 1.57079633f ? 1.0f / sinTurns (k * (1.0f / 6.28318531f)) : 1.0f;
        const float y = folded * scale;
        // The DC Bias leaves is blocked (8 Hz), as the circuit's coupling does.
        dcOut = y - dcIn + dcPole * dcOut;
        dcIn = y;
        return dcOut;
    }

    [[gnu::always_inline]] float voice()
    {
        const float e1 = eg1.next();
        if (eg2.idle() && ! droning)
            return 0.0f;
        const float e2 = eg2.next();
        const float vca = droning ? 1.0f : e2;
        // The Mod VCO first (it modulates the VCO): its pitch from the knob, SEQ 2 and EG1.
        const float dtMod = clampf (exp2Fast (smooth[modPitch] + e1 * smooth[modEg]) * invInner, 1.0e-9f, 0.45f);
        const float tri = mod.step (dtMod);
        const float m = Triangle::shape (mod.phase); // where it is now, for FM and the patched routings
        // The VCO: a sine, its frequency swung through zero by the Mod VCO (it runs backwards for a moment), so its
        // centre stays where the knob, SEQ 1 and EG1 put it.
        const float f = exp2Fast (smooth[vcoPitch] + e1 * smooth[vcoEg]) * invInner;
        const float dt = clampf (f * (1.0f + smooth[fm] * m), -0.45f, 0.45f);
        float p = vcoPhase + dt;
        p -= std::floor (p);
        vcoPhase = p;
        const float sine = sinTurns (p);
        // Noise: a 32-bit xorshift, split into low and high by Noise Tone.
        noise ^= noise << 13, noise ^= noise >> 17, noise ^= noise << 5;
        const float white = (float) (int32_t) noise * (1.0f / 2147483648.0f);
        noiseLow = white + noisePole * (noiseLow - white);
        const float n = smooth[noiseLowShare] * noiseLow + smooth[noiseHighShare] * (white - noiseLow);
        // The mixer: each level drives a shared stage that overdrives when pushed.
        const float mix = tanhFast (smooth[levelVco] * sine + smooth[levelRing] * sine * tri + smooth[levelMod] * tri + smooth[levelNoise] * n);
        // The VCW and VCF, each with its modulation, in the Order set.
        const float foldShare = clampf (smooth[foldAt] + e1 * smooth[vcwEg] + m * smooth[modFoldAt], 0.0f, 1.0f);
        const float hz = exp2Fast (smooth[cut] + e1 * smooth[vcfEg] + m * smooth[modCut]);
        const float g = tanPade (clampf ((float) M_PI * hz * invInner, 0.0005f, 1.2f));
        float wavefolded, filtered;
        if (order == 1)
        {
            wavefolded = vcw (mix, foldShare);
            filtered = svf.process (wavefolded, g, smooth[damping], smooth[mode]);
        }
        else if (order == 2)
        {
            filtered = svf.process (mix, g, smooth[damping], smooth[mode]);
            wavefolded = vcw (filtered, foldShare);
        }
        else
        {
            wavefolded = vcw (mix, foldShare);
            filtered = svf.process (mix, g, smooth[damping], smooth[mode]);
        }
        // The two VCAs (EG2, or open with Drone), Blend between them, and Volume.
        // (Each end exactly its own path.)
        return ((1.0f - smooth[blendAt]) * wavefolded + smooth[blendAt] * filtered) * vca * smooth[gain] * 1.3f;
    }

    double rate = 48000.0, perFrame = 0.0, position = 0.0;
    float inner = 96000.0f, invInner = 1.0f / 96000.0f;
    float knob[ParamId::count] {};
    float target[smoothed] {}, smooth[smoothed] {};
    float vcoPhase = 0.0f, noiseLow = 0.0f, noisePole = 0.0f, dcIn = 0.0f, dcOut = 0.0f, dcPole = 0.0f;
    float decay1 = 0.0f, decay2 = 0.0f, attackRate = 1.0f; // the EGs' rates, worked out when `rated`
    bool rated = true;
    float transpose = 0.0f;
    int order = 0;
    bool droning = false, chained = false;
    bool playing = false, dirty = true, moving = false;
    Lane lanes[2];
    Clock clocks[2];
    uint64_t dice = 0x853c49e6748fea9bull;
    uint32_t changed = 0; // seqStateIds the engine changed and the host has not heard
    float buffer[seqStateCount] {};
    bool bufferFull = false, bufferSaved = false;
    uint32_t tick = 0, lastBlock = 0, noise = 0x9e3779b9u;
    uint64_t now = 0; // frames rendered since prepare(): when each clock played
    Envelope eg1, eg2;
    Triangle mod;
    Svf svf;
    Decimator decimator;
};
} // namespace psychic
