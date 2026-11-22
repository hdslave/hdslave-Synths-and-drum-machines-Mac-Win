// Foundry's engine: the sixteen voices, what a note does (which voice, its chance, its bar cycle, flam, sub-steps,
// nudge, humanize, the voices linked to it), choke groups, the side chain, the mix, the send effects and the master
// chain (bus compressor, analog-style filter and drive, volume, clip).
//
// Time is counted in samples from the first one rendered. The knobs that move between hits are worked out every
// `tick` (16) samples counted from there, and hits land on their sample, so the output is the same whatever the
// host's block sizes.
#pragma once

#include "fx.h"
#include "params.h"
#include "voice.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace foundry
{
struct Hit
{
    int64_t due;
    float velocity, accent, transpose;
    uint32_t note; // the note it came from (its linked layers' hits share it)
};

struct Engine
{
    static constexpr int queueSize = 64;

    void* meterContext = nullptr;
    void (*meterStrike) (void*, int, float) = nullptr;
    Voice voices[voiceCount];
    float m[M::count] {};
    Shared sh;
    double rate = 48000.0;
    int64_t now = 0;

    Hit queue[voiceCount][queueSize];
    int queued[voiceCount] {};
    int64_t nextDue = INT64_MAX;
    uint32_t stoppedHits[voiceCount] {};
    uint32_t notes = 0; // notes played, numbering their hits

    // The host's transport: the song's place is anchored to a sample and counted on from it, so it does not wobble
    // with the host's rounding; a jump, a tempo change or Play anchors it again.
    bool playing = false;
    double anchorBeats = 0.0, bpm = 120.0, barBeats = 4.0, playBar = 0.0;
    int64_t anchorSample = 0;

    Reverb reverb;
    Delay delay;
    float duckEnv = 0.0f, duckTick = 0.9f;
    float compEnv = 0.0f, compAtk = 0.1f, compRel = 0.001f, compThr = 0.0f, compSlope = 0.0f, compMakeup = 0.0f, compMix = 1.0f;
    bool compOn = false, compKeyed = false;
    bool tickIdle = false; // nothing sounds this tick: decided at its start (a hit in it wakes it)
    int scVoice = -1; // the side-chain source, -1 none
    Svf afx[2];
    int afxMode = 0;
    float afxDrive = 1.0f, afxNorm = 1.0f, volume = 1.0f;
    int clip = 1;
    int8_t gmVoice[128];

    // Aligned for the vector unit: the mixing loops then need no lead-in, whatever the voices' size.
    alignas (16) float buf[tick] {};
    alignas (16) float mixL[tick] {}, mixR[tick] {}, revIn[tick] {}, dlyIn[tick] {}, key[tick] {};

    Engine()
    {
        for (int v = 0; v < voiceCount; ++v)
            voices[v].init (v);
        std::memset (gmVoice, -1, sizeof (gmVoice));
        for (int v = voiceCount - 1; v >= 0; --v)
            for (int8_t k : voiceSpecs[v].gm)
                if (k >= 0)
                    gmVoice[k] = (int8_t) v;
        for (uint32_t i = 0; i < table().count; ++i)
            set (table().rows[i].id, table().rows[i].def);
    }

    void prepare (double sampleRate, uint32_t)
    {
        rate = sampleRate;
        sh.rate = (float) rate;
        sh.invRate = (float) (1.0 / rate);
        reverb.prepare ((float) rate);
        delay.prepare ((float) rate);
        applyMaster();
        reset();
    }

    void reset()
    {
        for (auto& v : voices)
            v.active = false, v.fading = false, v.clearState();
        std::fill (queued, queued + voiceCount, 0);
        nextDue = INT64_MAX;
        reverb.clear();
        delay.clear();
        duckEnv = 0.0f;
        compEnv = 0.0f;
        afx[0].clear(), afx[1].clear();
    }

    // --- Parameters -----------------------------------------------------------------------------------------------
    void set (uint32_t id, float value)
    {
        if (id < M::count)
        {
            m[id] = value;
            applyMaster();
            return;
        }
        if (id < voiceIdBase || id >= idLimit)
            return;
        const uint32_t v = (id - voiceIdBase) / voiceIdStride, off = (id - voiceIdBase) % voiceIdStride;
        if (off < V::count)
            voices[v].p[off] = value;
    }

    // The kit's controls into the effects and what the voices share.
    void applyMaster()
    {
        const float r = (float) rate;
        sh.kitTune = m[M::kitTune];
        sh.kitDecay = std::exp2 (1.5f * m[M::kitDecay]);
        sh.chokeCoef = decayCoef (chokeMs (m[M::chokeTime]) * 0.001f, r);
        duckTick = std::exp (-(float) tick / (releaseMs (m[M::scRelease]) * 0.001f * r)); // back to 1/e of the dip in Release
        reverb.set (revSeconds (m[M::revTime]), m[M::revTone], m[M::revPreDelay], lowCutHz (m[M::revLowCut]), m[M::revMod], m[M::revLevel]);
        setDelay();
        const float amount = m[M::comp];
        compOn = amount > 0.0f;
        compThr = -30.0f * amount;
        const float ratio = 1.0f + 7.0f * amount;
        compSlope = 1.0f - 1.0f / ratio;
        scVoice = (int) m[M::scSource] - 1;
        compKeyed = m[M::compKey] >= 0.5f && scVoice >= 0;
        compMakeup = compKeyed ? 0.0f : -compThr * compSlope * 0.6f; // keyed, it only ducks
        compAtk = 1.0f - std::exp (-1.0f / (compAttackMs (m[M::compAttack]) * 0.001f * r));
        compRel = 1.0f - std::exp (-1.0f / (releaseMs (m[M::compRelease]) * 0.001f * r));
        compMix = m[M::compMix];
        const float f = m[M::afxFilter];
        afxMode = std::fabs (f) < 0.01f ? 0 : (f < 0.0f ? 1 : 2);
        if (afxMode != 0)
        {
            const float hz = f < 0.0f ? afxLowPassHz (f) : afxHighPassHz (f);
            for (auto& s : afx)
                s.setHz (hz, 2.0f - 1.9f * m[M::afxReso], r);
        }
        afxDrive = 1.0f + 4.0f * m[M::afxDrive];
        afxNorm = m[M::afxDrive] > 0.0f ? 1.0f / tanhFast (afxDrive) : 1.0f;
        volume = dbGain (m[M::volume]);
        clip = (int) m[M::clip];
    }
    void setDelay()
    {
        const double seconds = dlyTimeBeats[(int) m[M::dlyTime]] * 60.0 / bpm;
        delay.set (seconds, m[M::dlyFeedback], m[M::dlyTone], (int) m[M::dlyMode], m[M::dlyLevel], m[M::dlyToRev]);
    }

    // --- The transport ----------------------------------------------------------------------------------------------
    double beatsPerSample() const { return bpm / (60.0 * rate); }
    double beatsAt (int64_t sample) const { return anchorBeats + (double) (sample - anchorSample) * beatsPerSample(); }

    // At a block's start: playing or not, the song's place there (NaN: the host gives none), tempo, beats a bar.
    void transport (bool play, double beats, double tempo, double bar)
    {
        if (tempo > 0.0 && tempo != bpm)
        {
            if (playing)
                anchorBeats = beatsAt (now), anchorSample = now;
            bpm = tempo;
            setDelay();
        }
        barBeats = bar > 0.0 ? bar : 4.0;
        if (play)
        {
            const bool placed = beats == beats;
            const double expected = playing ? beatsAt (now) : (placed ? beats : 0.0);
            const double target = placed ? beats : expected;
            if (! playing || std::fabs (target - expected) > 0.5 * beatsPerSample())
            {
                anchorBeats = target, anchorSample = now;
                if (! playing)
                    playBar = barOf (target);
            }
        }
        playing = play;
    }

    // --- Notes --------------------------------------------------------------------------------------------------
    // The voice a key plays and how far it is transposed (the Keys voice), -1 none.
    int voiceOf (int key, float& transpose) const
    {
        transpose = 0.0f;
        const bool gm = m[M::keyMap] >= 0.5f;
        int v = gm ? gmVoice[key & 127] : (key >= 36 && key < 36 + voiceCount ? (int) padVoice[key - 36] : -1);
        if (v >= 0)
            return v;
        const int keys = (int) m[M::keysVoice];
        if (keys <= 0 || (gm && key < 84))
            return -1;
        transpose = (float) (key - (gm ? 96 : 60));
        return keys - 1;
    }

    void note (int key, float velocity)
    {
        float transpose = 0.0f;
        const int v = voiceOf (key, transpose);
        if (v < 0)
            return;
        playVoice (v, velocity, transpose);
    }

    // The note's voice and the voices linked to it. Humanize's numbers (how late, how much softer) are the note's,
    // drawn as its voice's, so its layers land together; each layer keeps its own Prob and Cycle.
    // audition: the page's Push, which plays the voice and its linked layers whatever their Prob and Cycle.
    void playVoice (int v, float velocity, float transpose, bool audition = false)
    {
        const uint32_t note = ++notes;
        const double beats = beatsAt (now);
        const float late = chance (v, beats, 1), softer = chance (v, beats, 2);
        hitVoice (v, velocity, transpose, note, late, softer, audition);
        for (int u = 0; u < voiceCount; ++u)
            if (u != v && (int) voices[u].p[V::link] == v + 1)
                hitVoice (u, velocity, transpose, note, late, softer, audition);
    }

    // A choke from the host (a pad cut): the voice and the voices linked to it fade, and their waiting hits go.
    // Key -1: every voice.
    void chokeKey (int key)
    {
        float transpose = 0.0f;
        const int v = key < 0 ? -1 : voiceOf (key, transpose);
        if (v < 0 && key >= 0)
            return;
        for (int u = 0; u < voiceCount; ++u)
            if (key < 0 || u == v || (int) voices[u].p[V::link] == v + 1)
            {
                voices[u].choke();
                queued[u] = 0;
            }
        findNextDue();
    }

    // The bar a place in the song is in, counted from its top. A note a hair before a bar line (a host that puts
    // notes on the nearest frame) is in the new bar: half a sample of slack.
    double barOf (double beats) const { return std::floor ((beats + 0.5 * beatsPerSample()) / barBeats); }

    // The voice's random numbers: from the song's place when playing (the same hits when the same part plays
    // again), from a count of its hits when stopped.
    float chance (int v, double beats, int k) const
    {
        if (playing)
            return hash01 ((uint64_t) std::llround (beats * 960.0) * 64u + (uint64_t) v * 4u + (uint64_t) k + 0x5eedull);
        return hash01 ((uint64_t) stoppedHits[v] * 64u + (uint64_t) v * 4u + (uint64_t) k + 0xc0ffeeull);
    }

    void hitVoice (int v, float velocity, float transpose, uint32_t note, float late, float softer, bool audition = false)
    {
        const Voice& voice = voices[v];
        const double beats = beatsAt (now);
        const float r = chance (v, beats, 0);
        if (! playing)
            ++stoppedHits[v];
        if (! audition && voice.p[V::prob] < 1.0f && r >= voice.p[V::prob])
            return;
        const int cycle = (int) voice.p[V::cycle];
        if (! audition && playing && cycle != 0)
        {
            const double bar = barOf (beats);
            const int nth = cycleOf[cycle][0], every = cycleOf[cycle][1];
            if (every == 0)
            {
                if (bar != playBar)
                    return;
            }
            else
            {
                int64_t at = (int64_t) bar % every;
                at += at < 0 ? every : 0;
                if (at + 1 != nth)
                    return;
            }
        }
        const float human = m[M::humanize];
        float vel = std::pow (clampf (velocity, 0.0f, 1.0f), velCurveExponent[(int) m[M::velCurve]]);
        const float accent = velocity * 127.0f >= m[M::accentVel] - 0.5f ? m[M::accent] : 0.0f;
        vel *= 1.0f - 0.25f * human * softer;
        const double ms = voice.p[V::nudge] + 8.0 * human * late;
        const double start = ms * 0.001 * rate;
        const int subs = subStepCounts[(int) voice.p[V::subStep]];
        const double spacing = stepBeats[(int) m[M::step]] * 60.0 / bpm * rate / subs;
        const double flam = voice.p[V::flam] * 0.001 * rate;
        for (int k = 0; k < subs; ++k)
        {
            const double t = start + spacing * k;
            if (k == 0 && flam > 0.0)
            {
                push (v, { now + std::llround (t), vel * 0.45f, 0.0f, transpose, note });
                push (v, { now + std::llround (t + flam), vel, accent, transpose, note });
            }
            else
                push (v, { now + std::llround (t), vel, accent, transpose, note });
        }
    }

    void push (int v, const Hit& h)
    {
        int n = queued[v];
        if (n == queueSize)
            return;
        int at = n;
        while (at > 0 && queue[v][at - 1].due > h.due)
            queue[v][at] = queue[v][at - 1], --at;
        queue[v][at] = h;
        queued[v] = n + 1;
        nextDue = std::min (nextDue, h.due);
    }
    void findNextDue()
    {
        nextDue = INT64_MAX;
        for (int v = 0; v < voiceCount; ++v)
            if (queued[v] > 0)
                nextDue = std::min (nextDue, queue[v][0].due);
    }

    // How hard a hit strikes its voice: the level trigger() gives it, the accent in.
    float strength (int v, const Hit& h) const
    {
        const float vs = voices[v].p[V::velSens];
        return (1.0f - vs + vs * h.velocity) * (1.0f + h.accent * voices[v].p[V::accent]);
    }
    // Of voice v's first n hits (those due now), the one that plays: the strongest, the later on a tie; of `note`'s
    // hits only, unless `any`. -1: none.
    int strongest (int v, int n, bool any, uint32_t note) const
    {
        int best = -1;
        float most = 0.0f;
        for (int i = 0; i < n; ++i)
        {
            if (! any && queue[v][i].note != note)
                continue;
            const float s = strength (v, queue[v][i]);
            if (best < 0 || s >= most)
                best = i, most = s;
        }
        return best;
    }

    // `firing`: the voices hit on this sample, which this hit does not choke.
    void fire (int v, const Hit& h, uint32_t firing)
    {
        Voice& voice = voices[v];
        const int group = (int) voice.p[V::choke];
        if (group != 0)
            for (int u = 0; u < voiceCount; ++u)
                if (u != v && ! ((firing >> u) & 1u) && (int) voices[u].p[V::choke] == group)
                    voices[u].choke();
        sh.lfoBeats = lfoBeats();
        voice.trigger (h.velocity, h.accent, h.transpose, sh);
        if (meterStrike) meterStrike (meterContext, v, h.velocity);
        tickIdle = false;
        if (v == scVoice)
            duckEnv = 1.0f;
    }
    // The hits due on this sample (the host gives every note of a block its first frame, so notes close together on
    // one pad land on one sample). A voice hit more than once plays its strongest hit, once: an accent and a ghost
    // together keep the accent. Voices of a choke group hit by separate notes: the lowest plays (the closed hat
    // before the open one, as on the 808 and 909) and the others' hits go. Voices hit together never choke each
    // other, so a note's linked layers all sound.
    void fireDue()
    {
        int due[voiceCount], pick[voiceCount];
        for (int v = 0; v < voiceCount; ++v)
        {
            int n = 0;
            while (n < queued[v] && queue[v][n].due <= now)
                ++n;
            due[v] = n;
            pick[v] = strongest (v, n, true, 0);
        }
        uint32_t firing = 0;
        for (int v = 0; v < voiceCount; ++v)
        {
            if (pick[v] < 0)
                continue;
            const int group = (int) voices[v].p[V::choke];
            int lowest = v;
            if (group != 0)
                for (int u = 0; u < v && lowest == v; ++u)
                    if (pick[u] >= 0 && (int) voices[u].p[V::choke] == group)
                        lowest = u;
            if (lowest != v)
                pick[v] = strongest (v, due[v], false, queue[lowest][pick[lowest]].note);
            if (pick[v] >= 0)
                firing |= 1u << v;
        }
        for (int v = 0; v < voiceCount; ++v)
        {
            if (pick[v] >= 0)
                fire (v, queue[v][pick[v]], firing);
            const int n = due[v];
            if (n > 0)
            {
                for (int i = n; i < queued[v]; ++i)
                    queue[v][i - n] = queue[v][i];
                queued[v] -= n;
            }
        }
        findNextDue();
    }

    double lfoBeats() const { return playing ? beatsAt (now) : (double) now * beatsPerSample(); }

    void controlTick()
    {
        sh.lfoBeats = lfoBeats();
        duckEnv *= duckTick;
        sh.duck = duckEnv;
        for (auto& v : voices)
            if (v.active)
                v.control (sh, false);
    }

    // --- Rendering ------------------------------------------------------------------------------------------------
    // The mix into left / right; the side-chain source voice alone into keyL / keyR (either may be null).
    void render (float* left, float* right, uint32_t frames, float* keyL = nullptr, float* keyR = nullptr)
    {
        uint32_t done = 0;
        while (done < frames)
        {
            const int phase = (int) (now % tick);
            if (phase == 0)
                controlTick();
            if (nextDue <= now)
                fireDue();
            if (phase == 0)
                tickIdle = quietNow();
            int64_t n = std::min<int64_t> (frames - done, tick - phase);
            if (nextDue > now)
                n = std::min<int64_t> (n, nextDue - now);
            chunk (left + done, right ? right + done : nullptr, (int) n, phase);
            if (keyL)
                std::memcpy (keyL + done, key, sizeof (float) * (size_t) n);
            if (keyR)
                std::memcpy (keyR + done, key, sizeof (float) * (size_t) n);
            now += n;
            done += (uint32_t) n;
        }
    }

    void chunk (float* left, float* right, int n, int phase)
    {
        static constexpr float ramp[tick] = { 1 / 16.f, 2 / 16.f, 3 / 16.f, 4 / 16.f, 5 / 16.f, 6 / 16.f, 7 / 16.f, 8 / 16.f,
                                              9 / 16.f, 10 / 16.f, 11 / 16.f, 12 / 16.f, 13 / 16.f, 14 / 16.f, 15 / 16.f, 1.f };
        for (int i = 0; i < n; ++i)
            mixL[i] = mixR[i] = revIn[i] = dlyIn[i] = key[i] = 0.0f;
        if (tickIdle)
        {
            std::memset (left, 0, sizeof (float) * (size_t) n);
            if (right)
                std::memset (right, 0, sizeof (float) * (size_t) n);
            return;
        }
        const bool tickEnd = phase + n == tick;
        for (auto& v : voices)
        {
            if (! v.active)
                continue;
            v.render (buf, n, sh, tickEnd);
            // Each gain ramped across the tick, held, or nothing at all (most voices' sends): the same sums, the
            // work only where there is some.
            auto add = [&] (float* to, float a, float b) {
                if (a == b)
                {
                    if (a != 0.0f)
                        for (int i = 0; i < n; ++i)
                            to[i] += buf[i] * a;
                    return;
                }
                for (int i = 0; i < n; ++i)
                    to[i] += buf[i] * (a + (b - a) * ramp[phase + i]);
            };
            add (mixL, v.g0[0], v.g1[0]);
            add (mixR, v.g0[1], v.g1[1]);
            add (revIn, v.g0[2], v.g1[2]);
            add (dlyIn, v.g0[3], v.g1[3]);
            if (v.index == scVoice)
                for (int i = 0; i < n; ++i)
                    key[i] = buf[i];
        }
        delay.process (dlyIn, mixL, mixR, revIn, n);
        reverb.process (revIn, mixL, mixR, n);
        for (int i = 0; i < n; ++i)
        {
            float l = mixL[i], r = mixR[i];
            if (compOn)
            {
                const float peak = compKeyed ? std::fabs (key[i]) : std::fmax (std::fabs (l), std::fabs (r));
                compEnv += (peak - compEnv) * (peak > compEnv ? compAtk : compRel);
                const float db = 6.0206f * log2Fast (compEnv + 1e-9f);
                const float over = db - compThr;
                const float reduce = over > 0.0f ? over * compSlope : 0.0f;
                const float g = exp2Fast ((compMakeup - reduce) * 0.16609640f);
                const float k = compMix * g + 1.0f - compMix;
                l *= k, r *= k;
            }
            else
                compEnv = 0.0f;
            if (afxMode != 0)
            {
                float low, band;
                afx[0].run (l, low, band);
                l = afxMode == 1 ? low : l - afx[0].k * band - low;
                afx[1].run (r, low, band);
                r = afxMode == 1 ? low : r - afx[1].k * band - low;
            }
            if (afxDrive > 1.0f)
                l = tanhFast (l * afxDrive) * afxNorm, r = tanhFast (r * afxDrive) * afxNorm;
            l *= volume, r *= volume;
            if (clip == 1)
                l = softClip (l), r = softClip (r);
            else if (clip == 2)
                l = clampf (l, -0.98f, 0.98f), r = clampf (r, -0.98f, 0.98f);
            left[i] = l;
            if (right)
                right[i] = r;
        }
    }
    // Unity up to 0.7, then a tanh knee to 1.
    static float softClip (float x)
    {
        const float a = std::fabs (x);
        if (a <= 0.7f)
            return x;
        const float y = 0.7f + 0.3f * tanhFast ((a - 0.7f) * (1.0f / 0.3f));
        return x < 0.0f ? -y : y;
    }

    // At a tick's start: nothing will sound in it unless a hit comes. Then the master chain's last traces are
    // cleared, so skipping the tick and rendering it would come out the same.
    bool quietNow()
    {
        for (const auto& v : voices)
            if (v.active)
                return false;
        if (! reverb.asleep || ! delay.asleep || compEnv > 1e-9f)
            return false;
        for (const auto& f : afx)
            if (std::fabs (f.ic1) + std::fabs (f.ic2) > 1e-9f)
                return false;
        compEnv = 0.0f;
        afx[0].clear(), afx[1].clear();
        return true;
    }

    bool idle() const
    {
        for (const auto& v : voices)
            if (v.active)
                return false;
        return nextDue == INT64_MAX && reverb.asleep && delay.asleep;
    }
};
} // namespace foundry
