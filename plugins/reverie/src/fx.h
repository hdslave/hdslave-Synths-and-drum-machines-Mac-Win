// Reverie's effects, in the order they run: an ensemble (three modulated delays a side, a slow and a quick LFO, as
// the string machines' ensembles did), a ping-pong delay locked to the tempo (filtered feedback), and a reverb: an
// eight-line feedback delay network (Hadamard mixing, each line damped and slowly modulated, lengths set by Size and
// gains by Decay), with input diffusion, a pre-delay, a high-pass ahead of it to keep the low end clear, and Shimmer:
// an octave-up pitch shifter fed back into the reverb's input.
//
// Each effect updates its own controls every control tick (16 samples, the engine's grid) and ramps them across
// it, so its output does not depend on how a host cuts its blocks. A reset (All Sound Off, the host's reset, falling
// asleep) does no work on the lines whatever their length (DelayLine), and none at all when nothing ran since the
// last one.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

namespace reverie
{
constexpr int tickSize = 16;

namespace fast
{
// sin (x) for any x (5th-order odd polynomial on the reduced range; error under 2e-4).
inline float sin (float x)
{
    constexpr float invTwoPi = 0.15915494f, twoPi = 6.2831853f, pi = 3.14159265f;
    x -= twoPi * std::floor (x * invTwoPi + 0.5f); // -pi..pi
    if (x > pi * 0.5f) x = pi - x;
    else if (x < -pi * 0.5f) x = -pi - x;
    const float x2 = x * x;
    return x * (0.99997f + x2 * (-0.16649f + x2 * 0.0080665f));
}
inline float cos (float x) { return sin (x + 1.5707963f); }
// 2^x for any x a pitch needs (an error under 1e-6, 0.002 cent): the fraction by a polynomial, the rest in the
// exponent's bits.
inline float exp2 (float x)
{
    x = std::clamp (x, -126.0f, 126.0f);
    const float i = std::floor (x), f = x - i;
    const float p = 1.0f + f * (0.69314720f + f * (0.24022652f + f * (0.05550411f + f * (0.00961813f + f * (0.00133336f + f * 0.00015403f)))));
    uint32_t bits;
    std::memcpy (&bits, &p, 4);
    bits += (uint32_t) (int32_t) i << 23; // the exponent moved by i (two's complement: a negative i wraps)
    float out;
    std::memcpy (&out, &bits, 4);
    return out;
}
// 2^x for |x| up to about 0.5 (detune, drift: an error under 0.1 cent).
inline float exp2Small (float x) { return 1.0f + x * (0.6931472f + x * (0.2402265f + x * (0.0555041f + x * 0.0096181f))); }
// tan (x) for 0 <= x < 1.5 (a Pade approximant; the filters' prewarping).
inline float tan (float x)
{
    const float x2 = x * x;
    return x * (135135.0f + x2 * (-17325.0f + x2 * (378.0f - x2))) / (135135.0f + x2 * (-62370.0f + x2 * (3150.0f - 28.0f * x2)));
}
// tanh, smooth to 1 (a rational fit; the limiter's knee).
inline float tanh (float x)
{
    if (x > 3.0f) return 1.0f;
    if (x < -3.0f) return -1.0f;
    const float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}
} // namespace fast

// A delay line of a power-of-two length.
//
// A clear costs nothing, however long the line (the delay's are a megabyte each): it only forgets what was written.
// `fill` counts the samples written since (up to the length); until the line is full again, before each block the
// effect zeroes the few places that block will read and that were not written since (silenceStale), so the block
// reads exactly what a wiped line would give, through the same code.
struct DelayLine
{
    std::vector<float> buffer;
    uint32_t mask = 0, at = 0, fill = 0;

    void allocate (size_t minimum) // main thread
    {
        size_t n = 16;
        while (n < minimum) n <<= 1;
        buffer.assign (n, 0.0f);
        mask = (uint32_t) n - 1;
        at = 0;
        fill = (uint32_t) n; // zeros: silence already
    }
    void clear() { at = 0, fill = 0; }
    bool full() const { return fill > mask; }
    // `n` samples written by a block.
    void wrote (uint32_t n) { fill = std::min (fill + n, mask + 1); }
    // Ahead of a block: the place it reads `back` samples before the newest once `pushed` of its samples are in (and
    // a place either side, for rounding) set to silence where nothing was written since the clear.
    void silenceStale (float back, uint32_t pushed)
    {
        const uint32_t written = fill + pushed;
        const uint32_t from = back >= 1.0f ? (uint32_t) back - 1u : 0u;
        for (uint32_t d = from; d <= from + 3u; ++d)
            if (d >= written) buffer[(at + pushed - 1u - d) & mask] = 0.0f;
    }
    void push (float x) { buffer[at] = x; at = (at + 1) & mask; }
    // `d` samples before the newest (0 = the newest).
    float tap (uint32_t d) const { return buffer[(at - 1u - d) & mask]; }
    float tapFrac (float d) const
    {
        const uint32_t i = (uint32_t) d;
        const float f = d - (float) i;
        const float a = buffer[(at - 1u - i) & mask], b = buffer[(at - 2u - i) & mask];
        return a + (b - a) * f;
    }
};

inline float onePoleCoef (float hz, float rate) { return 1.0f - std::exp (-6.2831853f * hz / rate); }

// --- Ensemble ---------------------------------------------------------------------------------------------------
class Ensemble
{
public:
    void prepare (float sampleRate)
    {
        rate = sampleRate;
        clean = false;
        reset();
        for (auto& d : line) d.allocate ((size_t) (0.04f * rate) + 4);
        filled = true; // zeros: nothing stale to read
    }
    // Silence: the lines cleared and the LFOs back at their start. Nothing to do if nothing ran since the last.
    void reset()
    {
        if (clean) return;
        for (auto& d : line) d.clear();
        filled = false;
        slow = fastPhase = 0.0f;
        primed = false;
        clean = true;
        ++resets;
    }
    uint32_t resets = 0; // the resets that had something to clear (for the tests)

    // Once a tick: mix, depth (0..1) and the slow LFO's rate (Hz).
    void control (float mix, float depth, float hz)
    {
        clean = false;
        wet = mix;
        dry = 1.0f - 0.4f * mix;
        const float tickSeconds = (float) tickSize / rate;
        slow += 6.2831853f * hz * tickSeconds;
        fastPhase += 6.2831853f * (5.9f + hz * 1.3f) * tickSeconds;
        if (slow > 6.2831853f) slow -= 6.2831853f;
        if (fastPhase > 6.2831853f) fastPhase -= 6.2831853f;
        const float base = 0.0075f * rate, swing = (0.0006f + 0.0032f * depth) * rate, quick = 0.00018f * rate * (0.3f + depth);
        for (int s = 0; s < 2; ++s)
            for (int v = 0; v < 3; ++v)
            {
                const float offset = 2.0943951f * (float) v + (s == 1 ? 1.0471976f : 0.0f);
                const float d = base + swing * (0.5f + 0.5f * fast::sin (slow + offset)) + quick * fast::sin (fastPhase + offset * 1.7f);
                const int i = 3 * s + v;
                step[i] = primed ? (d - now[i]) / (float) tickSize : 0.0f;
                if (! primed) now[i] = d;
            }
        primed = true;
    }

    void process (float* l, float* r, int n)
    {
        clean = false;
        if (! filled)
        {
            if (wet > 0.0f) // the taps, as below: three a side after each push
            {
                float d[6];
                std::memcpy (d, now, sizeof (d));
                for (int i = 0; i < n; ++i)
                    for (int v = 0; v < 6; ++v) line[v / 3].silenceStale (d[v], (uint32_t) i + 1), d[v] += step[v];
            }
            line[0].wrote ((uint32_t) n), line[1].wrote ((uint32_t) n);
            filled = line[0].full() && line[1].full();
        }
        run (l, r, n);
    }

private:
    void run (float* l, float* r, int n)
    {
        if (wet <= 0.0f)
        {
            for (int i = 0; i < n; ++i)
            {
                line[0].push (l[i]), line[1].push (r[i]);
                for (int v = 0; v < 6; ++v) now[v] += step[v];
            }
            return;
        }
        for (int i = 0; i < n; ++i)
        {
            line[0].push (l[i]);
            line[1].push (r[i]);
            const float a = line[0].tapFrac (now[0]) + line[0].tapFrac (now[1]) + line[0].tapFrac (now[2]);
            const float b = line[1].tapFrac (now[3]) + line[1].tapFrac (now[4]) + line[1].tapFrac (now[5]);
            for (int v = 0; v < 6; ++v) now[v] += step[v];
            l[i] = l[i] * dry + a * (wet * 0.3333f);
            r[i] = r[i] * dry + b * (wet * 0.3333f);
        }
    }

    float rate = 48000.0f;
    DelayLine line[2];
    float slow = 0.0f, fastPhase = 0.0f, wet = 0.0f, dry = 1.0f;
    float now[6] {}, step[6] {};
    bool primed = false, clean = false, filled = true; // filled: every line written through since its clear
};

// --- Ping-pong delay --------------------------------------------------------------------------------------------
class PingPong
{
public:
    static constexpr float longest = 3.2f; // seconds (a half note at 40 BPM is 3 s)
    void prepare (float sampleRate)
    {
        rate = sampleRate;
        clean = false;
        reset();
        line[0].allocate ((size_t) (longest * rate) + 8);
        line[1].allocate ((size_t) (longest * rate) + 8);
        filled = true;
        lowCoef = onePoleCoef (4200.0f, rate);
        highCoef = onePoleCoef (160.0f, rate);
    }
    void reset() // as the ensemble's
    {
        if (clean) return;
        line[0].clear(), line[1].clear();
        filled = false;
        lp[0] = lp[1] = hp[0] = hp[1] = 0.0f;
        primed = false;
        clean = true;
        ++resets;
    }
    uint32_t resets = 0;
    void control (float mix, float seconds, float feedback)
    {
        clean = false;
        wet = mix;
        fb = std::min (feedback, 0.95f) * 0.92f;
        const float target = std::clamp (seconds * rate, 16.0f, longest * rate);
        if (! primed) time = target, primed = true;
        // Glide to a new time (a tape-like bend), within about 60 ms.
        const float next = time + (target - time) * std::min (1.0f, (float) tickSize / (0.06f * rate));
        step = (next - time) / (float) tickSize;
    }
    bool active() const { return wet > 0.0f; }
    void process (float* l, float* r, int n)
    {
        clean = false;
        if (! filled)
        {
            if (wet > 0.0f) // both lines tapped at the time, before each push
            {
                float d = time;
                for (int i = 0; i < n; ++i) line[0].silenceStale (d, (uint32_t) i), line[1].silenceStale (d, (uint32_t) i), d += step;
            }
            line[0].wrote ((uint32_t) n), line[1].wrote ((uint32_t) n);
            filled = line[0].full() && line[1].full();
        }
        run (l, r, n);
    }

private:
    void run (float* l, float* r, int n)
    {
        if (wet <= 0.0f)
        {
            for (int i = 0; i < n; ++i) line[0].push (0.0f), line[1].push (0.0f), time += step;
            return;
        }
        for (int i = 0; i < n; ++i)
        {
            const float a = line[0].tapFrac (time), b = line[1].tapFrac (time);
            time += step;
            // the feedback filtered: a gentle low-pass and high-pass, so the repeats darken and thin
            lp[0] += (b - lp[0]) * lowCoef;
            hp[0] += (lp[0] - hp[0]) * highCoef;
            lp[1] += (a - lp[1]) * lowCoef;
            hp[1] += (lp[1] - hp[1]) * highCoef;
            const float mono = (l[i] + r[i]) * 0.5f;
            line[0].push (mono + fb * (lp[0] - hp[0]));
            line[1].push (fb * (lp[1] - hp[1]));
            l[i] += wet * a;
            r[i] += wet * b;
        }
    }

    float rate = 48000.0f;
    DelayLine line[2];
    float time = 0.0f, step = 0.0f, wet = 0.0f, fb = 0.0f, lowCoef = 0.0f, highCoef = 0.0f;
    float lp[2] {}, hp[2] {};
    bool primed = false, clean = false, filled = true;
};

// --- Reverb -----------------------------------------------------------------------------------------------------
class Reverb
{
public:
    static constexpr int lines = 8;
    void prepare (float sampleRate)
    {
        rate = sampleRate;
        clean = false;
        reset();
        const float scale = rate / 48000.0f;
        for (int i = 0; i < lines; ++i) fdn[i].allocate ((size_t) (baseLength[i] * maxSize * scale) + 64);
        for (int c = 0; c < 2; ++c)
        {
            pre[c].allocate ((size_t) (0.26f * rate) + 4);
            for (int k = 0; k < diffusers; ++k)
            {
                diffLength[c][k] = std::max (1u, (uint32_t) std::lround (diffMs[c][k] * 0.001f * rate));
                diff[c][k].allocate (diffLength[c][k] + 2);
            }
        }
        shift.allocate ((size_t) (0.2f * rate) + 4);
        filled = shiftFilled = true;
        window = 0.065f * rate;
        highCoef = onePoleCoef (140.0f, rate);
        shimLow = onePoleCoef (5200.0f, rate);
        shimHigh = onePoleCoef (380.0f, rate);
    }
    void reset() // as the ensemble's
    {
        if (clean) return;
        clearLines();
        for (int i = 0; i < lines; ++i) mod[i] = 0.0f;
        mixNow = 0.0f;
        lastDecay = lastDamp = -1.0f;
        primed = false;
        clean = true;
        ++resets;
    }
    uint32_t resets = 0;
    void clearLines()
    {
        filled = shiftFilled = false;
        for (auto& d : fdn) d.clear();
        for (int c = 0; c < 2; ++c)
        {
            pre[c].clear();
            for (auto& d : diff[c]) d.clear();
            hp[c] = 0.0f;
        }
        shift.clear();
        for (int i = 0; i < lines; ++i) damp[i] = 0.0f;
        shimFeed = shimLp = shimHp = 0.0f;
        shiftPhase = 0.0f;
    }

    // Once a tick. size 0..1, decay in seconds (to -60 dB), dampHz the lines' low-pass, predelay in ms.
    void control (float mix, float size, float decay, float dampHz, float predelayMs, float shimmer)
    {
        clean = false;
        wetGain = std::sqrt (mix) * 0.62f;
        dryGain = std::sqrt (1.0f - mix);
        const float s = minSize + (maxSize - minSize) * size;
        const float scale = rate / 48000.0f;
        const float tickSeconds = (float) tickSize / rate;
        predelay = std::clamp (predelayMs * 0.001f * rate, 0.0f, 0.25f * rate);
        if (dampHz != lastDamp) dampCoef = onePoleCoef (std::min (dampHz, 0.45f * rate), rate), lastDamp = dampHz;
        bool lengths = false;
        for (int i = 0; i < lines; ++i)
        {
            const float target = baseLength[i] * s * scale;
            if (! primed) length[i] = target;
            if (length[i] != target)
            {
                length[i] += (target - length[i]) * std::min (1.0f, tickSeconds / 0.25f); // Size glides (a quarter second)
                if (std::fabs (length[i] - target) < 0.01f) length[i] = target;
                lengths = true;
            }
            phase[i] += 6.2831853f * modHz[i] * tickSeconds;
            if (phase[i] > 6.2831853f) phase[i] -= 6.2831853f;
            const float depth = (3.0f + 9.0f * size) * scale;
            const float d = length[i] + depth * (1.0f + fast::sin (phase[i]));
            modStep[i] = primed ? (d - mod[i]) / (float) tickSize : 0.0f;
            if (! primed) mod[i] = d;
        }
        if (lengths || decay != lastDecay || ! primed)
        {
            lastDecay = decay;
            float sumG = 0.0f;
            for (int i = 0; i < lines; ++i)
            {
                gain[i] = std::pow (10.0f, -3.0f * (length[i] / rate) / std::max (decay, 0.05f));
                sumG += gain[i] * gain[i];
            }
            // Longer decays gather more energy: the input is turned down part of the way to keep the level even.
            const float energy = 1.0f / std::max (1e-3f, 1.0f - sumG / (float) lines);
            inputGain = 0.45f * std::pow (energy, -0.3f);
        }
        shimmerGain = shimmer * 0.55f;
        if (mix > 0.0f && mixNow <= 0.0f && primed) clearLines(); // turned on again: no stale tail
        mixNow = mix;
        primed = true;
    }

    void process (float* l, float* r, int n)
    {
        clean = false;
        if (mixNow > 0.0f)
        {
            if (! filled) silenceStale (n);
            if (shimmerGain > 0.0f && ! shiftFilled) silenceStaleShift (n); // the shimmer's line runs only with it
        }
        run (l, r, n);
    }

private:
    // Ahead of a block while a line is not full: every place the block reads (as run does) that was not written
    // since the clear, set to silence.
    void silenceStale (int n)
    {
        float m[lines];
        std::memcpy (m, mod, sizeof (m));
        for (int i = 0; i < n; ++i)
        {
            const uint32_t before = (uint32_t) i, after = (uint32_t) i + 1;
            for (int c = 0; c < 2; ++c)
            {
                if (predelay >= 1.0f) pre[c].silenceStale (predelay, after);
                for (int k = 0; k < diffusers; ++k) diff[c][k].silenceStale ((float) (diffLength[c][k] - 1), before);
            }
            for (int j = 0; j < lines; ++j) fdn[j].silenceStale (m[j], before), m[j] += modStep[j];
        }
        filled = true;
        for (auto& d : fdn) d.wrote ((uint32_t) n), filled &= d.full();
        for (int c = 0; c < 2; ++c)
        {
            pre[c].wrote ((uint32_t) n), filled &= pre[c].full();
            for (auto& d : diff[c]) d.wrote ((uint32_t) n), filled &= d.full();
        }
    }
    void silenceStaleShift (int n)
    {
        float phase = shiftPhase;
        for (int i = 0; i < n; ++i)
        {
            phase += 1.0f / window;
            if (phase >= 1.0f) phase -= 1.0f;
            float p2 = phase + 0.5f;
            if (p2 >= 1.0f) p2 -= 1.0f;
            shift.silenceStale (2.0f + window * (1.0f - phase), (uint32_t) i + 1);
            shift.silenceStale (2.0f + window * (1.0f - p2), (uint32_t) i + 1);
        }
        shift.wrote ((uint32_t) n);
        shiftFilled = shift.full();
    }

    void run (float* l, float* r, int n)
    {
        if (mixNow <= 0.0f)
        {
            for (int k = 0; k < n; ++k)
                for (int i = 0; i < lines; ++i) mod[i] += modStep[i];
            return;
        }
        // Local copies: the compiler keeps them in registers (the buffers cannot be seen to move).
        float* line[lines];
        uint32_t lineMask[lines], lineAt[lines];
        for (int j = 0; j < lines; ++j) line[j] = fdn[j].buffer.data(), lineMask[j] = fdn[j].mask, lineAt[j] = fdn[j].at;
        float* dbuf[2][diffusers];
        uint32_t dmask[2][diffusers], dat[2][diffusers];
        for (int c = 0; c < 2; ++c)
            for (int k = 0; k < diffusers; ++k)
                dbuf[c][k] = diff[c][k].buffer.data(), dmask[c][k] = diff[c][k].mask, dat[c][k] = diff[c][k].at;
        float m[lines];
        for (int j = 0; j < lines; ++j) m[j] = mod[j];
        for (int i = 0; i < n; ++i)
        {
            // input: high-passed, delayed, diffused (the shimmer joins it)
            float in[2] = { l[i], r[i] };
            for (int c = 0; c < 2; ++c)
            {
                hp[c] += (in[c] - hp[c]) * highCoef;
                pre[c].push (in[c] - hp[c] + shimFeed);
                float x = predelay < 1.0f ? pre[c].tap (0) : pre[c].tapFrac (predelay);
                for (int k = 0; k < diffusers; ++k)
                {
                    float* b = dbuf[c][k];
                    const uint32_t mk = dmask[c][k], at = dat[c][k];
                    const float delayed = b[(at - diffLength[c][k]) & mk];
                    const float v = x + 0.62f * delayed;
                    b[at] = v;
                    dat[c][k] = (at + 1) & mk;
                    x = delayed - 0.62f * v;
                }
                in[c] = x * inputGain;
            }
            float y[lines];
            for (int j = 0; j < lines; ++j)
            {
                const float d = m[j];
                const uint32_t k = (uint32_t) d;
                const float f = d - (float) k;
                const float a = line[j][(lineAt[j] - 1u - k) & lineMask[j]], b = line[j][(lineAt[j] - 2u - k) & lineMask[j]];
                y[j] = a + (b - a) * f;
                m[j] += modStep[j];
            }
            const float outL = (y[0] - y[1] + y[2] - y[3] + y[4] - y[5] + y[6] - y[7]) * 0.5f;
            const float outR = (y[0] + y[1] - y[2] - y[3] + y[4] + y[5] - y[6] - y[7]) * 0.5f;
            float z[lines];
            for (int j = 0; j < lines; ++j)
            {
                damp[j] += (y[j] - damp[j]) * dampCoef;
                z[j] = damp[j] * gain[j];
            }
            // Hadamard (8), three butterflies; the 1 / sqrt (8) keeps the energy
            const float a0 = z[0] + z[1], a1 = z[0] - z[1], a2 = z[2] + z[3], a3 = z[2] - z[3];
            const float a4 = z[4] + z[5], a5 = z[4] - z[5], a6 = z[6] + z[7], a7 = z[6] - z[7];
            const float b0 = a0 + a2, b1 = a1 + a3, b2 = a0 - a2, b3 = a1 - a3;
            const float b4 = a4 + a6, b5 = a5 + a7, b6 = a4 - a6, b7 = a5 - a7;
            const float h[lines] = { b0 + b4, b1 + b5, b2 + b6, b3 + b7, b0 - b4, b1 - b5, b2 - b6, b3 - b7 };
            for (int j = 0; j < lines; ++j)
            {
                line[j][lineAt[j]] = h[j] * 0.35355339f + in[j & 1];
                lineAt[j] = (lineAt[j] + 1) & lineMask[j];
            }
            // Shimmer: the reverb an octave up (two heads sliding through a short line, crossfaded), filtered
            if (shimmerGain > 0.0f)
            {
                shift.push ((outL + outR) * 0.5f);
                shiftPhase += 1.0f / window;
                if (shiftPhase >= 1.0f) shiftPhase -= 1.0f;
                float p2 = shiftPhase + 0.5f;
                if (p2 >= 1.0f) p2 -= 1.0f;
                const float a = shift.tapFrac (2.0f + window * (1.0f - shiftPhase));
                const float b = shift.tapFrac (2.0f + window * (1.0f - p2));
                const float wa = 1.0f - std::fabs (2.0f * shiftPhase - 1.0f);
                const float up = a * wa + b * (1.0f - wa);
                shimLp += (up - shimLp) * shimLow;
                shimHp += (shimLp - shimHp) * shimHigh;
                shimFeed = (shimLp - shimHp) * shimmerGain;
            }
            else
                shimFeed = 0.0f;
            l[i] = l[i] * dryGain + outL * wetGain;
            r[i] = r[i] * dryGain + outR * wetGain;
        }
        for (int j = 0; j < lines; ++j) fdn[j].at = lineAt[j], mod[j] = m[j];
        for (int c = 0; c < 2; ++c)
            for (int k = 0; k < diffusers; ++k) diff[c][k].at = dat[c][k];
    }

    static constexpr int diffusers = 3;
    static constexpr float minSize = 0.3f, maxSize = 1.6f;
    static constexpr float baseLength[lines] = { 1693.0f, 1997.0f, 2333.0f, 2711.0f, 3109.0f, 3527.0f, 3967.0f, 4421.0f };
    static constexpr float modHz[lines] = { 0.11f, 0.17f, 0.23f, 0.31f, 0.13f, 0.19f, 0.29f, 0.37f };
    static constexpr float diffMs[2][diffusers] = { { 4.77f, 3.59f, 12.73f }, { 4.31f, 3.11f, 11.23f } };
    float rate = 48000.0f;
    DelayLine fdn[lines], pre[2], diff[2][diffusers], shift;
    uint32_t diffLength[2][diffusers] {};
    float length[lines] {}, phase[lines] { 0.0f, 0.9f, 1.7f, 2.6f, 3.3f, 4.1f, 4.9f, 5.6f }, mod[lines] {}, modStep[lines] {};
    float gain[lines] {}, damp[lines] {};
    float hp[2] {}, highCoef = 0.0f, dampCoef = 0.5f, predelay = 0.0f, inputGain = 0.4f;
    float wetGain = 0.0f, dryGain = 1.0f, mixNow = 0.0f;
    float shimmerGain = 0.0f, shimFeed = 0.0f, shimLp = 0.0f, shimHp = 0.0f, shimLow = 0.0f, shimHigh = 0.0f;
    float shiftPhase = 0.0f, window = 3000.0f;
    float lastDecay = -1.0f, lastDamp = -1.0f;
    bool primed = false, clean = false, filled = true, shiftFilled = true;
};
} // namespace reverie
