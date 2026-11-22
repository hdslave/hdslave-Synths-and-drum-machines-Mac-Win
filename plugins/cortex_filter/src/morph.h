// The morphing filter's maths, shared by the sound (engine.h) and the screen (screen.h), which draws the response
// from the same numbers the sound uses: a cube's corners blended at a point (design), each band a trapezoidal
// state-variable filter (Simper's form, steady under fast modulation), and the response of the whole filter at any
// frequency (responseDb), exact for that filter.
#pragma once

#include "cubes.h"
#include "params.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace cortex
{
constexpr float pi = 3.14159265358979f;
constexpr float dbToLog2 = 0.166096404744f; // 10^(dB/20) = 2^(dB * this)

// A point in the cube: Morph, Freq, Transform, each 0..1.
struct Position
{
    float x = 0.0f, y = 0.0f, z = 0.0f;
    bool operator== (const Position& o) const { return x == o.x && y == o.y && z == o.z; }
    bool operator!= (const Position& o) const { return ! (*this == o); }
};

// A cube as the filter reads it: each band's settings at the eight corners, on the scales they blend on.
enum Field { logHz, logQ, dryMix, lpMix, bpMix, hpMix, bellDb, blendFieldCount };
struct CubeTable
{
    int bands = 0; // bands some corner uses; the rest pass everything everywhere and are skipped
    float band[maxBands][blendFieldCount][8] {};
    bool bell[maxBands] {}; // some corner has a bell on the band
    float gainDb[8] {}, dry[8] {};
};

inline bool atRest (const Band& b) { return b.dry == 1.0f && b.lp == 0.0f && b.bp == 0.0f && b.hp == 0.0f && b.bellDb == 0.0f; }

// Built once, on first use (the plug-in asks at init, on the main thread).
inline const CubeTable* cubeTables()
{
    static const CubeTable* const tables = [] {
        static CubeTable t[cubeCount];
        for (int c = 0; c < cubeCount; ++c)
        {
            const CubeSpec& spec = cubeSpecs[c];
            for (int corner = 0; corner < 8; ++corner)
            {
                const Frame& f = spec.corner[corner];
                for (int b = 0; b < maxBands; ++b)
                {
                    const Band& band = f.band[b];
                    t[c].band[b][logHz][corner] = std::log2 (band.hz);
                    t[c].band[b][logQ][corner] = std::log2 (band.q);
                    t[c].band[b][dryMix][corner] = band.dry;
                    t[c].band[b][lpMix][corner] = band.lp;
                    t[c].band[b][bpMix][corner] = band.bp;
                    t[c].band[b][hpMix][corner] = band.hp;
                    t[c].band[b][bellDb][corner] = band.bellDb;
                    if (! atRest (band))
                        t[c].bands = std::max (t[c].bands, b + 1);
                    t[c].bell[b] |= band.bellDb != 0.0f;
                }
                t[c].gainDb[corner] = f.gainDb;
                t[c].dry[corner] = f.dry;
            }
        }
        return t;
    }();
    return tables;
}
inline const CubeTable& cubeTable (int index) { return cubeTables()[std::clamp (index, 0, cubeCount - 1)]; }

// 2^x for |x| < 126, to about 1e-7: 2^round(x) from the float's exponent bits, 2^f for |f| <= 1/2 from its series.
inline float fastExp2 (float x)
{
    x = std::clamp (x, -126.0f, 126.0f);
    const float n = std::nearbyint (x), f = (x - n) * 0.6931471805599453f; // e^f now
    const float p = 1.0f + f * (1.0f + f * (0.5f + f * (1.0f / 6.0f + f * (1.0f / 24.0f + f * (1.0f / 120.0f + f * (1.0f / 720.0f + f * (1.0f / 5040.0f)))))));
    union { uint32_t bits; float value; } scale { (uint32_t) ((int32_t) n + 127) << 23 };
    return p * scale.value;
}

// tan(w) for 0 <= w <= 1.42 (a band's pi f / rate, f up to 0.45 of the rate), to about 1e-6 (the cosine near the top
// is small, 0.16, so its rounding weighs most there): sin over cos, each from its series.
inline float fastTan (float w)
{
    const float w2 = w * w;
    const float s = w * (1.0f + w2 * (-1.0f / 6.0f + w2 * (1.0f / 120.0f + w2 * (-1.0f / 5040.0f + w2 * (1.0f / 362880.0f + w2 * (-1.0f / 39916800.0f + w2 * (1.0f / 6227020800.0f)))))));
    const float c = 1.0f + w2 * (-0.5f + w2 * (1.0f / 24.0f + w2 * (-1.0f / 720.0f + w2 * (1.0f / 40320.0f + w2 * (-1.0f / 3628800.0f + w2 * (1.0f / 479001600.0f + w2 * (-1.0f / 87178291200.0f)))))));
    return s / c;
}

// How much of each corner a point takes (they sum to 1).
inline void cornerWeights (const Position& p, float w[8])
{
    const float x1 = p.x, x0 = 1.0f - p.x, y1 = p.y, y0 = 1.0f - p.y, z1 = p.z, z0 = 1.0f - p.z;
    w[0] = x0 * y0 * z0, w[1] = x1 * y0 * z0, w[2] = x0 * y1 * z0, w[3] = x1 * y1 * z0;
    w[4] = x0 * y0 * z1, w[5] = x1 * y0 * z1, w[6] = x0 * y1 * z1, w[7] = x1 * y1 * z1;
}

// One band as the sound runs it: y = m0 x + m1 BP + m2 LP of a state-variable filter with a1..a3 from its frequency
// and Q (BP here unnormalized: its peak is Q).
struct BandCoefs
{
    float a1, a2, a3, m0, m1, m2;
};
struct BandState
{
    float ic1 = 0.0f, ic2 = 0.0f;
};

// The filter at a point: its bands, then wet * (the bands' output) + dryGain * (their input). g and k (the
// frequency's prewarped tangent and 1 / Q) are kept for the response.
struct Design
{
    int bands = 0;
    BandCoefs band[maxBands] {};
    float g[maxBands] {}, k[maxBands] {};
    float wet = 1.0f, dryGain = 0.0f;
};

// The corners' weights, four to a vector (a NEON register on the MPC), and a value blended by them: eight corners in
// two multiplies and a sum.
typedef float Quad __attribute__ ((vector_size (16)));
struct Weights
{
    Quad low, high; // corners 0-3, 4-7
};
inline Weights weightsOf (const Position& p)
{
    float w[8];
    cornerWeights (p, w);
    Weights out;
    std::memcpy (&out.low, w, sizeof (Quad));
    std::memcpy (&out.high, w + 4, sizeof (Quad));
    return out;
}
inline float blend (const float* v, const Weights& w)
{
    Quad low, high;
    std::memcpy (&low, v, sizeof (Quad));
    std::memcpy (&high, v + 4, sizeof (Quad));
    const Quad s = low * w.low + high * w.high;
    return (s[0] + s[1]) + (s[2] + s[3]);
}

// The filter at `p` in cube `c`, Emphasis `emphasis` (-1..1), at sample rate `rate`.
inline void design (const CubeTable& c, const Position& p, float emphasis, float rate, Design& d)
{
    const Weights w = weightsOf (p);
    const float qShift = emphasis * (emphasis < 0.0f ? emphasisWideOctaves : emphasisOctaves), piOverRate = pi / rate;
    const float lowest = 3.321928f, highest = std::log2 (0.45f * rate); // log2 of 10 Hz and of 0.45 of the rate
    constexpr float leastLogQ = -3.321928f, mostLogQ = 6.321928f;     // log2 of minQ and maxQ
    static_assert (minQ == 0.1f && maxQ == 80.0f, "the log2 above are these");
    // Emphasis below 0 widens the bands; a wider boost would add as much more energy as it is wider (its energy goes
    // as its gain squared times its width), so a boost comes down by as many dB as that, to no less than half.
    const float widen = qShift < 0.0f ? 3.0103f * qShift : 0.0f;
    d.bands = c.bands;
    for (int b = 0; b < c.bands; ++b)
    {
        const auto& f = c.band[b];
        const float logAt = blend (f[logHz], w);
        const float hz = fastExp2 (std::clamp (logAt, lowest, highest));
        const float k = fastExp2 (-std::clamp (blend (f[logQ], w) + qShift, leastLogQ, mostLogQ)); // 1 / Q
        const float g = fastTan (hz * piOverRate);
        const float a1 = 1.0f / (1.0f + g * (g + k)), a2 = g * a1, a3 = g * a2;
        float bell = 0.0f;
        if (c.bell[b])
        {
            float db = blend (f[bellDb], w);
            if (db > 0.0f && widen < 0.0f)
                db = std::max (0.5f * db, db + widen);
            // A band pushed past the top of the audio band (0.45 of the rate) stays there, so its bell fades flat over
            // the next half octave rather than piling up on the others there.
            if (logAt > highest)
                db *= std::max (0.0f, 1.0f - 2.0f * (logAt - highest));
            bell = fastExp2 (db * dbToLog2) - 1.0f;
        }
        const float dry = blend (f[dryMix], w), lp = blend (f[lpMix], w), bp = blend (f[bpMix], w) + bell, hp = blend (f[hpMix], w);
        d.band[b] = { a1, a2, a3, dry + hp, k * (bp - hp), lp - hp };
        d.g[b] = g, d.k[b] = k;
    }
    const float gain = fastExp2 (blend (c.gainDb, w) * dbToLog2), parallel = blend (c.dry, w);
    d.wet = gain * (1.0f - parallel);
    d.dryGain = gain * parallel;
}

// One sample through one band.
__attribute__ ((always_inline)) inline float runBand (const BandCoefs& c, BandState& s, float x)
{
    const float v3 = x - s.ic2;
    const float v1 = c.a1 * s.ic1 + c.a2 * v3;
    const float v2 = s.ic2 + c.a2 * s.ic1 + c.a3 * v3;
    s.ic1 = 2.0f * v1 - s.ic1;
    s.ic2 = 2.0f * v2 - s.ic2;
    return c.m0 * x + c.m1 * v1 + c.m2 * v2;
}

// The filter's gain at a frequency whose tan(pi f / rate) is `t`, in dB (down to -200). Each band is the analog
// prototype at s = j t / g, which is what the trapezoidal filter is.
inline float responseDbAt (const Design& d, double t)
{
    double re = 1.0, im = 0.0;
    for (int b = 0; b < d.bands; ++b)
    {
        const BandCoefs& c = d.band[b];
        const double w = t / d.g[b], dr = 1.0 - w * w, di = d.k[b] * w;
        const double nr = c.m0 * dr + c.m2, ni = c.m0 * di + c.m1 * w;
        const double den = dr * dr + di * di;
        const double hr = (nr * dr + ni * di) / den, hi = (ni * dr - nr * di) / den;
        const double r2 = re * hr - im * hi, i2 = re * hi + im * hr;
        re = r2, im = i2;
    }
    re = d.wet * re + d.dryGain, im = d.wet * im;
    return (float) (10.0 * std::log10 (re * re + im * im + 1e-20));
}

// The same at `hz`.
inline double tanOf (float hz, float rate) { return std::tan ((double) pi * std::min ((double) hz, 0.4999 * rate) / rate); }
inline float responseDb (const Design& d, float hz, float rate) { return responseDbAt (d, tanOf (hz, rate)); }
} // namespace cortex
