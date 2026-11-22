// Foundry's send effects: a reverb (eight delay lines in a feedback network, gently modulated, after a pre-delay
// and a low cut) and a tempo-synced delay (mono, ping-pong or tape). Both sleep while they have nothing to play,
// and wake on the next send; that is decided by the signal alone, so the output does not depend on block sizes.
#pragma once

#include "dsp.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace foundry
{
struct Reverb
{
    static constexpr int lines = 8;
    std::vector<float> buf[lines];
    int len[lines] {}, size[lines] {}, w[lines] {};
    std::vector<float> pre;
    int preSize = 1, preW = 0, preDelay = 0;
    std::vector<float> ap[2];
    int apSize[2] {}, apW[2] {};
    float g[lines] {}, lp[lines] {}, modPh[lines] {}, modInc[lines] {};
    float dampA = 0.5f, lowState = 0.0f, lowA = 0.01f, modDepth = 0.0f, level = 0.5f;
    int quiet = 0, sleepAfter = 1;
    bool asleep = true;
    float rate = 48000.0f;

    void prepare (float sampleRate)
    {
        rate = sampleRate;
        static constexpr int base[lines] = { 1557, 1617, 1491, 1422, 1277, 1356, 1188, 1116 };
        const float scale = rate / 48000.0f;
        int longest = 0;
        for (int i = 0; i < lines; ++i)
        {
            len[i] = (int) std::lround (base[i] * scale);
            size[i] = len[i] + (int) (16.0f * scale) + 4;
            buf[i].assign ((size_t) size[i] + 2, 0.0f); // and two guards: copies of the first two samples, after the last
            w[i] = 0;
            modPh[i] = 0.13f * (float) i;
            modInc[i] = (0.31f + 0.07f * (float) i) / rate;
            longest = std::max (longest, len[i]);
        }
        static constexpr int apBase[2] = { 142, 107 };
        for (int a = 0; a < 2; ++a)
        {
            apSize[a] = (int) std::lround (apBase[a] * scale);
            ap[a].assign ((size_t) apSize[a], 0.0f);
            apW[a] = 0;
        }
        preSize = (int) (0.21f * rate) + 2;
        pre.assign ((size_t) preSize, 0.0f);
        preW = 0;
        sleepAfter = 6 * longest + preSize;
        clear();
    }
    void clear()
    {
        for (int i = 0; i < lines; ++i)
            std::fill (buf[i].begin(), buf[i].end(), 0.0f), lp[i] = 0.0f;
        for (auto& a : ap)
            std::fill (a.begin(), a.end(), 0.0f);
        std::fill (pre.begin(), pre.end(), 0.0f);
        lowState = 0.0f;
        asleep = true;
        quiet = 0;
    }
    // Time: seconds to fall 60 dB; tone 0..1 (dark to bright); pre-delay ms; low cut Hz; mod 0..1.
    void set (float seconds, float tone, float preMs, float lowCut, float mod, float lvl)
    {
        for (int i = 0; i < lines; ++i)
            g[i] = std::exp (-6.9077553f * (float) len[i] / (seconds * rate));
        dampA = onePoleCoef (1500.0f * std::pow (16.0f, tone), rate);
        preDelay = std::min (preSize - 1, (int) (preMs * 0.001f * rate));
        lowA = onePoleCoef (lowCut, rate);
        modDepth = mod * 12.0f * rate / 48000.0f;
        level = lvl;
    }

    // Adds the reverb of `in` (n samples) to left and right. Asleep, it wakes on the first sample sent; it falls
    // asleep on the sample its tail has been silent long enough.
    void process (const float* in, float* left, float* right, int n)
    {
        int s = 0;
        if (asleep)
        {
            while (s < n && in[s] == 0.0f)
                ++s;
            if (s == n)
                return;
            asleep = false, quiet = 0;
        }
        float* line[lines];
        for (int i = 0; i < lines; ++i)
            line[i] = buf[i].data();
        float* const delayed = pre.data();
        float* const diffuse[2] = { ap[0].data(), ap[1].data() };
        for (; s < n; ++s)
        {
            const float x = in[s];
            delayed[preW] = x;
            int r = preW - preDelay;
            r += r < 0 ? preSize : 0;
            float v = delayed[r];
            preW = preW + 1 == preSize ? 0 : preW + 1;
            lowState += (v - lowState) * lowA;
            v -= lowState;
            for (int a = 0; a < 2; ++a)
            {
                const float held = diffuse[a][apW[a]];
                const float y = held - 0.6f * v;
                diffuse[a][apW[a]] = v + 0.6f * y;
                apW[a] = apW[a] + 1 == apSize[a] ? 0 : apW[a] + 1;
                v = y;
            }
            // The lines' read points, each swung by its LFO: one loop of their own, which the compiler runs four
            // lines at a time (the same operations, so the same result).
            float offset[lines];
            for (int i = 0; i < lines; ++i)
            {
                modPh[i] += modInc[i];
                modPh[i] -= modPh[i] >= 1.0f ? 1.0f : 0.0f;
                offset[i] = (float) len[i] + modDepth * 0.5f * (1.0f + sin2pi (modPh[i]));
            }
            float d[lines], sum = 0.0f;
            for (int i = 0; i < lines; ++i)
            {
                float at = (float) w[i] - offset[i];
                at += at < 0.0f ? (float) size[i] : 0.0f; // a hair under 0, plus the size, can round up to it: the guards
                const int i0 = (int) at;
                const float frac = at - (float) i0;
                const float* const b = line[i];
                const float y = b[i0] + frac * (b[i0 + 1] - b[i0]); // past the end, the guards: the first samples
                lp[i] += (y - lp[i]) * dampA;
                d[i] = lp[i];
                sum += d[i];
            }
            sum *= 2.0f / (float) lines;
            for (int i = 0; i < lines; ++i)
            {
                const float into = (d[i] - sum) * g[i] + ((i & 1) ? -0.35f : 0.35f) * v;
                line[i][w[i]] = into;
                if (w[i] < 2)
                    line[i][size[i] + w[i]] = into; // the guards keep up
                w[i] = w[i] + 1 == size[i] ? 0 : w[i] + 1;
            }
            const float l = (d[0] - d[2] + d[4] - d[6]) * 0.5f * level, rr = (d[1] - d[3] + d[5] - d[7]) * 0.5f * level;
            left[s] += l;
            right[s] += rr;
            if (x == 0.0f && std::fabs (l) + std::fabs (rr) < 1e-7f)
            {
                if (++quiet > sleepAfter)
                {
                    asleep = true;
                    return;
                }
            }
            else
                quiet = 0;
        }
    }
};

// Its time glides to a new setting while it plays (the echoes in flight bend, as a tape's do), and is set at once
// when it wakes: asleep, the line is silent, and the first echo comes on time and at pitch.
struct Delay
{
    std::vector<float> bl, br;
    int size = 1, w = 0;
    float cur = 1.0f, target = 1.0f, feedback = 0.0f, toneA = 0.5f, hpA = 0.005f, level = 0.5f, toRev = 0.0f;
    float lpL = 0.0f, lpR = 0.0f, hpL = 0.0f, hpR = 0.0f, wow = 0.0f, wowInc = 0.0f;
    int mode = 0, quiet = 0, sleepAfter = 1;
    // The line's last `real` samples are what it would hold had it run on while it slept: the silence it wrote
    // before sleeping, and what it has taken since. Older ones are from before that silence; a read about to reach
    // them (the time turned up since) finds them zeroed first.
    int real = 1;
    bool asleep = true;
    float rate = 48000.0f;

    void prepare (float sampleRate)
    {
        rate = sampleRate;
        size = (int) (2.6f * rate) + 4;
        bl.assign ((size_t) size + 2, 0.0f); // and two guards: copies of the first two samples, after the last
        br.assign ((size_t) size + 2, 0.0f);
        wowInc = 0.6f / rate;
        clear();
    }
    void clear()
    {
        std::fill (bl.begin(), bl.end(), 0.0f);
        std::fill (br.begin(), br.end(), 0.0f);
        lpL = lpR = hpL = hpR = 0.0f;
        w = 0;
        asleep = true;
        quiet = 0;
        real = size;
    }
    void set (double seconds, float fb, float tone, int m, float lvl, float rev)
    {
        // Under the buffer with room for Tape's wow (up to 0.3 % longer).
        target = (float) std::fmin (std::fmax (seconds * rate, 1.0), (double) size / 1.004 - 8.0);
        feedback = fb, mode = m, level = lvl, toRev = rev;
        toneA = onePoleCoef (1000.0f * std::pow (16.0f, tone), rate);
        hpA = onePoleCoef (60.0f, rate);
        sleepAfter = (int) target * 2 + (int) (0.05f * rate);
    }
    float read (const std::vector<float>& b, float delay) const
    {
        float at = (float) w - delay;
        at += at < 0.0f ? (float) size : 0.0f; // a hair under 0, plus the size, can round up to it: the guards
        const int i0 = (int) at;
        const float frac = at - (float) i0;
        return b[(size_t) i0] + frac * (b[(size_t) i0 + 1] - b[(size_t) i0]);
    }
    // The samples `from` + 1 to `to` back from the write point zeroed (both lines), and the guards kept up.
    void silence (int from, int to)
    {
        int at = w - to;
        at += at < 0 ? size : 0;
        for (int left = to - from; left > 0;)
        {
            const int run = std::min (left, size - at);
            std::fill (bl.begin() + at, bl.begin() + at + run, 0.0f);
            std::fill (br.begin() + at, br.begin() + at + run, 0.0f);
            left -= run, at = 0;
        }
        bl[(size_t) size] = bl[0], bl[(size_t) size + 1] = bl[1];
        br[(size_t) size] = br[0], br[(size_t) size + 1] = br[1];
    }
    // Adds the echoes of `in` to left and right, and Delay > Reverb's share of them to `rev`.
    void process (const float* in, float* left, float* right, float* rev, int n)
    {
        int s = 0;
        if (asleep)
        {
            while (s < n && in[s] == 0.0f)
                ++s;
            if (s == n)
                return;
            asleep = false, quiet = 0;
            cur = target;
        }
        // The farthest back these samples read (the time between where it is and where it glides to, Tape's wow,
        // the interpolation's next sample); the time does not move within a call.
        const int reach = (int) (std::fmax (cur, target) * 1.004f) + 3;
        if (reach > real)
            silence (real, reach), real = reach;
        const int from = s;
        for (; s < n; ++s)
        {
            cur += (target - cur) * 0.0007f;
            float delay = cur;
            if (mode == 2)
            {
                wow += wowInc;
                wow -= wow >= 1.0f ? 1.0f : 0.0f;
                delay *= 1.0f + 0.003f * sin2pi (wow);
            }
            const float yl = read (bl, delay), yr = mode == 1 ? read (br, delay) : yl;
            lpL += (yl - lpL) * toneA;
            lpR += (yr - lpR) * toneA;
            hpL += (lpL - hpL) * hpA;
            hpR += (lpR - hpR) * hpA;
            const float fl = lpL - hpL, fr = lpR - hpR;
            const float x = in[s];
            if (mode == 1)
            {
                bl[(size_t) w] = x + feedback * fr;
                br[(size_t) w] = feedback * fl;
            }
            else if (mode == 2)
                bl[(size_t) w] = x + feedback * tanhFast (1.2f * fl) * 0.95f;
            else
                bl[(size_t) w] = x + feedback * fl;
            if (w < 2) // the guards keep up
                bl[(size_t) (size + w)] = bl[(size_t) w], br[(size_t) (size + w)] = br[(size_t) w];
            w = w + 1 == size ? 0 : w + 1;
            left[s] += yl * level;
            right[s] += yr * level;
            rev[s] += (yl + yr) * 0.5f * toRev;
            if (x == 0.0f && std::fabs (yl) + std::fabs (yr) < 1e-7f)
            {
                if (++quiet > sleepAfter)
                {
                    asleep = true;
                    real = std::min (quiet, size); // the silence it wrote; before that, what it played before going quiet
                    return;
                }
            }
            else
                quiet = 0;
        }
        real = std::min (real + (n - from), size);
    }
};
} // namespace foundry
