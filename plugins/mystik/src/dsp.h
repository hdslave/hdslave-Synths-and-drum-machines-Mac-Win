// Mystik's small DSP parts: fast math (each with its error stated), a hash for repeatable randomness, the half-band
// decimator, four-lane vectors for the membrane.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace mystik
{
// Two branch-free instructions (fmaxnm, fminnm); a NaN comes out as `lo`.
inline float clampf (float x, float lo, float hi) { return std::fmin (std::fmax (x, lo), hi); }

// sin(pi t) for t in [-1, 1]: t (1 - t^2) P(t^2), exact at 0 and +-1, within 6.6e-6 (-104 dB) of it.
[[gnu::always_inline]] inline float sinPi (float t)
{
    const float t2 = t * t;
    return t * (1.0f - t2) * (3.1415211f + t2 * (-2.02477257f + t2 * (0.517489406f + t2 * -0.063688005f)));
}

// sin(2 pi x) for any x (in turns): x folded to [-0.5, 0.5], then sinPi. About ten instructions.
[[gnu::always_inline]] inline float sinTurns (float x)
{
    const float f = x - std::rint (x);
    return sinPi (2.0f * f);
}

// tan(pi t) for t in [0, 0.49], as sin over cos (cos(pi t) = sin(pi (0.5 - t))): within 2e-4 relative at the top.
inline float tanPi (float t) { return sinPi (t) / sinPi (0.5f - t); }

// 2^x to about 3e-6 relative: the integer part by rounding, so the polynomial spans [-0.5, 0.5]. For |x| < 126.
inline float exp2Fast (float x)
{
    const float xi = std::rint (x), f = x - xi;
    const float p = 1.0f + f * (0.69314718f + f * (0.24022651f + f * (0.05550411f + f * (0.00961813f + f * 0.00133336f))));
    const int32_t bits = ((int32_t) xi + 127) << 23;
    float scale;
    std::memcpy (&scale, &bits, sizeof (scale));
    return p * scale;
}

// log2 of a positive normal float to about 3e-5: the exponent's bits, then a quintic on the mantissa in [1, 2).
inline float log2Fast (float x)
{
    int32_t bits;
    std::memcpy (&bits, &x, sizeof (bits));
    const float e = (float) ((bits >> 23) - 127);
    bits = (bits & 0x007fffff) | 0x3f800000;
    float m;
    std::memcpy (&m, &bits, sizeof (m));
    const float f = m - 1.0f;
    return e + f * (1.44182586f + f * (-0.70868274f + f * (0.415424104f + f * (-0.194425546f + f * 0.0458868439f))));
}

// tanh, rational, exact at 0 and +-3 where it reaches +-1 and holds: bounded, smooth, cheap.
[[gnu::always_inline]] inline float tanhFast (float x)
{
    x = clampf (x, -3.0f, 3.0f);
    const float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

// 1 - e^-a for a time constant's share a sample (a = 1 / (tau * rate), small): a / (1 + a/2), within a^2/12 relative.
// Exact exp is not needed, and exp(-a) near 1 would lose the very digits that set the time.
inline float onePoleCoeff (float a) { return a / (1.0f + 0.5f * a); }

// A number in [0, 1) from a count and a salt: the same for the same place in the song (Chance, Mutate, Random...).
inline float unit (int64_t n, uint32_t salt)
{
    uint64_t x = (uint64_t) n * 0x9e3779b97f4a7c15ull + salt;
    x ^= x >> 30, x *= 0xbf58476d1ce4e5b9ull, x ^= x >> 27, x *= 0x94d049bb133111ebull, x ^= x >> 31;
    return (float) (x >> 40) * (1.0f / 16777216.0f);
}
inline uint32_t bits32 (int64_t n, uint32_t salt)
{
    uint64_t x = (uint64_t) n * 0x9e3779b97f4a7c15ull + salt;
    x ^= x >> 30, x *= 0xbf58476d1ce4e5b9ull, x ^= x >> 27, x *= 0x94d049bb133111ebull, x ^= x >> 31;
    return (uint32_t) (x >> 32);
}

// Four floats as one NEON register (GCC's vector extension: plain arithmetic, lane by lane); two for left and right.
typedef float v4 __attribute__ ((vector_size (16)));
typedef float v2 __attribute__ ((vector_size (8)));
inline float sum (v4 a) { return (a[0] + a[1]) + (a[2] + a[3]); }
inline v4 splat (float x) { return v4 { x, x, x, x }; }
inline v4 vabs (v4 a) { return v4 { std::fabs (a[0]), std::fabs (a[1]), std::fabs (a[2]), std::fabs (a[3]) }; }
inline v4 sinPi4 (v4 t)
{
    const v4 t2 = t * t;
    return t * (1.0f - t2) * (3.1415211f + t2 * (-2.02477257f + t2 * (0.517489406f + t2 * -0.063688005f)));
}

// --- Half-band decimator ---------------------------------------------------------------------------------------
// From twice the host's rate to its rate: a 47-tap Kaiser-windowed half-band FIR (Anvil's design). Its even taps are zero
// but the centre, so a frame is 12 symmetric pairs: the odd samples are kept twice, oldest first and newest first, so
// both halves of each pair read forwards and the 12 pairs are three four-lane multiply-adds. Flat to about 19 kHz at
// 48 kHz, about 80 dB down from 29 kHz.
struct Decimator
{
    static constexpr int half = 12, len = 2 * half;
    v4 coeff[half / 4] {};
    float forward[2 * len] {}, backward[2 * len] {}; // each written twice, so a window reads straight
    float even[half] {};
    int at = 0, back = 0, evenAt = 0, quiet = 0;

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
        for (int i = 0; i < half; ++i)
            coeff[i / 4][i % 4] = (float) (h[i] * 0.5 / total);
        clear();
    }
    void clear()
    {
        std::memset (forward, 0, sizeof (forward));
        std::memset (backward, 0, sizeof (backward));
        std::memset (even, 0, sizeof (even));
        at = back = evenAt = 0;
        quiet = 2 * half + 1; // starts silent
    }
    bool silent() const { return quiet >= 2 * half + 1; }
    static v4 load (const float* p)
    {
        v4 v;
        std::memcpy (&v, p, sizeof (v));
        return v;
    }
    float push (float a, float b)
    {
        // forward + at + 1: b[m-len+1] .. b[m]; backward + back: b[m], b[m-1], ...
        forward[at] = forward[at + len] = b;
        back = back == 0 ? len - 1 : back - 1;
        backward[back] = backward[back + len] = b;
        at = at + 1 == len ? 0 : at + 1;
        even[evenAt] = a;
        quiet = (a == 0.0f && b == 0.0f) ? std::min (quiet + 1, 2 * half + 1) : 0;
        // sum over i of c[i] (b[m-half+1+i] + b[m-half-i]): the first from forward, the second from backward.
        const float* f = forward + at + half;
        const float* r = backward + back + half;
        const v4 acc = coeff[0] * (load (f) + load (r)) + coeff[1] * (load (f + 4) + load (r + 4)) + coeff[2] * (load (f + 8) + load (r + 8));
        evenAt = evenAt + 1 == half ? 0 : evenAt + 1;
        return sum (acc) + 0.5f * even[evenAt];
    }
};
} // namespace mystik
