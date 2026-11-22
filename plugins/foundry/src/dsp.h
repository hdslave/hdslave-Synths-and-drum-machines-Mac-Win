// Foundry's building blocks: fast math, the resonator the 808-style voices ring with, filters, band-limited squares
// and noise. No allocation, no locks; everything here runs a sample at a time.
#pragma once

#include <cmath>
#include <cstdint>
#include <cstring>

namespace foundry
{
constexpr float pi = 3.14159265358979f;

// Two branch-free instructions (fmaxnm, fminnm); a NaN comes out as `lo`.
inline float clampf (float x, float lo, float hi) { return std::fmin (std::fmax (x, lo), hi); }

// 2^x to about 3e-6 relative: the integer part by rounding, the rest a polynomial over [-0.5, 0.5]. For pitch and
// cutoff, whose octaves stay within +-30; decays use exp (they sit so near 1 that this error would show).
[[gnu::always_inline]] inline float exp2Fast (float x)
{
    const float xi = std::rint (x), f = x - xi;
    const float p = 1.0f + f * (0.69314718f + f * (0.24022651f + f * (0.05550411f + f * (0.00961813f + f * 0.00133336f))));
    const int32_t bits = ((int32_t) xi + 127) << 23;
    float scale;
    std::memcpy (&scale, &bits, sizeof (scale));
    return p * scale;
}

// log2 to about 0.01 (a compressor's detector needs no more): the exponent's bits and a parabola for the mantissa.
inline float log2Fast (float x)
{
    int32_t bits;
    std::memcpy (&bits, &x, sizeof (bits));
    const float e = (float) ((bits >> 23) - 127);
    bits = (bits & 0x7fffff) | 0x3f800000;
    float m;
    std::memcpy (&m, &bits, sizeof (m));
    m -= 1.0f;
    return e + m * (1.3465f - 0.3465f * m);
}

// tanh, rational, exact at 0 and +-3 where it reaches +-1 and holds: bounded, smooth, cheap.
[[gnu::always_inline]] inline float tanhFast (float x)
{
    x = clampf (x, -3.0f, 3.0f);
    const float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

// sin(2 pi x) for any x, to about 4e-6: one period folded to a quarter, then a minimax polynomial.
inline float sin2pi (float x)
{
    x -= std::rint (x); // [-0.5, 0.5]
    const float up = 0.5f - x, down = -0.5f - x;
    x = x > 0.25f ? up : (x < -0.25f ? down : x);
    const float t = 2.0f * pi * x, t2 = t * t;
    return t * (0.99999660f + t2 * (-0.16664824f + t2 * (0.00830629f + t2 * -0.00018363f)));
}
inline float cos2pi (float x) { return sin2pi (x + 0.25f); }

// tan(x) for x in [0, 1.26] (a corner up to 0.4 of the rate): Pade (3,2), under 1 % off at the top.
inline float tanPade (float x)
{
    const float x2 = x * x;
    return x * (15.0f - x2) / (15.0f - 6.0f * x2);
}

// The multiplier a sample that falls 60 dB in `seconds`.
inline float decayCoef (float seconds, float rate) { return std::exp (-6.9077553f / (std::fmax (seconds, 1e-4f) * rate)); }
// A one-pole low pass's coefficient for a corner at `hz`.
inline float onePoleCoef (float hz, float rate) { return 1.0f - std::exp (-2.0f * pi * std::fmin (hz, 0.45f * rate) / rate); }

// --- Noise -------------------------------------------------------------------------------------------------------
struct Noise
{
    uint32_t s = 0x9e3779b9u;
    float next()
    {
        s ^= s << 13;
        s ^= s >> 17;
        s ^= s << 5;
        return (float) (int32_t) s * (1.0f / 2147483648.0f);
    }
};

// A hash to [0, 1): the random choices that must come out the same for the same place in a song.
inline float hash01 (uint64_t x)
{
    x ^= x >> 33;
    x *= 0xff51afd7ed558ccdull;
    x ^= x >> 33;
    x *= 0xc4ceb9fe1a85ec53ull;
    x ^= x >> 33;
    return (float) (x >> 40) * (1.0f / 16777216.0f);
}

// --- State-variable filter ---------------------------------------------------------------------------------------
// Trapezoidal (zero-delay) form: low, band and high pass from one state. `g` is tan(pi f / rate), `k` 1/Q.
struct Svf
{
    float ic1 = 0.0f, ic2 = 0.0f;
    float a1 = 1.0f, a2 = 0.0f, a3 = 0.0f, k = 1.4142f;

    static float gOf (float hz, float rate) { return tanPade (pi * clampf (hz, 10.0f, 0.4f * rate) / rate); }
    void set (float g, float k_)
    {
        k = k_;
        a1 = 1.0f / (1.0f + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }
    void setHz (float hz, float k_, float rate) { set (gOf (hz, rate), k_); }
    // One sample: v1 the band pass, v2 the low pass; high = x - k band - low.
    void run (float x, float& low, float& band)
    {
        const float v3 = x - ic2;
        const float v1 = a1 * ic1 + a2 * v3;
        const float v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.0f * v1 - ic1;
        ic2 = 2.0f * v2 - ic2;
        low = v2, band = v1;
    }
    float lp (float x) { float l, b; run (x, l, b); return l; }
    float hp (float x) { float l, b; run (x, l, b); return x - k * b - l; }
    float bp (float x) { float l, b; run (x, l, b); return b * k; } // unity at its centre
    void clear() { ic1 = ic2 = 0.0f; }
};

// --- The resonator -----------------------------------------------------------------------------------------------
// A decaying rotation of one complex state: an impulse in rings as r^n sin(n w), its size the impulse's whatever the
// pitch, so a pitch sweep keeps its level (a filter's coefficients would not). A second hit adds to the ring as a
// bridged-T's second trigger does. c, s are r cos w and r sin w.
struct Resonator
{
    float re = 0.0f, im = 0.0f, c = 1.0f, s = 0.0f, r = 0.0f;

    void tune (float cycles, float r_) { r = r_, c = r * cos2pi (cycles), s = r * sin2pi (cycles); }
    void tune (float cycles) { c = r * cos2pi (cycles), s = r * sin2pi (cycles); }
    // The same for a low pitch (under about 1 kHz at 48 kHz: an angle under 0.13 a sample), by Taylor series to 1e-8:
    // a third of tune's cost, for the voices retuned every sample (the kick's glide and sigh, the toms' diodes).
    void tuneLow (float cycles)
    {
        if (cycles > 0.05f) // past about 2.4 kHz at 48 kHz the series' gain errs above 1: the full sine and cosine
        {
            tune (cycles);
            return;
        }
        const float w = 2.0f * pi * cycles, w2 = w * w;
        c = r * (1.0f - w2 * (0.5f - w2 * (1.0f / 24.0f)));
        s = r * w * (1.0f - w2 * (1.0f / 6.0f) * (1.0f - w2 * (1.0f / 20.0f)));
    }
    void strike (float x) { re += x; }
    float run()
    {
        const float nr = c * re - s * im;
        im = s * re + c * im;
        re = nr;
        return im;
    }
    // The band pass's answer, as a bridged-T gives it: the cosine phase, at its peak the moment it is struck (the
    // attack's snap), then rotated on.
    float ring()
    {
        const float y = re;
        const float nr = c * re - s * im;
        im = s * re + c * im;
        re = nr;
        return y;
    }
    float size() const { return std::fabs (re) + std::fabs (im); }
    void clear() { re = im = 0.0f; }
};

// --- Band-limited square -----------------------------------------------------------------------------------------
// Polynomial BLEP at both edges: cheap, and clean enough for the metal oscillators (a few hundred hertz into
// band-pass filters a few octaves up).
[[gnu::always_inline]] inline float blep (float t, float dt)
{
    if (t < dt)
    {
        t /= dt;
        return t + t - t * t - 1.0f;
    }
    if (t > 1.0f - dt)
    {
        t = (t - 1.0f) / dt;
        return t * t + t + t + 1.0f;
    }
    return 0.0f;
}
[[gnu::always_inline]] inline float squareAt (float phase, float dt)
{
    float half = phase + 0.5f;
    half -= half >= 1.0f ? 1.0f : 0.0f;
    return (phase < 0.5f ? 1.0f : -1.0f) + blep (phase, dt) - blep (half, dt);
}
// A rectangle high for `duty` of its cycle: the 808's Schmitt-trigger oscillators run at 47.98 % (Werner), which
// adds the even harmonics that fill the cowbell's and the metal's spectra between the odd ones.
[[gnu::always_inline]] inline float rectAt (float phase, float dt, float duty)
{
    float fall = phase + 1.0f - duty;
    fall -= fall >= 1.0f ? 1.0f : 0.0f;
    return (phase < duty ? 1.0f : -1.0f) + blep (phase, dt) - blep (fall, dt);
}

// The same, four oscillators at a time (the NEON unit's width): lane by lane the very operations above, so the
// very results. Both edges' polynomials are worked out and the right one picked.
using f4 = float __attribute__ ((vector_size (16)));
[[gnu::always_inline]] inline f4 blep4 (f4 t, f4 dt)
{
    const f4 one = { 1.0f, 1.0f, 1.0f, 1.0f }, none = {};
    const f4 a = t / dt, b = (t - 1.0f) / dt;
    f4 aa = a * a;
    asm ("" : "+w"(aa)); // rounded on its own, as the compiler does it in blep, not fused into the subtraction
    const f4 rise = a + a - aa - 1.0f, fall = b * b + b + b + 1.0f;
    return t < dt ? rise : (t > one - dt ? fall : none);
}
[[gnu::always_inline]] inline f4 rectAt4 (f4 phase, f4 dt, float duty)
{
    const f4 one = { 1.0f, 1.0f, 1.0f, 1.0f }, none = {}, high = one, low = -one;
    f4 fall = phase + 1.0f - duty;
    fall -= fall >= 1.0f ? one : none;
    return (phase < duty ? high : low) + blep4 (phase, dt) - blep4 (fall, dt);
}

// A one-pole high pass at a few hertz: what the rectifying bends add as DC comes out.
struct DcBlock
{
    float x1 = 0.0f, y1 = 0.0f, a = 0.9985f; // a: its pole, about 11.5 Hz (0.9985 at 48 kHz)
    float run (float x)
    {
        const float y = x - x1 + a * y1;
        x1 = x, y1 = y;
        return y;
    }
    void clear() { x1 = y1 = 0.0f; }
};
} // namespace foundry
