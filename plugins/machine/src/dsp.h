// The small pieces every machine is made of: phase-accumulator sines from a table, noise, a state-variable filter,
// a soft clip and a fast exp2. Everything here is plain float code the compiler keeps inline; nothing allocates.
#pragma once

#include <cmath>
#include <cstdint>
#include <cstring>

namespace machine::dsp
{
constexpr float pi = 3.14159265358979f;
constexpr float cycle = 4294967296.0f; // one turn of a 32-bit phase

// Sine by table: 2048 points and linear interpolation (error about 1e-6), from a 32-bit phase.
struct SineTable
{
    float v[2049];
    SineTable()
    {
        for (int i = 0; i <= 2048; ++i)
            v[i] = (float) std::sin (2.0 * 3.14159265358979323846 * i / 2048.0);
    }
};
inline const SineTable sineTable {};
inline float sine (uint32_t phase)
{
    const uint32_t i = phase >> 21;
    const float f = (float) (phase & 0x1fffffu) * (1.0f / 2097152.0f);
    const float a = sineTable.v[i];
    return a + (sineTable.v[i + 1] - a) * f;
}
// A phase offset from turns (any size: wraps).
inline uint32_t turns (float t) { return (uint32_t) (int64_t) (t * cycle); }
// The phase step of `hz` at `rate`, kept under half a turn.
inline uint32_t step (float hz, float invRate)
{
    float t = hz * invRate;
    t = t < 0.0f ? 0.0f : t > 0.49f ? 0.49f : t;
    return (uint32_t) (t * cycle);
}

// 2^x within about 2e-4 relative (a third of a cent): the integer part into the exponent, a cubic for the rest. For
// sweeps; notes use std::exp2.
inline float exp2Fast (float x)
{
    x = x < -60.0f ? -60.0f : x > 60.0f ? 60.0f : x;
    const float fl = std::floor (x);
    const float f = x - fl;
    const float p = 1.0f + f * (0.6939825f + f * (0.2312795f + f * 0.0747380f));
    uint32_t bits;
    std::memcpy (&bits, &p, 4);
    bits += (uint32_t) (int32_t) fl << 23;
    float out;
    std::memcpy (&out, &bits, 4);
    return out;
}

// A soft clip: 3x/(1+...) shaped, flat beyond +-3.
inline float softClip (float x)
{
    x = x < -3.0f ? -3.0f : x > 3.0f ? 3.0f : x;
    return x * (27.0f + x * x) / (27.0f + 9.0f * x * x);
}

// White noise, -1..1, from a 32-bit xorshift.
struct Noise
{
    uint32_t s = 0x9e3779b9u;
    void seed (uint32_t v) { s = v != 0 ? v : 0x9e3779b9u; }
    float next()
    {
        s ^= s << 13;
        s ^= s >> 17;
        s ^= s << 5;
        return (float) (int32_t) s * (1.0f / 2147483648.0f);
    }
};

// A multiplier that falls to 1/1000 (-60 dB) in `seconds`.
inline float decayCoef (float seconds, float rate)
{
    if (seconds <= 0.0f) return 0.0f;
    return std::exp (-6.9077553f / (seconds * rate));
}
// One-pole low-pass coefficient for `hz`.
inline float onePole (float hz, float rate)
{
    const float a = 1.0f - std::exp (-2.0f * pi * hz / rate);
    return a < 0.0f ? 0.0f : a > 1.0f ? 1.0f : a;
}

// The state-variable filter (trapezoidal, zero-delay feedback): low, band and high pass at once.
struct Svf
{
    float ic1 = 0.0f, ic2 = 0.0f;
    float k = 2.0f, a1 = 1.0f, a2 = 0.0f, a3 = 0.0f;
    void reset() { ic1 = ic2 = 0.0f; }
    // `q` 0..1: 0 a gentle slope, 1 at the edge of ringing for ever.
    void set (float hz, float q, float rate)
    {
        const float top = rate * 0.45f;
        hz = hz < 10.0f ? 10.0f : hz > top ? top : hz;
        const float g = std::tan (pi * hz / rate);
        k = 2.0f - 1.97f * (q < 0.0f ? 0.0f : q > 1.0f ? 1.0f : q);
        a1 = 1.0f / (1.0f + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }
    void run (float x, float& low, float& band, float& high)
    {
        const float v3 = x - ic2;
        const float v1 = a1 * ic1 + a2 * v3;
        const float v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.0f * v1 - ic1;
        ic2 = 2.0f * v2 - ic2;
        low = v2;
        band = v1;
        high = x - k * v1 - v2;
    }
    float low (float x) { float l, b, h; run (x, l, b, h); return l; }
    float band (float x) { float l, b, h; run (x, l, b, h); return b; }
    float high (float x) { float l, b, h; run (x, l, b, h); return h; }
};
} // namespace machine::dsp
