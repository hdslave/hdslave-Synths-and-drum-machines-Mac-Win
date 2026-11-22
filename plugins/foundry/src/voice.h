// One of Foundry's sixteen voices: its type's circuit in three models (808, 909 and 606 style), then the chain every
// voice shares (bend, drive, filter with its envelope, attack, gate, choke).
//
// The models follow what is documented of the machines' circuits (see the README's sources), simplified to what is
// heard and what an A55 core affords:
//   808: bridged-T resonators struck by the trigger (a second hit adds to a ring still going). The kick's centre is
//        raised for its first milliseconds, then it rings at 49.5 Hz with its pitch sighing on the negative swing;
//        the toms' pitch falls with their ring (diodes); the metal is the six square oscillators into the 3.44 and
//        7.1 kHz bands, clipped by swing-type VCAs against their envelopes, high passed and differentiated.
//   909: oscillators reset by the hit through shapers and envelopes (the kick a triangle shaped to a sine, swept,
//        with a pulse and filtered noise for its attack); the hats and cymbals are read as 6-bit words at a 30 kHz
//        clock (Tune moves it), then a VCA and a low pass, as the 909's ROM voices are.
//   606: two networks for the kick (its body and a click), simpler snare and toms, its own six oscillators through
//        a band pass and a high pass in parallel.
// Voices a machine never had (a 909 cowbell, a 606 clap) are new ones in that model's character.
#pragma once

#include "dsp.h"
#include "params.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace foundry
{
constexpr int tick = 16;          // the control period, samples, counted from the engine's first sample
constexpr float silent = 1.6e-5f; // -96 dB: a voice whose sources fall below this is done

// What the engine gives a voice at a hit or a tick.
struct Shared
{
    float rate = 48000.0f, invRate = 1.0f / 48000.0f;
    float kitTune = 0.0f;  // semitones
    float kitDecay = 1.0f; // multiplies every decay
    float chokeCoef = 0.99f;
    double lfoBeats = 0.0; // where the LFOs are: the song's place playing, the engine's own clock stopped
    float duck = 0.0f;     // the side chain's envelope, 0..1
};

// The metal: six square oscillators. The 808's are its six (its cowbell's two among them) and the 606's its six;
// the 909 style's are a new, denser set (the 909 itself plays samples).
inline constexpr float metalHz[modelCount][6] = {
    { 205.3f, 304.4f, 369.6f, 522.7f, 540.0f, 800.0f },
    { 263.0f, 400.0f, 421.0f, 474.0f, 587.0f, 845.0f },
    { 245.0f, 306.0f, 365.0f, 415.0f, 437.0f, 619.0f },
};
// The six's mixing weights: unequal, as the 808's dividers are, so their sum is never exactly nothing.
inline constexpr float metalWeight[6] = { 1.0f, 0.9f, 0.95f, 0.85f, 1.05f, 0.8f };
inline constexpr float metalNoise[] = { 0.0f, 0.5f, 0.15f }; // noise each model adds to its metal (the 909's samples are noisy)
// The swing-type VCAs' clipping: how hard the band-passed metal drives them.
inline constexpr float swingDrive[] = { 5.0f, 1.0f, 3.5f };
// Output levels by model, so the kit sits balanced at its default levels (set from the level table the tests print).
inline constexpr float hatGain[] = { 1.5f, 0.9f, 9.0f }, cymGain[] = { 1.45f, 1.0f, 7.5f }, clapGain[] = { 2.4f, 2.0f, 2.0f };

// The 909 style's VCO: a triangle (from its phase, rising from 0) through a shaper toward a sine; `drive` past ~1.7
// pushes it toward a square.
inline float shapedTriangle (float phase, float drive, float norm)
{
    float t = phase + 0.25f;
    t -= t >= 1.0f ? 1.0f : 0.0f;
    return tanhFast (drive * (1.0f - 4.0f * std::fabs (t - 0.5f))) * norm;
}

// An op-amp's rails: linear to 1, then a soft limit.
inline float rails (float y)
{
    const float a = std::fabs (y);
    if (a <= 1.0f)
        return y;
    const float c = 1.0f + 0.4f * tanhFast ((a - 1.0f) * 2.5f);
    return y < 0.0f ? -c : c;
}

struct Voice
{
    int index = 0;
    const VoiceSpec* spec = &voiceSpecs[0];
    Type type = Type::kick;
    float p[V::count] {};
    bool active = false;

    // The hit in hand.
    uint8_t model = 0;
    float amp = 0.0f;      // the hit's strength: velocity, accent
    float hz = 0.0f;       // its main pitch, the LFO aside
    float lfoPitch = 1.0f; // the LFO's pitch multiplier
    float lfo = 0.0f;      // the LFO, times its depth, at the last tick

    // The type's state.
    Resonator res[3];
    float ph[6] {}, dt[6] {}, gain[6] {};
    float e[4] {}, ec[4] {};
    float penv = 0.0f, pcoef = 0.0f, bendMul = 0.0f, ratio = 1.0f;
    float x[16] {}; // the type's constants for the hit
    Svf f[6];
    float lp[2] {}, lpa[2] {};
    Noise noise, clickNoise; // clickNoise: the toms' Attack, apart so the rest's noise is the same without it
    float prevOsc = 0.0f;
    int32_t count = 0, countNext = 0, burstsLeft = 0, burstGap = 1, rampLen = 1;
    float holdValue = 0.0f, quant = 1.0f;
    int32_t holdLeft = 0, holdLen = 1;
    int32_t hold = 0;                 // samples left in a kick's attack window or its click pulse
    float lastFc = 0.0f, prevY = 0.0f; // the resonator's last tuning; the metal's last sample (its differentiator)
    float clock = 0.0f, clockInc = 0.0f, held = 0.0f; // the 909 style's ROM clock and its word on the DAC

    // The shared chain.
    Svf flt;
    float fenv = 0.0f, fenvTick = 0.0f;
    float bendHeld = 0.0f, bendCount = 0.0f, bendHold = 1.0f, bendQuant = 4096.0f, bendDepth = 0.0f, bendPos = 0.0f;
    bool bendOn = false;
    DcBlock dc;
    float driveGain = 1.0f, driveNorm = 1.0f;
    uint8_t filterMode = 0;
    float attackPos = 1.0f, attackInc = 0.0f;
    int64_t gateLeft = -1;
    float fade = 1.0f;
    bool fading = false;
    // The mix: gains at the tick's start and end (ramped across it): left, right, reverb, delay, and the side-chain
    // key (the voice before its Level and the ducking, so a muted voice still keys).
    float g0[5] {}, g1[5] {};
    float levelDb = 1e9f, levelGain = 0.0f;      // the Level the gain was last worked out for
    float cutoffKnob = -1.0f, cutoffBase = 0.0f; // the Cutoff the filter's base was last worked out for
    float muteKnob = 0.0f, muteRate = 0.0f, muteCoef = 1.0f; // the Mute Decay its fade was last worked out for
    float bendMix = 1.0f;
    bool inverse = false, ownMute = false;
    // Corners fixed in hertz, set from their 48 kHz coefficients for the rate in use (at 48 kHz those exactly).
    float couple = 0.0015f, coefRate = 48000.0f; // the 909 kick's output coupling (about 11.5 Hz)

    float slot (int k) const { return p[slotId (k)]; }

    void init (int i)
    {
        index = i;
        spec = &voiceSpecs[i];
        type = spec->type;
        noise.s = 0x9e3779b9u + 0x85ebca6bu * (uint32_t) (i + 1);
        clickNoise.s = 0x2545f491u + 0x9e3779b9u * (uint32_t) (i + 1);
        for (int k = 0; k < 6; ++k) // the oscillators run free: they never start together
            ph[k] = hash01 ((uint64_t) (i * 8 + k) + 77u);
    }

    void clearState()
    {
        for (auto& r : res) r.clear();
        for (auto& v : e) v = 0.0f;
        for (auto& v : f) v.clear();
        lp[0] = lp[1] = 0.0f;
        penv = 0.0f;
        flt.clear();
        dc.clear();
        bendHeld = bendCount = 0.0f;
        holdValue = 0.0f, holdLeft = 0;
        prevOsc = 0.0f;
        burstsLeft = 0;
        count = 0;
        hold = 0;
        lastFc = prevY = clock = held = 0.0f;
    }

    float decaySeconds (float lfoMul) const
    {
        const float* range = spec->decay[model];
        return range[0] * std::pow (range[1] / range[0], p[V::decay]) * lfoMul;
    }

    // --- The LFO and the knobs that move between hits: at every tick while sounding, and at each hit --------------
    float lfoValue (const Shared& sh) const
    {
        const float depth = p[V::lfoDepth];
        if (depth == 0.0f || p[V::lfoTarget] == 0.0f)
            return 0.0f;
        const double rateBeats = lfoRateBeats[(int) p[V::lfoRate]];
        const double phase = sh.lfoBeats / rateBeats;
        const double cycle = std::floor (phase);
        const float t = (float) (phase - cycle);
        float w = 0.0f;
        switch ((int) p[V::lfoWave])
        {
            case 0: w = sin2pi (t); break;
            case 1: w = 1.0f - 4.0f * std::fabs (t - 0.5f); break;
            case 2: w = 1.0f - 2.0f * t; break;
            case 3: w = t < 0.5f ? 1.0f : -1.0f; break;
            default: w = 2.0f * hash01 ((uint64_t) (int64_t) cycle * 131u + (uint64_t) index * 7919u + 1u) - 1.0f; break;
        }
        return w * depth;
    }
    int lfoTarget() const { return (int) p[V::lfoTarget]; }

    // The tick: the LFO, the filter, the bend and drive amounts, the mix gains.
    void control (const Shared& sh, bool fresh)
    {
        lfo = lfoValue (sh);
        const int target = lfoTarget();
        const float pitchMul = target == lfoTune ? exp2Fast (lfo) : 1.0f;
        if (pitchMul != lfoPitch)
        {
            lfoPitch = pitchMul;
            retune();
        }
        // Bend: its type (a place between four characters) and depth.
        bendDepth = clampf (p[V::bend] + (target == lfoBend ? 0.5f * lfo : 0.0f), 0.0f, 1.0f);
        bendOn = bendDepth > 0.0f;
        if (bendOn)
        {
            bendPos = clampf (p[V::bendType], 0.0f, 1.0f) * 3.0f;
            bendQuant = exp2Fast (12.0f - 10.0f * bendDepth);
            bendHold = 1.0f + 31.0f * bendDepth;
        }
        const float drive = clampf (p[V::drive] + (target == lfoDrive ? 0.5f * lfo : 0.0f), 0.0f, 1.0f);
        driveGain = 1.0f + 7.0f * drive;
        driveNorm = drive > 0.0f ? 1.0f / tanhFast (driveGain) : 1.0f;
        filterMode = (uint8_t) p[V::filter];
        bendMix = p[V::bendMix];
        inverse = p[V::phase] > 0.5f;
        ownMute = p[V::muteDecay] > 0.0f;
        if (ownMute && (p[V::muteDecay] != muteKnob || sh.rate != muteRate))
            muteKnob = p[V::muteDecay], muteRate = sh.rate, muteCoef = decayCoef (chokeMs (muteKnob) * 0.001f, sh.rate);
        if (filterMode != 0)
        {
            const float octaves = 6.0f * p[V::fltEnv] * fenv + (target == lfoCutoff ? 3.0f * lfo : 0.0f);
            const float k = 2.0f - 1.95f * p[V::reso];
            if (p[V::cutoff] != cutoffKnob)
                cutoffKnob = p[V::cutoff], cutoffBase = cutoffHz (cutoffKnob);
            flt.setHz (cutoffBase * exp2Fast (clampf (octaves, -12.0f, 12.0f)), k, sh.rate);
            fenv *= fenvTick;
        }
        // The mix.
        if (p[V::level] != levelDb)
            levelDb = p[V::level], levelGain = dbGain (levelDb);
        float level = levelGain;
        if (target == lfoLevel)
            level *= std::fmax (0.0f, 1.0f + 0.9f * lfo);
        const float pan = clampf (p[V::pan] + (target == lfoPan ? lfo : 0.0f), -1.0f, 1.0f);
        const float duck = 1.0f - p[V::duck] * sh.duck;
        const float g[5] = { level * std::fmin (1.0f, 1.0f - pan) * duck, level * std::fmin (1.0f, 1.0f + pan) * duck,
                             level * duck * p[V::reverb], level * duck * p[V::delay], 1.0f };
        for (int i = 0; i < 5; ++i)
        {
            g0[i] = fresh ? g[i] : g1[i];
            g1[i] = g[i];
        }
    }

    // --- A hit ------------------------------------------------------------------------------------------------------
    // `velocity` 0..1 after the curve; `accent` how far the accent lifts it (0 none); `transpose` semitones.
    void trigger (float velocity, float accent, float transpose, const Shared& sh)
    {
        const bool wasActive = active;
        if (fading)
            clearState(); // a choked voice's tail is gone, not back at full
        fading = false;
        fade = 1.0f;
        model = (uint8_t) p[V::model];
        invRate = sh.invRate, rate = sh.rate;
        if (rate != coefRate)
        {
            coefRate = rate;
            couple = (float) (0.0015 * 48000.0 / (double) rate);
            dc.a = (float) std::pow (0.9985, 48000.0 / (double) rate);
        }
        const float vs = p[V::velSens];
        amp = (1.0f - vs + vs * velocity) * (1.0f + accent * p[V::accent]);
        lfo = lfoValue (sh);
        lfoPitch = lfoTarget() == lfoTune ? exp2Fast (lfo) : 1.0f;
        const float decayMul = lfoTarget() == lfoDecay ? exp2Fast (1.5f * lfo) : 1.0f;
        const float semis = p[V::tune] + sh.kitTune + transpose;
        hz = spec->hz[model] * exp2Fast (semis * (1.0f / 12.0f));
        const float decay = decaySeconds (decayMul) * sh.kitDecay;
        switch (type)
        {
            case Type::kick: hitKick (decay, sh); break;
            case Type::rim: hitRim (decay, sh); break;
            case Type::snare: hitSnare (decay, sh); break;
            case Type::clap: hitClap (decay, sh); break;
            case Type::tom: hitTom (decay, sh); break;
            case Type::hat: hitMetal (decay, sh, false); break;
            case Type::cymbal: hitMetal (decay, sh, true); break;
            case Type::cowbell: hitCowbell (decay, sh); break;
            case Type::clave: hitClave (decay, sh); break;
            case Type::maraca: hitMaraca (decay, sh); break;
        }
        // The shared chain.
        fenv = 1.0f;
        fenvTick = decayCoef (envMs (p[V::fltDecay]) * 0.001f, sh.rate / (float) tick);
        const float attack = attackMs (p[V::attack]);
        if (attack > 0.05f && ! wasActive)
            attackPos = 0.0f, attackInc = 1000.0f / (attack * sh.rate);
        else if (! wasActive)
            attackPos = 1.0f, attackInc = 0.0f;
        gateLeft = p[V::gate] > 0.0f ? (int64_t) (gateMs (p[V::gate]) * 0.001f * sh.rate) : -1;
        active = true;
        control (sh, ! wasActive);
    }

    void choke()
    {
        if (active)
            fading = true;
    }

    // --- Rendering ------------------------------------------------------------------------------------------------
    // n samples of this voice into `out`, then the shared chain. Turns itself off when done, looked at only where a
    // tick ends (`tickEnd`), so the sample it stops on does not depend on how the host cut its blocks.
    void render (float* out, int n, const Shared& sh, bool tickEnd)
    {
        bool alive = true;
        switch (type)
        {
            case Type::kick: alive = renderKick (out, n); break;
            case Type::rim: alive = renderRim (out, n); break;
            case Type::snare: alive = renderSnare (out, n); break;
            case Type::clap: alive = renderClap (out, n); break;
            case Type::tom: alive = renderTom (out, n); break;
            case Type::hat: alive = renderMetal (out, n, false); break;
            case Type::cymbal: alive = renderMetal (out, n, true); break;
            case Type::cowbell: alive = renderCowbell (out, n); break;
            case Type::clave: alive = renderClave (out, n); break;
            case Type::maraca: alive = renderMaraca (out, n); break;
        }
        chain (out, n, sh);
        if (tickEnd && (! alive || (fading && fade < 1e-4f)))
        {
            active = false;
            fading = false;
            clearState();
        }
    }

    // The LFO moved the pitch: oscillators, resonators and filters that are not retuned every sample take the new one.
    // (The kick, the 808 and 606 toms and the 909 VCOs read it each sample.)
    void retune()
    {
        const float c = hz * lfoPitch * invRate;
        switch (type)
        {
            case Type::snare:
                if (model != m909)
                {
                    res[0].tune (c * (1.0f + bendMul * penv));
                    res[1].tune (c * (1.0f + bendMul * penv) * ratio);
                }
                break;
            case Type::rim:
                res[0].tune (c * x[3]);
                res[1].tune (c * ratio);
                res[2].tune (c * 4.5f * x[4]);
                break;
            case Type::clap:
                f[0].setHz (hz * lfoPitch * clapShift, f[0].k, rate);
                f[1].setHz (hz * lfoPitch * clapShift * 0.55f, f[1].k, rate);
                break;
            case Type::hat:
            case Type::cymbal:
                for (int k = 0; k < 6; ++k)
                    dt[k] = std::fmin (metalBase[k] * c, 0.3f); // a step under a cycle, even swung by XMod
                clockInc = 30000.0f * hz * lfoPitch / spec->hz[model] * invRate;
                break;
            case Type::cowbell: dt[0] = c, dt[1] = c * ratio, dt[2] = c * x[6], dt[3] = c * x[5]; break;
            case Type::clave:
                res[0].tune (c);
                res[1].tune (c * 1.47f);
                break;
            case Type::maraca:
                f[0].setHz (hz * lfoPitch * exp2Fast (slot (1)), 1.2f, rate);
                f[1].setHz (hz * lfoPitch * exp2Fast (slot (1)) * 1.3f, 0.4f, rate);
                break;
            default: break;
        }
    }

    float invRate = 1.0f / 48000.0f, rate = 48000.0f, clapShift = 1.0f;
    float metalBase[6] {}; // the metal oscillators' frequencies over the voice's pitch

    // --- Kick -----------------------------------------------------------------------------------------------------
    // 808: the bridged-T struck by the trigger, answering in its band pass's cosine phase (at its peak the moment it
    // is struck: the snap), its centre gliding down from raised (Attack: 3.5 times at the default) to 53 Hz over a
    // few milliseconds (Body Decay: 4 ms), the first cycles louder (the attack's boost), as the attack envelope's
    // transistor and the retrigger pulse make it. Its pitch sighs: on the negative swing the leakage raises the centre
    // by the swing's size (Body Depth), and that half swings wider. Fitted to recordings of the machine: the lobes'
    // lengths and levels match within a few per cent. Tone is the pot's low pass; the op-amp's rails round a hot hit.
    // 909: a triangle VCO reset by the hit, shaped toward a sine, its negative half fuller than its positive (as the
    // machine's), swept down from about six times its pitch (Body Depth) over Body Decay (Tune, on the machine: 5 to
    // 20 ms), through a VCA whose envelope holds before it falls, plus the attack: a short pulse and filtered noise.
    // 606: the same struck network at 62 Hz with a lighter glide, and a second, higher one for the click (Attack its
    // level, Body Decay its length).
    void hitKick (float decay, const Shared& sh)
    {
        const float attack = slot (0), tone = slot (1), depth = slot (2), time = slot (3);
        x[0] = slot (4);
        lpa[0] = onePoleCoef (300.0f * exp2Fast (5.0f * tone), sh.rate);
        lp[1] = 0.0f;
        if (model == m909)
        {
            ph[0] = 0.0f;
            const float tau = decay / 9.23f; // the held envelope (1 + t/tau) e^(-t/tau) falls 60 dB at 9.23 tau
            e[0] = amp, e[3] = 0.0f, ec[0] = std::exp (-sh.invRate / tau), x[5] = sh.invRate / tau;
            bendMul = 11.0f * depth;
            penv = 1.0f, pcoef = std::exp (-sh.invRate / (0.004f + 0.03f * time));
            e[1] = amp * attack * 1.2f, ec[1] = decayCoef (0.008f, sh.rate); // the pulse: merges with the first half-cycle
            e[2] = amp * attack * 0.6f, ec[2] = decayCoef (0.010f, sh.rate);
            x[1] = 1.7f + 6.0f * x[0];
            x[2] = 1.0f / tanhFast (x[1]);
            return;
        }
        res[0].r = decayCoef (decay, sh.rate);
        res[0].strike (0.7f * amp); // headroom: the attack's first cycles stand above the body, as recorded
        penv = 1.0f, pcoef = std::exp (-sh.invRate / (0.001f + 0.0086f * time));
        if (model == m808)
        {
            bendMul = 7.14f * attack;
            x[3] = 1.5f * depth;
            e[3] = 1.43f * attack, ec[3] = std::exp (-sh.invRate / 0.003f);
            x[4] = 0.2f;
        }
        else
        {
            bendMul = 4.0f * attack;
            x[3] = 0.5f * depth;
            e[3] = 0.0f, ec[3] = 0.0f;
            x[4] = 0.05f;
            res[1].tune (hz * lfoPitch * 2.15f * invRate, decayCoef (0.015f + 0.06f * time, sh.rate));
            res[1].strike (amp * attack * 1.2f);
        }
        lastFc = hz * lfoPitch * (1.0f + bendMul) * invRate;
        res[0].tune (lastFc);
    }
    [[gnu::always_inline]] bool renderKick (float* out, int n)
    {
        const float base = hz * lfoPitch * invRate, excite = x[0];
        if (model == m909)
        {
            for (int i = 0; i < n; ++i)
            {
                penv *= pcoef;
                ph[0] += base * (1.0f + bendMul * penv);
                ph[0] -= std::floor (ph[0]);
                const float held = e[0] + e[3]; // (1 + t/tau) e^(-t/tau)
                e[3] = ec[0] * (e[3] + x[5] * e[0]);
                e[0] *= ec[0];
                float w = shapedTriangle (ph[0], x[1], x[2]);
                w *= w < 0.0f ? 1.0f : 0.7f; // the fuller negative half
                float y = held * w;
                lp[1] += (y - lp[1]) * couple; // the output's coupling: the asymmetry's DC out
                y -= lp[1];
                const float click = e[2] * noise.next() + e[1];
                e[1] *= ec[1], e[2] *= ec[2];
                lp[0] += (click - lp[0]) * lpa[0];
                out[i] = y + lp[0];
            }
            return e[0] + e[3] > silent || e[2] > silent || std::fabs (lp[0]) > silent;
        }
        const bool is808 = model == m808;
        const float sigh = x[3], asym = x[4];
        for (int i = 0; i < n; ++i)
        {
            const float y = res[0].re;
            penv *= pcoef; // no branch on it: a loop the compiler could split there would round differently
            float fc = base * (1.0f + bendMul * penv);
            if (y < -0.05f)
                fc *= 1.0f + sigh * (-y - 0.05f);
            if (fc != lastFc)
                res[0].tuneLow (fc), lastFc = fc;
            res[0].ring();
            float v = y * (1.0f + e[3]);
            e[3] *= ec[3];
            if (v < 0.0f)
                v *= 1.0f + asym * std::fmin (1.0f, -v);
            if (! is808)
                v += res[1].ring();
            lp[0] += (v - lp[0]) * lpa[0];
            float w = rails (lp[0]);
            if (excite > 0.0f)
                w += excite * (tanhFast (3.0f * w) - w);
            out[i] = w;
        }
        return res[0].size() > silent || (! is808 && res[1].size() > silent) || std::fabs (lp[0]) > silent;
    }

    // --- Rim shot -------------------------------------------------------------------------------------------------
    // 808: two high-Q networks at 455 and 1667 Hz struck together, through a swing-type VCA (its short envelope is
    // Decay; it saturates and adds the upper harmonics), plus a little of the clap's band-passed noise, high passed.
    // Body is the lower network's share, Tone the upper's and the high pass. 909 style: three networks near 220, 500
    // and 1000 Hz, short, with a noise tick. 606 style (new): two higher networks.
    void hitRim (float decay, const Shared& sh)
    {
        const float tone = slot (0), body = slot (1);
        static constexpr float ratios[] = { 3.664f, 2.27f, 2.0f };
        // O1-O3 Tune, Decay: each network's (oscillator's) pitch, an octave either way, and its ring, a quarter to
        // four times; at 50 % as they are. The third is the 909 style's.
        auto tuneOf = [this] (int k) { return exp2Fast (2.0f * (slot (5 + k) - 0.5f)); };
        auto ringOf = [this] (int k) { return exp2Fast (4.0f * (slot (8 + k) - 0.5f)); };
        x[3] = tuneOf (0), x[4] = tuneOf (2);
        ratio = ratios[model] * tuneOf (1);
        const float c = hz * lfoPitch * invRate;
        const bool vca = model == m808;
        const float ring = vca ? 0.15f : decay;
        res[0].tune (c * x[3], decayCoef (ring * (1.0f + 1.5f * body) * ringOf (0), sh.rate));
        res[1].tune (c * ratio, decayCoef (ring * (vca ? 0.35f : 1.0f) * ringOf (1), sh.rate));
        res[0].strike (amp * (0.4f + body));
        res[1].strike (amp);
        if (model == m909)
        {
            res[2].tune (c * 4.5f * x[4], decayCoef (decay * 0.6f * ringOf (2), sh.rate));
            res[2].strike (amp * 0.6f);
        }
        else
            res[2].clear();
        x[0] = 0.6f + 0.6f * body;
        x[1] = 1.2f * exp2Fast (0.8f * tone);
        x[2] = slot (3);
        e[0] = 1.0f, ec[0] = vca ? decayCoef (decay, sh.rate) : 1.0f;
        e[1] = amp * slot (2) * (model == m808 ? 1.0f : 0.6f), ec[1] = decayCoef (model == m808 ? 0.012f : 0.008f, sh.rate);
        e[2] = amp * slot (4) * 0.6f, ec[2] = decayCoef (0.0012f, sh.rate);
        f[0].setHz (model == m808 ? 3000.0f : 1000.0f, model == m808 ? 1.4f : 0.7f, sh.rate);
        f[1].setHz (250.0f * exp2Fast (1.5f * tone), 1.2f, sh.rate);
    }
    [[gnu::always_inline]] bool renderRim (float* out, int n)
    {
        const float base2 = hz * lfoPitch * ratio * invRate;
        const bool three = model == m909, vca = model == m808;
        const float level = vca ? 1.0f : 0.55f;
        for (int i = 0; i < n; ++i)
        {
            const float y0 = res[0].ring();
            if (x[2] > 0.0f)
                res[1].tune (base2 * (1.0f + x[2] * y0)); // O1 swings O2
            float y = x[0] * y0 + x[1] * res[1].ring();
            if (three)
                y += 0.5f * res[2].ring();
            if (vca)
            {
                y = 0.6f * e[0] * tanhFast (3.0f * y);
                e[0] *= ec[0];
            }
            const float nz = (vca ? f[0].hp (noise.next()) : f[0].bp (noise.next())) * e[1]; // the 808's: its strike's highs
            e[1] *= ec[1];
            const float click = e[2];
            e[2] *= ec[2];
            out[i] = f[1].hp (y + nz + click) * level;
        }
        return (e[0] > silent && (res[0].size() > silent || res[1].size() > silent || (three && res[2].size() > silent))) ||
               e[1] > silent || e[2] > silent;
    }

    // --- Snare ----------------------------------------------------------------------------------------------------
    // 808: two networks, the fundamental at 175 Hz and the second near 330 Hz (Detune moves it), Tone their balance
    // (the service notes' fundamental/harmonic ratio, with Osc Mix), a slight sweep; Snappy is white noise through its
    // own envelope (Snappy Decay) and a high pass. 909 style: two VCOs at about 200 and 330 Hz shaped toward sines,
    // each through its own envelope (the upper shorter), swept by Pitch Env; noise through a low pass (Tone) into a
    // high-passed band (Snappy, Snappy Decay) and a shorter unfiltered share. 606 style: one main network, a weak
    // second, louder snappy noise, the whole high passed.
    void hitSnare (float decay, const Shared& sh)
    {
        const float tone = slot (0), snappy = slot (1);
        const float mix = clampf (slot (4) + (model == m808 ? tone : 0.0f), -1.0f, 1.0f);
        static constexpr float ratios[] = { 1.89f, 1.38f, 1.5f }, pitchOct[] = { 0.3f, 1.0f, 0.6f };
        ratio = ratios[model] * exp2Fast (0.5f * slot (3));
        bendMul = exp2Fast (pitchOct[model] * slot (5)) - 1.0f;
        pcoef = decayCoef (0.004f * std::pow (40.0f, slot (6)), sh.rate);
        penv = 1.0f;
        x[0] = (model == m909 ? 0.75f : 0.55f) * std::fmin (1.0f, 1.0f - mix);
        x[1] = model == m909 ? 0.55f * std::fmin (1.0f, 1.0f + mix) : (model == m808 ? 0.05f : 0.04f) * exp2Fast (3.0f * mix);
        x[2] = slot (7);
        if (model == m909)
        {
            ph[0] = ph[1] = 0.0f;
            e[0] = amp, ec[0] = decayCoef (decay, sh.rate);
            e[3] = amp, ec[3] = decayCoef (decay * 0.6f, sh.rate);
            x[3] = 0.8f, x[4] = 1.0f / tanhFast (0.8f);
        }
        else
        {
            const float c = hz * lfoPitch * (1.0f + bendMul) * invRate;
            res[0].tune (c, decayCoef (decay, sh.rate));
            res[1].tune (c * ratio, decayCoef (decay * 0.55f, sh.rate));
            res[0].strike (amp);
            res[1].strike (amp);
        }
        static constexpr float snapScale[] = { 1.0f, 3.0f, 0.9f }, snapLevel[] = { 2.0f, 0.8f, 2.2f };
        const float snapTime = 0.05f * std::pow (16.0f, slot (2)) * snapScale[model];
        e[1] = amp * snappy * snapLevel[model], ec[1] = decayCoef (snapTime, sh.rate);
        e[2] = amp * snappy * 0.6f, ec[2] = decayCoef (0.06f, sh.rate);
        lp[1] = model == m909 ? 0.0f : 1.0f, lpa[1] = 1.0f - std::exp (-sh.invRate / 0.015f); // the 909's swell
        if (model == m808)
        {
            f[0].setHz (1200.0f, 1.4f, sh.rate);
            f[1].setHz (9000.0f, 1.4f, sh.rate);
        }
        else if (model == m909)
        {
            f[0].setHz (4000.0f * std::pow (4.0f, 0.5f * (tone + 1.0f)), 1.2f, sh.rate);
            f[1].setHz (2500.0f, 1.0f, sh.rate);
        }
        else
        {
            f[0].setHz (2500.0f * exp2Fast (tone), 1.0f, sh.rate);
            f[1].setHz (11000.0f, 1.4f, sh.rate);
            f[2].setHz (120.0f, 1.4f, sh.rate);
        }
    }
    [[gnu::always_inline]] bool renderSnare (float* out, int n)
    {
        const float base = hz * lfoPitch * invRate;
        const int m = model;
        for (int i = 0; i < n; ++i)
        {
            float fc = base;
            const bool sweeping = penv > 1e-5f;
            if (sweeping)
            {
                penv *= pcoef;
                fc = base * (1.0f + bendMul * penv);
            }
            float y1, y2;
            if (m != m909)
            {
                if (sweeping)
                    res[0].tune (fc);
                y1 = res[0].ring();
                if (sweeping || x[2] > 0.0f)
                    res[1].tune (fc * ratio * (1.0f + 1.5f * x[2] * y1));
                y2 = res[1].ring();
            }
            else
            {
                ph[0] += fc;
                ph[0] -= std::floor (ph[0]);
                y1 = e[0] * shapedTriangle (ph[0], x[3], x[4]);
                e[0] *= ec[0];
                ph[1] += fc * ratio * (1.0f + 1.5f * x[2] * y1);
                ph[1] -= std::floor (ph[1]);
                y2 = e[3] * shapedTriangle (ph[1], x[3], x[4]);
                e[3] *= ec[3];
            }
            const float w = noise.next();
            float nz;
            if (m == m909)
            {
                lp[1] += (1.0f - lp[1]) * lpa[1];
                const float low = f[0].lp (w);
                nz = (f[1].hp (low) * e[1] + low * e[2]) * lp[1];
                e[2] *= ec[2];
            }
            else
                nz = f[1].lp (f[0].hp (w)) * e[1];
            e[1] *= ec[1];
            float y = x[0] * y1 + x[1] * y2 + nz;
            if (m == m606)
                y = f[2].hp (y);
            out[i] = y;
        }
        const float body = m == m909 ? e[0] + e[3] : res[0].size() + res[1].size();
        return body > silent || e[1] > silent;
    }

    // --- Hand clap ------------------------------------------------------------------------------------------------
    // White noise through a band pass (about 1 kHz in the 808; Nuance its Q) into two VCAs, as the 808's: the
    // sawtooth envelope's bursts (Claps of them, Speed apart, each falling within its gap; the last falls over Clap
    // Decay) and the reverb's long envelope (Decay), its share by Mix and its own low pass (Tail Tone). Filter tilts
    // the whole, Size thickens (a second, lower band) or thins (the band higher).
    void hitClap (float decay, const Shared& sh)
    {
        const int claps = 1 + (int) std::lrint (slot (0) * 7.0f);
        static constexpr float gapScale[] = { 1.0f, 0.85f, 0.7f };
        const float speed = 1.0f - slot (1);
        const float gapMs = (4.0f + 20.0f * speed * speed) * gapScale[model];
        burstGap = std::max (1, (int32_t) (gapMs * 0.001f * sh.rate));
        burstsLeft = claps - 1;
        count = 0;
        countNext = burstGap;
        rampLen = std::max (1, (int32_t) (0.002f * sh.rate));
        x[5] = decayCoef (gapMs * 0.0011f, sh.rate);                     // a burst falling within its gap
        x[6] = decayCoef (0.01f + 0.1f * slot (2), sh.rate);              // the last one
        e[0] = amp, ec[0] = burstsLeft > 0 ? x[5] : x[6];
        e[1] = 0.0f, ec[1] = decayCoef (decay, sh.rate);
        x[0] = std::fmin (1.0f, 1.0f - slot (3)) * 1.6f;
        x[1] = std::fmin (1.0f, 1.0f + slot (3)) * 1.2f;
        const float q = 1.2f + 3.5f * slot (5), size = slot (6);
        clapShift = size < 0.0f ? exp2Fast (-0.7f * size) : 1.0f;
        const float centre = hz * lfoPitch * clapShift;
        f[0].setHz (centre, 1.0f / q, sh.rate);
        f[1].setHz (centre * 0.55f, 1.0f / (0.7f * q), sh.rate);
        x[2] = std::fmax (0.0f, size) * 1.2f;
        lpa[0] = onePoleCoef (800.0f * std::pow (16.0f, slot (7)), sh.rate);
        lpa[1] = onePoleCoef (1500.0f, sh.rate);
        f[2].setHz (450.0f, 1.4f, sh.rate);
        x[3] = slot (4);
        x[4] = amp;
    }
    [[gnu::always_inline]] bool renderClap (float* out, int n)
    {
        const float rampStep = x[4] / (float) rampLen, gain = clapGain[model];
        for (int i = 0; i < n; ++i)
        {
            if (burstsLeft > 0 && count >= countNext) // burst k starts k gaps after the hit
            {
                e[0] = x[4];
                --burstsLeft;
                ec[0] = burstsLeft > 0 ? x[5] : x[6];
                countNext += burstGap;
            }
            if (count <= rampLen)
                e[1] = rampStep * (float) count;
            else
                e[1] *= ec[1];
            const float nz = noise.next();
            float b = f[0].bp (nz);
            if (x[2] > 0.0f)
                b += x[2] * f[1].bp (nz);
            lp[0] += (b - lp[0]) * lpa[0];
            float y = x[0] * b * e[0] + x[1] * lp[0] * e[1];
            y = f[2].hp (y);
            e[0] *= ec[0];
            lp[1] += (y - lp[1]) * lpa[1];
            y = lp[1] * (1.0f - x[3]) + (y - lp[1]) * (1.0f + x[3]);
            out[i] = y * gain;
            ++count;
        }
        return burstsLeft > 0 || e[0] > silent || count <= rampLen || e[1] > silent;
    }

    // --- Tom and conga ----------------------------------------------------------------------------------------------
    // 808 and 606: a network struck into its cosine phase, its pitch riding on its own ring as the 808's diodes make
    // it: raised while the ring is big (Pitch Env how far; the 606's falls further), settling as it dies, after a
    // brief glide on the strike (Pitch Decay its length); a second, optional
    // tone (Osc Mix, Detune: 1.5 times the first). 909 style: VCOs reset by the hit and swept down by Pitch Env over
    // Pitch Decay. Shape pushes either toward a square; Color is the low tom's pink noise (its own tone and decay).
    void hitTom (float decay, const Shared& sh)
    {
        const float pe = slot (3), pd = slot (4);
        ratio = 1.5f * exp2Fast (0.5f * slot (5));
        const float mix = slot (6);
        x[0] = std::fmin (1.0f, 1.0f - mix);
        x[1] = 0.8f * std::fmin (1.0f, 1.0f + mix);
        x[2] = slot (7);
        x[3] = 1.0f + 6.0f * slot (7);
        x[4] = 1.0f / tanhFast (x[3]);
        if (model == m909)
        {
            bendMul = exp2Fast (1.4f * pe) - 1.0f;
            pcoef = decayCoef (0.02f * std::pow (30.0f, pd), sh.rate);
            penv = 1.0f;
            ph[0] = ph[1] = 0.0f;
            e[0] = amp, ec[0] = decayCoef (decay, sh.rate);
        }
        else
        {
            x[5] = (model == m808 ? 0.4f : 1.5f) * pe;             // how far the ring's size raises the pitch
            penv = 1.0f, pcoef = std::exp (-4.0f * sh.invRate / (0.001f + 0.007f * pd)); // the attack's glide, a 4th-sample step
            x[6] = 0.3f;
            res[0].r = decayCoef (decay, sh.rate);
            res[1].r = decayCoef (decay * 0.8f, sh.rate);
            res[0].strike (amp);
            if (x[1] > 0.0f)
                res[1].strike (amp);
            else
                res[1].clear(); // Osc Mix at -1: no second network, nothing of one left ringing unheard
            count = 0;
            const float c = hz * lfoPitch * invRate * (1.0f + x[5] * 0.75f * res[0].size() + x[6]);
            res[0].tune (c);
            res[1].tune (c * ratio);
        }
        e[1] = amp * slot (0) * 0.5f, ec[1] = decayCoef (0.02f * std::pow (40.0f, slot (2)), sh.rate);
        lpa[0] = onePoleCoef (200.0f * std::pow (32.0f, slot (1)), sh.rate);
        // Detail (the TR-1000's, on its 909 toms): the highs brought out, the edges of the tom squared off and kept
        // above 1.5 kHz. Attack (new): the stick's click, a pulse with some noise, gone in about 5 ms. Tone (new): a
        // low pass down from open (to 400 Hz), or a high pass up (to 800 Hz).
        x[7] = slot (8);
        f[2].setHz (1500.0f, 1.0f, sh.rate);
        e[2] = 1.2f * amp * slot (9), ec[2] = decayCoef (0.005f, sh.rate);
        const float tone = slot (10);
        x[8] = tone < 0.0f ? 1.0f : tone > 0.0f ? 2.0f : 0.0f;
        if (tone < 0.0f)
            f[3].setHz (20000.0f * exp2Fast (5.64f * tone), 0.9f, sh.rate);
        else if (tone > 0.0f)
            f[3].setHz (20.0f * exp2Fast (5.32f * tone), 0.9f, sh.rate);
    }
    [[gnu::always_inline]] bool renderTom (float* out, int n)
    {
        const float base = hz * lfoPitch * invRate;
        const bool vco = model == m909, second = x[1] > 0.0f, shape = x[2] > 0.0f;
        for (int i = 0; i < n; ++i)
        {
            float y;
            if (! vco)
            {
                if ((++count & 3) == 0) // the diodes follow the ring's size: retuned every fourth sample
                {
                    penv *= pcoef;
                    const float c = base * (1.0f + x[5] * 0.75f * res[0].size() + x[6] * penv);
                    res[0].tuneLow (c);
                    if (second)
                        res[1].tuneLow (c * ratio);
                }
                float y1 = res[0].ring();
                if (shape)
                {
                    const float size = res[0].size() + 1e-9f;
                    const float w = y1 / size;
                    y1 += x[2] * (tanhFast (x[3] * w) * x[4] - w) * size;
                }
                y = x[0] * y1 + (second ? x[1] * res[1].ring() : 0.0f);
            }
            else
            {
                float fc = base;
                if (penv > 1e-5f)
                {
                    penv *= pcoef;
                    fc = base * (1.0f + bendMul * penv);
                }
                ph[0] += fc;
                ph[0] -= std::floor (ph[0]);
                ph[1] += fc * ratio;
                ph[1] -= std::floor (ph[1]);
                float w = sin2pi (ph[0]);
                if (shape)
                    w += x[2] * (tanhFast (x[3] * w) * x[4] - w);
                y = e[0] * (x[0] * w + x[1] * sin2pi (ph[1]));
                e[0] *= ec[0];
            }
            const float nz = noise.next() * e[1];
            e[1] *= ec[1];
            lp[0] += (nz - lp[0]) * lpa[0];
            lp[1] += (lp[0] - lp[1]) * lpa[0];
            out[i] = y + 2.0f * lp[1];
        }
        // Attack, Detail and Tone, over the tom (a pass of their own, only when one is on).
        const bool click = e[2] > 0.0f, detail = x[7] > 0.0f;
        const int tone = (int) x[8];
        if (click || detail || tone != 0)
            for (int i = 0; i < n; ++i)
            {
                float o = out[i];
                if (click)
                {
                    o += e[2] * (0.6f + 0.5f * clickNoise.next());
                    e[2] *= ec[2];
                }
                if (detail)
                    o += x[7] * f[2].hp (tanhFast (8.0f * o));
                if (tone == 1)
                    o = f[3].lp (o);
                else if (tone == 2)
                    o = f[3].hp (o);
                out[i] = o;
            }
        const float body = vco ? e[0] : res[0].size() + res[1].size();
        return body > silent || e[1] > silent || std::fabs (lp[1]) > silent || e[2] > silent;
    }

    // --- Hats and cymbals -------------------------------------------------------------------------------------------
    // The metal: six square oscillators, Blend of them (1 to 6), spread by Detune, cross-modulated (XMod: each one's
    // frequency by the one before) and ring-modulated in pairs (Amp Mod), plus Noise.
    // 808: hats take the 7.1 kHz band (Tone moves it, Sculpt its Q) into a swing-type VCA, whose clipping against its
    // envelope is the sizzle, then a high pass (the open hat's lower than the closed's) and the output's
    // differentiator. The cymbal is three bands, as the 808's: the 3.44 kHz band (Filter moves it, Sculpt its Q)
    // through its VCA (Decay) and high pass; the 7.1 kHz band through two VCAs: a middle one (half the decay) and the
    // highest (short, a third-order high pass ringing near 10.5 kHz); Tone mostly sets the highest's share.
    // 909 style: the ROM voices' path: the metal and noise, high passed (and a bell band for the cymbals: Filter, its
    // Q Sculpt), falling over the sample's own length, read as 6-bit words at a 30 kHz clock that Tune moves (Error
    // takes bits away), then the VCA (Decay) and a low pass.
    // 606 style: its own six oscillators through a band pass and a high pass in parallel, a swing VCA, a high pass.
    void hitMetal (float decay, const Shared& sh, bool cymbal)
    {
        const float* set = metalHz[model];
        float geo = 1.0f;
        for (int k = 0; k < 6; ++k)
            geo *= set[k];
        geo = std::pow (geo, 1.0f / 6.0f);
        const float tone = slot (0), blend = slot (3), spread = 0.25f + 1.75f * slot (4);
        float sum = 0.0f;
        for (int k = 0; k < 6; ++k)
        {
            metalBase[k] = geo * std::pow (set[k] / geo, spread);
            gain[k] = clampf (1.0f + 5.0f * blend - (float) k, 0.0f, 1.0f) * metalWeight[k];
            sum += gain[k];
            dt[k] = std::fmin (metalBase[k] * hz * lfoPitch * invRate, 0.3f); // a step under a cycle, even swung by XMod
        }
        x[0] = 0.9f * slot (2);
        x[1] = slot (5);
        x[2] = clampf (slot (7) + metalNoise[model], 0.0f, 1.0f);
        x[3] = 1.0f / std::fmax (sum, 1.0f);
        const float err = slot (6);
        e[0] = amp, ec[0] = decayCoef (decay, sh.rate);
        e[2] = 0.0f; // the 808 cymbal's top band (set below); in the others it must not keep the voice going
        // The swing VCAs let go below a share of the envelope's peak: the 808 closed hat's fade bends down to nothing
        // (about 90 ms at the default), the open hat's holds then drops.
        const float knee = model == m909 ? 0.0f : spec->knee[model];
        x[12] = knee * amp, x[13] = 1.0f / (1.0f - knee);
        if (model == m909)
        {
            quant = exp2Fast (5.0f - 3.0f * err);
            x[9] = spec->knee[m909] > 0.0f ? 1.0f : 0.0f; // the VCA's envelope held before it falls (the crash)
            x[10] = 0.0f, e[3] = 0.0f;
            if (x[9] > 0.0f)
            {
                const float tau = decay / 9.23f;
                ec[0] = std::exp (-sh.invRate / tau), x[10] = sh.invRate / tau;
            }
            clockInc = 30000.0f * hz * lfoPitch / spec->hz[model] * invRate;
            clock = 1.0f;
            const float natural = spec->decay[m909][1] * spec->hz[model] / (hz * lfoPitch); // the sample's own length
            e[1] = 1.0f, ec[1] = decayCoef (natural, sh.rate);
            f[0].setHz ((cymbal ? 2000.0f : 3000.0f) * exp2Fast (tone), 1.4f, sh.rate);
            f[3].setHz ((cymbal ? 2000.0f : 3000.0f) * exp2Fast (tone), 1.4f, sh.rate);
            f[1].setHz (500.0f * std::pow (4.0f, slot (2) - 0.5f), 0.5f, sh.rate);
            x[4] = cymbal ? 0.6f * std::fmax (0.0f, slot (1) + 0.5f) : 0.0f; // the body band's share
            f[2].setHz (cymbal ? spec->aux : 14000.0f, 1.3f, sh.rate); // the DAC's low pass (the ride's brighter)
            f[4].setHz (13000.0f, 1.3f, sh.rate); // the ROM's band: nothing above half its 30 kHz clock
            lp[0] = lp[1] = 0.0f;                 // the words either side of now
            return;
        }
        x[6] = err;
        quant = exp2Fast (11.0f - 7.0f * err);
        holdLen = 1 + (int32_t) (err * 3.0f);
        x[7] = swingDrive[model];
        x[8] = 1.0f / (2.0f * std::sin (pi * std::fmin (7000.0f, 0.4f * sh.rate) * invRate)); // the differentiator's gain at 7 kHz
        const float shift = exp2Fast (1.2f * (cymbal ? 0.0f : tone));
        if (model == m808)
        {
            if (! cymbal)
            {
                f[0].setHz (7600.0f * shift, 1.0f / (1.5f + 3.0f * slot (1)), sh.rate);
                f[1].setHz (spec->aux * shift, 1.4f, sh.rate);
            }
            else
            {
                const float q = 2.0f * std::pow (4.0f, slot (1));
                f[0].setHz (3440.0f * exp2Fast (2.0f * (slot (2) - 0.5f)), 1.0f / q, sh.rate);
                f[1].setHz (7100.0f, 1.0f / 2.0f, sh.rate);
                f[2].setHz (2200.0f, 1.4f, sh.rate);
                f[3].setHz (5000.0f, 1.4f, sh.rate);
                f[4].setHz (10500.0f, 0.8f, sh.rate);
                lpa[0] = onePoleCoef (10500.0f, sh.rate);
                e[1] = amp, ec[1] = decayCoef (decay, sh.rate);
                e[0] = amp, ec[0] = decayCoef (decay * 0.6f, sh.rate);
                e[2] = amp, ec[2] = decayCoef (0.12f * sh.kitDecay, sh.rate);
                x[9] = 0.12f * (1.0f - 0.3f * tone), x[10] = 0.25f, x[11] = 3.0f * (1.0f + tone);
            }
        }
        else
        {
            const float lift = exp2Fast (1.2f * tone);
            f[0].setHz ((cymbal ? 6000.0f : 7000.0f) * lift, 0.33f, sh.rate);
            f[1].setHz ((cymbal ? 8000.0f : 9000.0f) * lift, 1.4f, sh.rate);
            f[2].setHz (5000.0f * lift, 1.4f, sh.rate);
            if (cymbal || knee > 0.0f)
                e[1] = amp, ec[1] = decayCoef (0.1f * sh.kitDecay, sh.rate); // the burst, over the long tail
            else
                e[1] = 0.0f; // none (the closed hat): and nothing left from another style's hit
        }
    }

    // The six oscillators and what is added to them, one sample.
    [[gnu::always_inline]] float metal()
    {
        float sum = 0.0f, o[6];
        const float xmod = x[0], duty = model == m808 ? 0.4798f : 0.5f;
        if (xmod == 0.0f)
        {
            // No cross-modulation: each oscillator runs on its own (its step exactly dt, its phase never below 0),
            // so the six are worked out four at a time.
            const f4 one = { 1.0f, 1.0f, 1.0f, 1.0f }, none = {};
            f4 pa, pb = { 0.25f, 0.25f, 0.25f, 0.25f }, da, db = { 0.01f, 0.01f, 0.01f, 0.01f };
            std::memcpy (&pa, ph, sizeof (float) * 4), std::memcpy (&pb, ph + 4, sizeof (float) * 2);
            std::memcpy (&da, dt, sizeof (float) * 4), std::memcpy (&db, dt + 4, sizeof (float) * 2);
            const f4 va = rectAt4 (pa, da, duty), vb = rectAt4 (pb, db, duty);
            pa += da, pb += db;
            pa -= pa >= 1.0f ? one : none;
            pb -= pb >= 1.0f ? one : none;
            std::memcpy (ph, &pa, sizeof (float) * 4), std::memcpy (ph + 4, &pb, sizeof (float) * 2);
            o[0] = va[0], o[1] = va[1], o[2] = va[2], o[3] = va[3], o[4] = vb[0], o[5] = vb[1];
            for (int k = 0; k < 6; ++k)
                sum += gain[k] * o[k];
            prevOsc = o[5];
        }
        else
        {
            float prev = prevOsc;
            for (int k = 0; k < 6; ++k)
            {
                const float d = dt[k] * (1.0f + xmod * prev);
                const float v = rectAt (ph[k], d, duty);
                ph[k] += d;
                ph[k] -= std::floor (ph[k]); // back into [0, 1) from either side, whatever XMod swung the step to
                prev = v;
                o[k] = v;
                sum += gain[k] * v;
            }
            prevOsc = prev;
        }
        float m = sum * x[3];
        if (x[1] > 0.0f)
            m += x[1] * ((o[0] * o[1] + o[2] * o[3] + o[4] * o[5]) * (1.0f / 3.0f) - m);
        if (x[2] > 0.0f)
            m += x[2] * (noise.next() - m);
        return m;
    }

    [[gnu::always_inline]] bool renderMetal (float* out, int n, bool cymbal)
    {
        const float level = cymbal ? cymGain[model] : hatGain[model];
        if (model == m909)
        {
            for (int i = 0; i < n; ++i)
            {
                const float m = metal();
                float s = f[3].hp (f[0].hp (m));
                if (x[4] > 0.0f)
                    s += x[4] * f[1].bp (m);
                s = f[4].lp (s) * 1.6f * e[1]; // near the ROM's full scale, short of clipping
                e[1] *= ec[1];
                clock += clockInc;
                if (clock >= 1.0f)
                {
                    clock -= std::floor (clock);
                    lp[0] = lp[1];
                    lp[1] = std::rint (clampf (s, -1.0f, 1.0f) * quant) / quant;
                }
                // The DAC's output between its words: a staircase read at the host's rate would fold its images into
                // the mids; the machine's are ultrasonic and its low pass takes them, so here the words are joined.
                held = lp[0] + (lp[1] - lp[0]) * clock;
                const float vca = e[0] + e[3]; // e[3] stays 0 unless the envelope is held
                e[3] = x[9] * ec[0] * (e[3] + x[10] * e[0]);
                out[i] = f[2].lp (held * vca) * level;
                e[0] *= ec[0];
            }
            return e[0] + e[3] > silent && e[1] > silent;
        }
        const float drive = x[7], diff = x[8];
        const bool grit = x[6] > 0.0f, is808 = model == m808;
        for (int i = 0; i < n; ++i)
        {
            const float m = metal();
            float y;
            const float kneeAt = x[12], kneeGain = x[13];
            const float g0 = std::fmax (0.0f, e[0] - kneeAt) * kneeGain;
            if (is808 && ! cymbal)
            {
                const float h = f[1].hp (g0 * tanhFast (drive * f[0].bp (m)));
                e[0] *= ec[0];
                y = (h - prevY) * diff; // the output stage's differentiator
                prevY = h;
            }
            else if (is808)
            {
                const float low = tanhFast (drive * f[0].bp (m)), high = tanhFast (drive * f[1].bp (m));
                const float g1 = std::fmax (0.0f, e[1] - kneeAt) * kneeGain;
                const float top = f[4].hp (e[2] * high);
                lp[0] += (top - lp[0]) * lpa[0];
                y = x[9] * f[2].hp (g0 * low) + x[10] * f[3].hp (g1 * high) + x[11] * (top - lp[0]);
                e[0] *= ec[0], e[1] *= ec[1], e[2] *= ec[2];
                const float d = (y - prevY) * diff; // the output stage's differentiator
                prevY = y;
                y = d;
            }
            else
            {
                const float v = 0.8f * f[0].bp (m) + 0.25f * f[1].hp (m);
                y = f[2].hp (v * (cymbal ? 0.7f * g0 + 0.9f * e[1] : g0 + 0.6f * e[1]));
                e[0] *= ec[0], e[1] *= ec[1];
            }
            y *= level;
            if (grit)
            {
                if (--holdLeft <= 0)
                {
                    holdLeft = holdLen;
                    holdValue = std::rint (y * quant) / quant;
                }
                y = holdValue;
            }
            out[i] = y;
        }
        return e[0] > std::fmax (silent, x[12]) || (cymbal && (e[1] > std::fmax (silent, x[12]) || e[2] > silent));
    }

    // --- Cowbell --------------------------------------------------------------------------------------------------
    // The 808's: two squares at 540 and 800 Hz (Osc Mix, Detune), each gated, mixed into a band pass near 1 kHz
    // (Tone) that keeps the upper on top, an abrupt first fall then a long, quiet second (its RC network; Decay). Shape from square to sine, an optional third (Unison,
    // semitones away), all frequency-modulated by a sine at Ratio times the first (XMod) with Noise in it.
    void hitCowbell (float decay, const Shared& sh)
    {
        static constexpr float ratios[] = { 1.4815f, 1.4957f, 1.507f }, band[] = { 1000.0f, 1200.0f, 1400.0f };
        ratio = ratios[model] * exp2Fast (0.3f * slot (0));
        const float mix = slot (1);
        x[0] = std::fmin (1.0f, 1.0f - mix);
        x[1] = std::fmin (1.0f, 1.0f + mix);
        x[2] = 0.5f * slot (2);
        x[3] = slot (4);
        x[4] = slot (5);
        x[5] = slot (3);
        x[6] = exp2Fast (slot (6) * (1.0f / 12.0f));
        x[7] = std::fmin (1.0f, std::fabs (slot (6)) * 2.0f);
        const float c = hz * lfoPitch * invRate;
        dt[0] = c, dt[1] = c * ratio, dt[2] = c * x[6], dt[3] = c * x[5];
        f[0].setHz (band[model] * 0.9f * exp2Fast (slot (7)), 0.67f, sh.rate);
        e[0] = amp * 0.85f, ec[0] = decayCoef (0.09f * sh.kitDecay, sh.rate);
        e[1] = amp * 0.15f, ec[1] = decayCoef (decay, sh.rate);
    }
    [[gnu::always_inline]] float bellOsc (int k, float d, float shape)
    {
        const float sq = rectAt (ph[k], d, model == m808 ? 0.4798f : 0.5f);
        const float w = shape > 0.0f ? sq + shape * (sin2pi (ph[k]) - sq) : sq;
        ph[k] += d;
        ph[k] -= std::floor (ph[k]);
        return w;
    }
    [[gnu::always_inline]] bool renderCowbell (float* out, int n)
    {
        const bool unison = x[7] > 0.0f, modulated = x[2] > 0.0f;
        for (int i = 0; i < n; ++i)
        {
            float fm = 1.0f;
            if (modulated)
            {
                const float mod = sin2pi (ph[3]) + x[3] * noise.next();
                ph[3] += dt[3];
                ph[3] -= std::floor (ph[3]); // whole cycles: the step can pass one at the Keys voice's top
                fm = 1.0f + x[2] * mod;
            }
            const float a = bellOsc (0, dt[0] * fm, x[4]), b = bellOsc (1, dt[1] * fm, x[4]);
            float y = x[0] * a + x[1] * b;
            if (unison)
                y += x[7] * bellOsc (2, dt[2] * fm, x[4]);
            out[i] = f[0].bp (y) * (e[0] + e[1]);
            e[0] *= ec[0], e[1] *= ec[1];
        }
        return e[0] + e[1] > silent;
    }

    // --- Claves ---------------------------------------------------------------------------------------------------
    // The 808's: one high-Q network at 2.5 kHz struck short (two in the 909 style: a wood block), Attack a tick of
    // noise on the strike, Tone tilts it.
    void hitClave (float decay, const Shared& sh)
    {
        res[0].tune (hz * lfoPitch * invRate, decayCoef (decay, sh.rate));
        res[0].strike (amp);
        if (model == m909)
        {
            res[1].tune (hz * lfoPitch * 1.47f * invRate, decayCoef (decay * 0.7f, sh.rate));
            res[1].strike (amp * 0.6f);
        }
        else
            res[1].clear(); // a 909 hit's second network, unheard here, must not keep the voice going
        e[1] = amp * slot (0) * 0.8f, ec[1] = decayCoef (0.0015f, sh.rate);
        lpa[0] = onePoleCoef (1500.0f, sh.rate);
        x[0] = slot (1);
    }
    [[gnu::always_inline]] bool renderClave (float* out, int n)
    {
        const bool two = model == m909;
        for (int i = 0; i < n; ++i)
        {
            float y = res[0].ring() + (two ? 0.7f * res[1].ring() : 0.0f) + e[1] * noise.next();
            e[1] *= ec[1];
            lp[0] += (y - lp[0]) * lpa[0];
            out[i] = 1.3f * (lp[0] * (1.0f - x[0]) + (y - lp[0]) * (1.0f + x[0]));
        }
        return res[0].size() + res[1].size() > silent || e[1] > silent;
    }

    // --- Maracas --------------------------------------------------------------------------------------------------
    // White noise gated and high passed (Tune moves the corner, Tone too), swelling over Attack and falling over
    // Decay; the 909 style adds a ringing band.
    void hitMaraca (float decay, const Shared& sh)
    {
        const float corner = hz * lfoPitch * exp2Fast (slot (1));
        f[0].setHz (corner, 1.2f, sh.rate);
        f[1].setHz (corner * 1.3f, 0.4f, sh.rate);
        const float attack = 0.5f + 30.0f * slot (0) * slot (0);
        rampLen = std::max (1, (int32_t) (attack * 0.001f * sh.rate));
        count = 0;
        e[0] = amp, ec[0] = decayCoef (decay, sh.rate);
        lpa[0] = onePoleCoef (12000.0f, sh.rate);
    }
    [[gnu::always_inline]] bool renderMaraca (float* out, int n)
    {
        const bool band = model == m909;
        const float step = 1.0f / (float) rampLen;
        for (int i = 0; i < n; ++i)
        {
            const float nz = noise.next();
            float y = f[0].hp (nz);
            if (band)
                y = 0.6f * y + 0.5f * f[1].bp (nz);
            lp[0] += (y - lp[0]) * lpa[0];
            y = lp[0];
            float g = e[0];
            if (count < rampLen)
                g *= step * (float) ++count;
            else
                e[0] *= ec[0];
            out[i] = y * g;
        }
        return e[0] > silent;
    }

    // --- The shared chain -----------------------------------------------------------------------------------------
    // Bend (the TR-1000's Reformer, as four characters in a row: fold, rectify, crush, decimate; Bend Type is the
    // place between them, Bend the depth), Drive, the filter, then the amplitude: Attack Time, Gate, a choke's fade.
    [[gnu::always_inline]] float bendChar (int k, float v)
    {
        switch (k)
        {
            case 0:
            {
                float u = (v * (1.0f + 5.0f * bendDepth) + 1.0f) * 0.25f;
                u -= std::floor (u);
                return 1.0f - 4.0f * std::fabs (u - 0.5f);
            }
            case 1: return v + bendDepth * (1.5f * std::fabs (v) - v);
            case 2: return std::rint (v * bendQuant) / bendQuant;
            default: return bendHeld;
        }
    }
    void chain (float* out, int n, const Shared& sh)
    {
        if (bendOn && bendMix < 1.0f)
            shape<true> (out, n, sh);
        else if (bendOn || driveGain > 1.0f || filterMode != 0 || attackInc > 0.0f || gateLeft >= 0 || fading)
            shape<false> (out, n, sh); // otherwise nothing in it: the voice as it is
        if (inverse) // Phase: Inverse
            for (int i = 0; i < n; ++i)
                out[i] = -out[i];
    }
    // `mixing`: Bend Mix under 100 % (its own copy of the loop, so the usual one stays as lean as it was).
    template <bool mixing> void shape (float* out, int n, const Shared& sh)
    {
        // A choke's or the gate's fade: the voice's own Mute Decay (the TR-1000's MUTE DCY), or the kit's Choke Time.
        const float fadeCoef = ownMute ? muteCoef : sh.chokeCoef;
        const int seg = std::min (2, (int) bendPos);
        const float frac = bendPos - (float) seg;
        const bool decimating = bendOn && seg + 1 >= 3;
        for (int i = 0; i < n; ++i)
        {
            float v = out[i];
            if (bendOn)
            {
                if (decimating)
                {
                    bendCount += 1.0f;
                    if (bendCount >= bendHold)
                        bendCount -= bendHold, bendHeld = v;
                }
                const float a = bendChar (seg, v);
                const float b = frac > 0.0f ? bendChar (seg + 1, v) : a;
                if (mixing)
                    v += bendMix * (dc.run (a + (b - a) * frac) - v); // Bend Mix: the reformer's amount
                else
                    v = dc.run (a + (b - a) * frac);
            }
            if (driveGain > 1.0f)
                v = tanhFast (v * driveGain) * driveNorm;
            if (filterMode != 0)
            {
                float low, band;
                flt.run (v, low, band);
                v = filterMode == 1 ? low : filterMode == 2 ? v - flt.k * band - low : band * flt.k;
            }
            float g = 1.0f;
            if (attackInc > 0.0f)
            {
                attackPos = std::fmin (1.0f, attackPos + attackInc);
                g = attackPos;
                if (attackPos >= 1.0f)
                    attackInc = 0.0f;
            }
            if (gateLeft >= 0)
            {
                if (gateLeft == 0)
                    fading = true;
                --gateLeft;
            }
            if (fading)
            {
                fade *= fadeCoef;
                g *= fade;
            }
            out[i] = v * g;
        }
    }
};
} // namespace foundry
