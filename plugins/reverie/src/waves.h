// Reverie's waves: twelve sources made for pads, each four frames that Position morphs between (crossfading two
// neighbours: their harmonics share phases, so the crossfade is a smooth morph of the spectrum, never a phasey
// cancellation). Every frame is built once, band-limited, for ten ranges of pitch: level L holds harmonics up to
// 512 / 2^L in a table eight times as long (at least 64 samples), so linear interpolation stays clean (its images
// under -45 dB). The bank is
// made the first time a plug-in is created (about 20 ms) and shared by every instance; it is read only after that.
//
// The oscillators read a segment (the span between frame f and f + 1) laid out for one 128-bit load a sample: each
// point i holds the pair (frame f at i, frame f + 1 less frame f at i), so the four floats at 2i are both frames at
// i and i + 1, and the morph is value + position * difference.
#pragma once

#include "params.h"

#include <cmath>
#include <complex>
#include <cstdint>
#include <cstring>
#include <vector>

namespace reverie
{
constexpr int waveFrames = 4;
constexpr int waveLevels = 10;
constexpr int topHarmonics = 512;

struct WaveTable
{
    const float* data; // length + 1 samples (the first repeated at the end)
    uint32_t shift;    // 32 - log2 (length): a 32-bit phase's index is phase >> shift
    uint32_t length;
};

class WaveBank
{
public:
    static const WaveBank& get()
    {
        static const WaveBank bank;
        return bank;
    }

    const WaveTable& table (int wave, int frame, int level) const { return tables[((wave * waveFrames) + frame) * waveLevels + level]; }
    // Segment `seg` (frames seg and seg + 1) at a level: 2 (length + 1) floats, as above; the same shift as the table.
    const float* segment (int wave, int seg, int level) const { return segments[((wave * (waveFrames - 1)) + seg) * waveLevels + level]; }
    static constexpr int harmonicsAt (int level) { return topHarmonics >> level; }
    static constexpr uint32_t lengthAt (int level) { return (uint32_t) std::max (64, 8 * (topHarmonics >> level)); }

    // The level whose harmonics stay under `limit` (as a share of the sample rate) at `cycles` a sample.
    static int levelFor (float cycles, float limit = 0.46f)
    {
        // smallest L with (512 >> L) * cycles <= limit: L = ceil (log2 (512 * cycles / limit)), from the float's bits
        const float r = (float) topHarmonics * cycles / limit;
        if (! (r > 1.0f)) return 0;
        uint32_t bits;
        std::memcpy (&bits, &r, 4);
        const int level = (int) ((bits >> 23) & 0xff) - 127 + ((bits & 0x7fffff) != 0 ? 1 : 0);
        return level < waveLevels ? level : waveLevels - 1;
    }

private:
    std::vector<float> storage, pairs;
    WaveTable tables[waveCount * waveFrames * waveLevels] {};
    const float* segments[waveCount * (waveFrames - 1) * waveLevels] {};

    using Spectrum = std::vector<double>; // amplitudes of harmonics 1..512 (index h - 1); negative flips the phase

    WaveBank()
    {
        size_t total = 0;
        for (int l = 0; l < waveLevels; ++l) total += lengthAt (l) + 1;
        storage.resize (total * waveCount * waveFrames);
        float* at = storage.data();
        for (int w = 0; w < waveCount; ++w)
        {
            std::vector<double> phase (topHarmonics);
            phases (w, phase);
            for (int f = 0; f < waveFrames; ++f)
            {
                Spectrum a (topHarmonics, 0.0);
                spectrum (w, f, a);
                // The same loudness at every frame: the full table's RMS set to 0.3.
                double power = 0.0;
                for (double x : a) power += 0.5 * x * x;
                const double gain = power > 0.0 ? 0.3 / std::sqrt (power) : 0.0;
                for (int l = 0; l < waveLevels; ++l)
                {
                    const uint32_t n = lengthAt (l);
                    synthesize (a, phase, harmonicsAt (l), n, gain, at);
                    WaveTable& t = tables[((w * waveFrames) + f) * waveLevels + l];
                    t.data = at;
                    t.length = n;
                    t.shift = 32u - (uint32_t) std::lround (std::log2 ((double) n));
                    at += n + 1;
                }
            }
        }
        pairs.resize (2 * total * waveCount * (waveFrames - 1) + 4);
        float* out = pairs.data();
        for (int w = 0; w < waveCount; ++w)
            for (int s = 0; s < waveFrames - 1; ++s)
                for (int l = 0; l < waveLevels; ++l)
                {
                    const WaveTable& a = table (w, s, l);
                    const WaveTable& b = table (w, s + 1, l);
                    segments[((w * (waveFrames - 1)) + s) * waveLevels + l] = out;
                    for (uint32_t i = 0; i <= a.length; ++i)
                    {
                        *out++ = a.data[i];
                        *out++ = b.data[i] - a.data[i];
                    }
                }
    }

    // A fixed pseudo-random phase a harmonic, per wave (low crest factor, the same at every frame).
    static double hashPhase (int wave, int h)
    {
        uint32_t x = (uint32_t) (wave * 7919 + h * 104729) ^ 0x9e3779b9u;
        x ^= x >> 16; x *= 0x7feb352du; x ^= x >> 15; x *= 0x846ca68bu; x ^= x >> 16;
        return (double) x / 4294967296.0 * 2.0 * M_PI;
    }

    static void phases (int w, std::vector<double>& phase)
    {
        const bool natural = w == 0 || w == 1 || w == 3 || w == 6 || w == 8 || w == 11; // the shapes look as named
        for (int h = 1; h <= topHarmonics; ++h) phase[h - 1] = natural ? 0.0 : hashPhase (w, h);
    }

    static double bump (double x, double centre, double width) { const double d = (x - centre) / width; return std::exp (-0.5 * d * d); }
    static double resonance (double f, double centre, double bandwidth) { const double d = (f - centre) / bandwidth; return 1.0 / (1.0 + d * d); }

    // Each wave's four frames, as harmonic amplitudes.
    static void spectrum (int w, int f, Spectrum& a)
    {
        const double ref = 220.0; // the pitch the formants are tuned for (they move with the note, as a wavetable's do)
        for (int h = 1; h <= topHarmonics; ++h)
        {
            const double x = h, hz = ref * h;
            double v = 0.0;
            switch (w)
            {
                case 0: // Velvet Saw: soft, full, brighter, a glowing band
                {
                    static const double roll[waveFrames] = { 22.0, 70.0, 1e9, 1e9 };
                    v = 1.0 / x * std::exp (-(x - 1.0) / roll[f]);
                    if (f == 3) v *= 1.0 + 1.8 * bump (std::log2 (x), 3.3, 0.45);
                    break;
                }
                case 1: // Hollow Pulse: square to a narrow pulse
                {
                    static const double duty[waveFrames] = { 0.5, 0.36, 0.22, 0.11 };
                    v = std::sin (M_PI * x * duty[f]) / x;
                    break;
                }
                case 2: // Choir: Oo, Oh, Ah, Eh
                {
                    static const double F[waveFrames][3] = { { 325, 700, 2530 }, { 450, 800, 2830 }, { 730, 1090, 2440 }, { 530, 1840, 2480 } };
                    static const double B[3] = { 90, 110, 170 }, G[3] = { 1.0, 0.55, 0.22 };
                    double env = 0.012;
                    for (int k = 0; k < 3; ++k) env += G[k] * resonance (hz, F[f][k], B[k]);
                    v = env / std::sqrt (x) * std::exp (-hz / 9000.0);
                    break;
                }
                case 3: // Strings: bowed, the bow at four places, a body
                {
                    static const double beta[waveFrames] = { 1.0 / 7.3, 1.0 / 9.6, 1.0 / 5.4, 1.0 / 12.5 };
                    const double body = 1.0 + 0.7 * resonance (hz, 440, 160) + 0.45 * resonance (hz, 2600, 900);
                    v = (0.2 + 0.8 * std::fabs (std::sin (M_PI * x * beta[f]))) / x * body * std::exp (-hz / 12000.0);
                    if (h % 2 == 0) v = -v;
                    break;
                }
                case 4: // Drawbar: flute, jazz, full, gospel
                {
                    static const double bars[waveFrames][9] = { { 1, 0.65, 0, 0, 0, 0, 0, 0, 0 },
                                                                { 1, 0.75, 0.55, 0.45, 0, 0, 0, 0, 0 },
                                                                { 1, 0.8, 0.8, 0.65, 0, 0.45, 0, 0.35, 0 },
                                                                { 0.9, 0.85, 0.85, 0.8, 0.5, 0.6, 0, 0.55, 0.3 } };
                    static const int at[9] = { 1, 2, 3, 4, 5, 6, 7, 8, 10 };
                    for (int k = 0; k < 9; ++k) if (at[k] == h) v = bars[f][k];
                    if (f == 3 && (h == 12 || h == 16)) v = 0.25;
                    break;
                }
                case 5: // Crystal: sparse partials up high (glass)
                {
                    static const int sets[waveFrames][7] = { { 1, 3, 5, 0, 0, 0, 0 }, { 1, 4, 9, 16, 0, 0, 0 },
                                                             { 1, 5, 8, 13, 21, 0, 0 }, { 1, 7, 11, 17, 23, 31, 0 } };
                    for (int k = 0; k < 7; ++k)
                        if (sets[f][k] == h) v = k == 0 ? 1.0 : 0.72 * std::pow (0.82, k);
                    break;
                }
                case 6: // Spectra: a resonant band swept up the harmonics
                {
                    static const double centre[waveFrames] = { 1.6, 3.0, 4.4, 5.8 };
                    v = 0.12 / x + bump (std::log2 (x), centre[f], 0.38) / std::sqrt (x);
                    break;
                }
                case 7: // Octaves: octave stacks, then fifths among them
                {
                    const int p = (int) std::lround (std::log2 (x));
                    static const double tilt[waveFrames] = { 0.55, 0.72, 0.86, 0.86 };
                    if ((1 << p) == h) v = std::pow (tilt[f], p);
                    if (f >= 2 && h % 3 == 0)
                    {
                        const int q = (int) std::lround (std::log2 (x / 3.0));
                        if (3 * (1 << q) == h) v = (f == 2 ? 0.25 : 0.5) * std::pow (tilt[f], q + 1);
                    }
                    break;
                }
                case 8: // Warm: triangle to a soft square
                {
                    const bool odd = h % 2 == 1;
                    if (f == 0) v = odd ? ((h / 2) % 2 == 0 ? 1.0 : -1.0) / (x * x) : 0.0;
                    else if (f == 1) v = 1.0 / std::pow (x, 1.7);
                    else if (f == 2) v = 1.0 / std::pow (x, 1.35);
                    else v = odd ? std::exp (-x / 28.0) / x : 0.08 / (x * x);
                    break;
                }
                case 9: // Breath: dense, airy, shaped
                {
                    static const double tilt[waveFrames] = { 0.85, 0.7, 0.55, 0.45 };
                    static const double centre[waveFrames] = { 900, 1500, 2600, 4200 };
                    const double r = 0.35 + 0.65 * (0.5 + 0.5 * std::sin (hashPhase (9 + f, h)));
                    v = r / std::pow (x, tilt[f]) * (0.25 + resonance (hz, centre[f], centre[f] * 0.9));
                    break;
                }
                case 10: // Reed: clarinet to oboe
                {
                    static const double even[waveFrames] = { 0.04, 0.25, 0.55, 0.85 };
                    static const double formant[waveFrames] = { 1500, 1300, 1150, 1800 };
                    v = (h % 2 == 1 ? 1.0 : even[f]) / std::pow (x, 1.1) * (0.4 + resonance (hz, formant[f], 600));
                    break;
                }
                default: // Sine: pure, then gentle organ partials
                {
                    static const double mix[waveFrames][4] = { { 1, 0, 0, 0 }, { 1, 0.25, 0, 0 }, { 1, 0.35, 0.15, 0 }, { 1, 0.5, 0.3, 0.2 } };
                    if (h <= 4) v = mix[f][h - 1];
                    break;
                }
            }
            a[(size_t) (h - 1)] = v;
        }
    }

    // x[i] = gain * sum over h <= harmonics of a[h] sin (2 pi h i / n + phase[h]), by an inverse FFT.
    static void synthesize (const Spectrum& a, const std::vector<double>& phase, int harmonics, uint32_t n, double gain, float* out)
    {
        std::vector<std::complex<double>> x (n);
        const int top = std::min<int> (harmonics, (int) n / 2 - 1);
        for (int h = 1; h <= top; ++h)
        {
            // sin (t + p) = Im (e^{i (t + p)}): the bin h coefficient is -i/2 e^{ip}, the bin n - h its conjugate
            const std::complex<double> c = std::polar (0.5 * a[(size_t) (h - 1)], phase[(size_t) (h - 1)] - M_PI / 2.0);
            x[(size_t) h] = c;
            x[(size_t) (n - (uint32_t) h)] = std::conj (c);
        }
        fft (x, true);
        for (uint32_t i = 0; i < n; ++i) out[i] = (float) (gain * x[i].real());
        out[n] = out[0];
    }

    static void fft (std::vector<std::complex<double>>& x, bool inverse)
    {
        const size_t n = x.size();
        for (size_t i = 1, j = 0; i < n; ++i)
        {
            size_t bit = n >> 1;
            for (; j & bit; bit >>= 1) j ^= bit;
            j ^= bit;
            if (i < j) std::swap (x[i], x[j]);
        }
        for (size_t len = 2; len <= n; len <<= 1)
        {
            const double angle = (inverse ? 2.0 : -2.0) * M_PI / (double) len;
            const std::complex<double> step (std::cos (angle), std::sin (angle));
            for (size_t i = 0; i < n; i += len)
            {
                std::complex<double> w (1.0, 0.0);
                for (size_t k = 0; k < len / 2; ++k)
                {
                    const std::complex<double> u = x[i + k], v = x[i + k + len / 2] * w;
                    x[i + k] = u + v;
                    x[i + k + len / 2] = u - v;
                    w *= step;
                }
            }
        }
    }
};
} // namespace reverie
