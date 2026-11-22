// Mystik's drum: a complex oscillator and an 8-mode membrane, struck by a hand, folded, then through a modelled vactrol
// low-pass gate. Run at twice the host's rate and filtered back down. See docs/design.md for the sources and numbers.
//
// Cost: the per-sample path is two sines, the membrane as two four-lane rotations, a fold (one more sine) and a
// trapezoidal state-variable filter; everything that moves slowly (pitch, the membrane's angles, the vactrol's time
// constants, the gate's coefficients from its resistance) is worked out every 8 inner samples. A closed, quiet drum
// costs nothing: the voice goes idle and outputs zeros until the next strike.
#pragma once

#include "dsp.h"
#include "params.h"

namespace mystik
{
// --- Bodies: 8 modes each (ratios to the fundamental, amplitudes struck at the centre and at the edge, decays relative
// to Ring), and how the strike bends their pitch. Numbers fixed with the Body choice: append only.
struct Body
{
    float ratio[8];
    float centre[8], edge[8];
    float decay[8];
    float bend;
};
inline constexpr Body bodies[] = {
    // Tabla: a loaded membrane, its first modes nearly harmonic (Raman); the centre is "tun", the rim "na".
    { { 1, 2, 3, 4, 5, 6.02f, 7.1f, 8.25f }, { 1, .45f, .2f, .1f, .05f, .03f, .02f, .01f },
      { .35f, .8f, .7f, .55f, .4f, .3f, .25f, .2f }, { 1, .85f, .7f, .55f, .42f, .32f, .25f, .2f }, 1.0f },
    // Conga: an ideal membrane, the Bessel zeros (0,1) (1,1) (2,1) (0,2) (3,1) (1,2) (4,1), only the round modes moving
    // at the centre, and the open shell's air column under it (0.47: a palm at the centre booms). The shell damps the
    // upper modes.
    { { 1, 1.594f, 2.136f, 2.296f, 2.653f, 2.918f, 3.156f, 0.47f }, { 1, .05f, .03f, .35f, .02f, .05f, .01f, .45f },
      { .5f, .8f, .7f, .3f, .6f, .5f, .45f, .04f }, { 1, .6f, .45f, .5f, .35f, .3f, .25f, 1.3f }, 1.0f },
    // Frame: the same membrane, thin and open: bright and long.
    { { 1, 1.594f, 2.136f, 2.296f, 2.653f, 2.918f, 3.156f, 3.501f }, { 1, .1f, .05f, .4f, .05f, .1f, .05f, .08f },
      { .4f, .9f, .8f, .4f, .75f, .6f, .6f, .5f }, { 1, .9f, .8f, .85f, .7f, .65f, .6f, .55f }, 0.8f },
    // Udu: a clay pot; the hole's air (a strong low mode) rises in pitch after the strike, the clay pings above it.
    { { 1, 1.98f, 2.75f, 3.9f, 5.1f, 6.3f, 7.7f, 9.1f }, { 1, .2f, .15f, .1f, .05f, .03f, .02f, .01f },
      { .3f, .5f, .6f, .5f, .4f, .35f, .25f, .2f }, { 1, .4f, .35f, .3f, .25f, .2f, .15f, .12f }, -1.4f },
    // Slit: a wooden log drum's tongue, a free bar (1, 2.756, 5.404, 8.933, 13.34) and a few weaker wood modes.
    { { 1, 2.756f, 5.404f, 8.933f, 13.34f, 2.03f, 3.6f, 4.4f }, { 1, .3f, .15f, .05f, .02f, .1f, .05f, .03f },
      { .6f, .7f, .5f, .35f, .2f, .2f, .15f, .1f }, { 1, .45f, .25f, .15f, .1f, .3f, .2f, .15f }, 0.3f },
    // Handpan: steel, tuned fundamental, octave and twelfth, then the shell.
    { { 1, 2, 3, 4.03f, 5.2f, 6.1f, 7.4f, 8.9f }, { 1, .5f, .35f, .1f, .05f, .04f, .03f, .02f },
      { .6f, .7f, .6f, .3f, .25f, .2f, .15f, .1f }, { 1, .9f, .8f, .5f, .4f, .35f, .3f, .25f }, 0.2f },
    // Bowl: a singing bowl's modes in pairs a hair apart (the bowl is never quite round), so each beats.
    { { 1, 1.003f, 2.71f, 2.7155f, 5.15f, 5.1595f, 8.3f, 8.315f }, { .9f, .9f, .35f, .35f, .15f, .15f, .06f, .06f },
      { .6f, .6f, .5f, .5f, .3f, .3f, .15f, .15f }, { 1, 1, .7f, .7f, .45f, .45f, .3f, .3f }, 0.05f },
    // Bell: an iron agogo or gankogui, inharmonic and bright.
    { { 1, 1.84f, 2.94f, 4.26f, 5.62f, 7.1f, 8.6f, 10.2f }, { 1, .7f, .5f, .4f, .3f, .2f, .15f, .1f },
      { .7f, .8f, .7f, .6f, .5f, .4f, .3f, .2f }, { 1, .8f, .6f, .5f, .4f, .35f, .3f, .25f }, 0.1f },
    // Goblet (0.2): a doumbek or djembe, a membrane over a goblet's air (0.42, its Helmholtz boom): the centre's
    // dum is mostly air and fundamental, the rim's tek the upper modes.
    { { 0.42f, 1, 1.594f, 2.136f, 2.296f, 2.653f, 2.918f, 3.156f }, { .9f, 1, .05f, .03f, .35f, .02f, .05f, .01f },
      { .06f, .45f, .85f, .75f, .3f, .65f, .55f, .5f }, { 1.6f, 1, .5f, .4f, .45f, .3f, .28f, .25f }, 1.2f },
};
constexpr int bodyCount = (int) (sizeof (bodies) / sizeof (bodies[0]));
static_assert (bodyCount == (int) (sizeof (bodyChoices) / sizeof (bodyChoices[0])), "a body a name");

// --- Strokes: how a hand strikes. Force, the contact's length (shorter reaches higher modes), where it lands (a share
// toward a place of its own, else the Position knob), its noise burst, how it damps the membrane and the gate, and how
// much it bends the pitch.
struct StrokeShape
{
    float force, widthMs, place, placeShare; // how hard, the contact's length at full force, where (a share toward `place`)
    float burst, burstMs, noiseHz, bright;   // the hand's noise: level, decay, its crack's band, its colour into the skin
    float modeDamp, fundDamp;                // the membrane's decays this stroke (all modes; the fundamental again)
    float gateDamp, bend, contact;           // the gate's fall, the pitch bend, how much the hand damps what still rings
};
inline constexpr StrokeShape strokeShapes[Stroke::count] = {
    { 1.00f, 1.0f, 0.0f, 0.0f, 0.35f, 3.0f, 1600.0f, 0.6f, 1.0f, 1.0f, 1.0f, 1.0f, 0.45f },    // Open
    { 1.00f, 0.3f, 0.92f, 0.8f, 1.0f, 7.0f, 2800.0f, 1.0f, 0.5f, 0.25f, 0.55f, 0.5f, 0.75f },   // Slap: fingers stay on
    { 1.00f, 3.0f, 0.05f, 0.8f, 0.08f, 1.5f, 450.0f, 0.2f, 0.8f, 1.6f, 1.25f, 1.5f, 0.6f },     // Bass: the palm's boom
    { 0.90f, 1.2f, 0.0f, 0.0f, 0.4f, 2.0f, 1200.0f, 0.5f, 0.12f, 0.8f, 0.3f, 0.6f, 0.95f },     // Mute
    { 0.35f, 1.6f, 0.0f, 0.0f, 0.12f, 2.0f, 1400.0f, 0.4f, 0.8f, 1.0f, 0.8f, 0.5f, 0.25f },     // Ghost
    { 1.00f, 1.0f, 0.0f, 0.0f, 0.35f, 3.0f, 1600.0f, 0.6f, 1.0f, 1.0f, 1.0f, 1.0f, 0.45f },    // Flam (the engine plays the grace)
    { 1.00f, 1.0f, 0.0f, 0.0f, 0.35f, 3.0f, 1600.0f, 0.6f, 1.0f, 1.0f, 1.0f, 1.0f, 0.5f },     // Roll (the engine plays four)
    { 1.00f, 1.2f, 0.0f, 0.0f, 0.25f, 2.5f, 1300.0f, 0.5f, 1.0f, 1.0f, 1.0f, 1.0f, 0.55f },    // Press
};

// What the engine asks of a strike.
struct Hit
{
    float force = 1.0f;  // 0..1 (velocity, a ghost's or a roll's softness)
    int stroke = Stroke::open;
    bool accent = false;
    bool slide = false;  // glide to the pitch without a new strike
    bool hold = false;   // keep the LED on (Hold) until release()
    bool follow = false; // a further hit of one stroke (a flam's main hit, a roll's, a bounce's): Hold stays as it is,
                         // so one coming after the note (or the step's gate) has ended does not light it again
    float semis = 0.0f;  // the note, from Tune
};

// What the voice's knobs mean, as the engine hands them over (raw parameter values).
struct VoiceKnobs
{
    float tune = 0, source = 0.6f, ring = 0.45f, position = 0.35f, snap = 0.2f, bend = 0.2f, drift = 0.1f;
    float fmIndex = 0.2f, feedback = 0, timbre = 0.12f, symmetry = 0, bloom = 0.3f, human = 0.3f;
    float gateMode = 0.5f, resonance = 0, strike = 0.4f, decay = 0.45f, open = 0, hold = 0, memory = 0.3f, accent = 0.5f,
          glide = 0.35f;
    int body = 1, ratio = 10;
};

// --- The gate's analog response: shared by the voice and the screen's plot ------------------------------------------
// The 292 (Parker and D'Angelo, DAFx-13): H(s) = gain / (a1 + a2 s + a3 s^2) for the light x, Gate Mode and
// Resonance. Returned as the analog poles' natural frequency and Q, and the DC gain.
struct GateResponse
{
    double w0 = 0.0, q = 0.5, gain = 0.0;
    double a1 = 1.0, a2 = 0.0, a3 = 0.0; // H(s) = gain * a1 / (a1 + a2 s + a3 s^2)
};
// The parts Gate Mode and Resonance set (worked out when they move): Ra, C3, the VCA mode's make-up gain.
struct GateCircuit
{
    double ra = 5e6, c3 = 0.0, boost = 1.0, resonance = 0.0;
};
inline GateCircuit gateCircuit (double mode, double resonance)
{
    GateCircuit c;
    c.ra = mode < 0.5 ? 5e3 * std::pow (1000.0, 2.0 * mode) : 5e6;
    c.c3 = 4.7e-9 * std::max (mode > 0.5 ? 2.0 * mode - 1.0 : 0.0, resonance);
    c.boost = mode < 0.5 ? std::pow (10.0, 4.5 / 20.0 * (1.0 - 2.0 * mode)) : 1.0;
    c.resonance = resonance;
    return c;
}
inline double ldrOhms (double light)
{
    const double current = std::max (light, 2.5e-7) * 0.040; // amps: x = 1 is the 292's 40 mA ceiling
    return 3.464 / std::pow (current, 1.4) + 1136.212;
}
inline GateResponse gateResponse (double rf, const GateCircuit& c)
{
    constexpr double c1 = 1e-9, c2 = 220e-12;
    const double ra = c.ra, c3 = c.c3, boost = c.boost;
    double a = 1.0;
    if (c3 > 0.0)
    {
        const double amax = (2.0 * c1 * ra + (c2 + c3) * (ra + rf)) / (c3 * ra);
        a = 1.0 + c.resonance * (0.985 * amax - 1.0);
    }
    GateResponse g;
    g.a1 = 1.0 + 2.0 * rf / ra;
    g.a2 = rf * (2.0 * c1 + c2 - c3 * (a - 1.0) + (c2 + c3) * rf / ra);
    g.a3 = rf * rf * c1 * (c2 + c3);
    g.gain = boost / g.a1;
    g.w0 = std::sqrt (g.a1 / g.a3);
    g.q = std::sqrt (g.a1 * g.a3) / g.a2;
    return g;
}
// |H| in dB at `hz` (the screen's curve).
inline double gateDb (const GateResponse& g, double hz)
{
    const double w = 2.0 * M_PI * hz;
    const double re = g.a1 - g.a3 * w * w, im = g.a2 * w;
    return 20.0 * std::log10 (std::max (1e-12, g.gain * g.a1 / std::sqrt (re * re + im * im)));
}

class Voice
{
public:
    void prepare (double hostRate)
    {
        fs = (float) (2.0 * hostRate);
        invFs = 1.0f / fs;
        decimator.design();
        reset();
    }

    void reset()
    {
        phaseP = phaseM = 0.0f;
        last = 0.0f;
        mu0 = mu1 = mv0 = mv1 = splat (0.0f);
        mg0 = mg1 = splat (0.0f);
        pulseN = pulseLen = 0;
        burst = burstLp = 0.0f;
        exciting = false;
        light = history = cap = 0.0f;
        strikeLeft = 0;
        strikeLevel = 0.0f;
        held = false;
        ic1 = ic2 = 0.0f;
        note = noteTarget = bendSemis = pressSemis = pressTarget = driftValue = driftTarget = 0.0f;
        decimator.clear();
        idle = true;
        lastF0 = -1.0f;
        gateDirty = true;
        noise = 0x2545f491u;
        tick = 0;
        smoothed = false;
        retune();
    }

    void set (const VoiceKnobs& k)
    {
        knobs = k;
        dirty = true;
        if (idle && k.open > 0.0f) // Open lights the gate with no strike: a drone starts now
            wake();
    }

    // A strike (or a slide into a new pitch).
    void strike (const Hit& h)
    {
        noteTarget = h.semis;
        if (h.slide && ! idle)
        {
            sliding = true;
            held = h.hold || held;
            return;
        }
        sliding = false;
        note = noteTarget;
        const StrokeShape& s = strokeShapes[h.stroke & 7];
        const Body& b = bodies[clampBody (knobs.body)];
        // Human: no two hits alike (force, place, the contact, the bend, each mode's tuning a hair), the same each time
        // for the same strike count.
        const float hu = knobs.human;
        auto jitter = [this, hu] (uint32_t salt) { return hu * (unit ((int64_t) strikes, salt) - 0.5f); };
        const float force = clampf (h.force * s.force * (1.0f + 0.16f * jitter (0x1f83d9abu)), 0.0f, 1.0f);
        // The accent capacitor: half of what it lacks at each accent, draining slowly between.
        float boost = 0.0f;
        if (h.accent)
        {
            cap += (1.0f - cap) * 0.5f;
            boost = knobs.accent * (0.6f + 0.8f * cap);
        }
        lastForce = force;
        lastAccent = h.accent;
        // How loud the hand plays into the gate (the vactrol alone barely quiets a low tone): eased in over a tick.
        levelTarget = (0.2f + 0.8f * force) * (1.0f + 0.25f * boost);
        easing = true; // a new target: ease to it (the knobs' easing rests once everything has landed)
        // The LED: a pulse of Strike's length at the force (accents harder), then Hold while the gate is held. A ping
        // lights the vactrol only partly (a default one to about 0.08 of the 292's full 40 mA, a gate near 6 kHz), so
        // the gate closes on the note in a few hundred ms, as a pinged 292 does; a longer Strike opens it further.
        strikeLeft = std::max (1, (int) std::lround (strikeSeconds (knobs.strike) * fs));
        strikeLevel = 0.4f * force * (1.0f + boost);
        held = h.follow ? held : h.hold;
        holdLevel = force * (1.0f + 0.5f * boost) * knobs.hold * 0.06f;
        gateDamp = s.gateDamp;
        // The hand lands on a head that may still ring: its contact damps that first (a mute most, a ghost least).
        if (! idle && skinActive)
        {
            const v4 keep = splat (1.0f - s.contact * (0.4f + 0.6f * force));
            mu0 *= keep, mv0 *= keep, mu1 *= keep, mv1 *= keep;
        }
        // The membrane: where it is struck, how long it rings for this stroke (the fundamental by its own share).
        float place = s.placeShare > 0.0f ? knobs.position + (s.place - knobs.position) * s.placeShare : knobs.position;
        place = clampf (place + 0.16f * jitter (0x2b7e1516u), 0.0f, 1.0f);
        float amp[8], total = 0.0f;
        for (int i = 0; i < 8; ++i)
            amp[i] = b.centre[i] + (b.edge[i] - b.centre[i]) * place, total += amp[i];
        const float norm = total > 0.0f ? 1.0f / total : 0.0f;
        const float ringT = ringSeconds (knobs.ring) * s.modeDamp;
        float fundamentalT60 = ringT;
        for (int i = 0; i < 8; ++i)
        {
            const bool fundamental = b.ratio[i] == 1.0f;
            const float t60 = std::max (0.004f, ringT * b.decay[i] * (fundamental ? s.fundDamp : 1.0f));
            if (fundamental) fundamentalT60 = t60;
            const float r = (float) std::exp (-6.907755 / ((double) t60 * fs));
            (i < 4 ? mg0 : mg1)[i & 3] = amp[i] * norm;
            (i < 4 ? mr0 : mr1)[i & 3] = r;
            (i < 4 ? detune0 : detune1)[i & 3] = 1.0f + 0.008f * jitter (0x9e3779b9u + (uint32_t) i * 0x632be5abu);
        }
        // The hand: a raised-cosine contact of area `force`, shorter the harder (it reaches higher modes: a soft hand is
        // darker), and a noise burst: broadband into the skin, and its crack (a band around noiseHz) into the gate.
        const float width = s.widthMs * (1.4f - 0.6f * force) * (1.0f + 0.4f * jitter (0x3c6ef372u));
        pulseLen = std::max (2, (int) std::lround (width * 0.001f * fs));
        pulseN = 0;
        pulseStep = 0.5f / (float) pulseLen;
        pulseAmp = 2.0f * force / (float) pulseLen * skinDrive;
        burst = force * std::sqrt (force) * s.burst;
        burstDecay = (float) std::exp (-1.0 / ((double) s.burstMs * 0.001 * fs));
        burstCoef = onePoleCoeff (2.0f * (float) M_PI * (1500.0f + 14000.0f * s.bright) * invFs);
        {
            constexpr float q = 1.3f;
            const float g = tanPi (std::min (s.noiseHz * invFs, 0.45f));
            crackK = 1.0f / q;
            crackA1 = 1.0f / (1.0f + g * (g + crackK));
            crackA2 = g * crackA1;
            crackA3 = g * crackA2;
            crackS1 = crackS2 = 0.0f;
        }
        exciting = true;
        // The pitch: tension bends it (harder hits more; the body and stroke set the sign and depth) and the bend falls
        // as the head's swing does (amplitude squared: twice as fast as the fundamental's ring); Press rises.
        bendSemis = knobs.bend * 12.0f * force * b.bend * s.bend * (1.0f + 0.5f * jitter (0x4f1bbcdcu));
        const double bendTau = std::min (0.15, std::max (0.012, (double) fundamentalT60 / 13.8));
        bendDecay = (float) std::exp (-8.0 / (bendTau * fs));
        pressTarget = h.stroke == Stroke::press ? 2.0f + 5.0f * std::fabs (knobs.bend) : 0.0f;
        if (h.stroke != Stroke::press)
            pressSemis = 0.0f;
        if (idle)
            wake();
        lastF0 = -1.0f; // the membrane's angles and decays again
        modesDirty = true;
        gateDirty = true;
        ++strikes;
        control(); // the strike's first sample with its own pitch, gate and knobs,
        tick = 1;  // and the slow parts counted from the strike: a hit sounds the same wherever it falls
    }

    void release() { held = false; }

    // A choke: silence now (a cut, Melodee's all sound off).
    void choke()
    {
        light = 0.0f;
        strikeLeft = 0;
        held = false;
        exciting = false;
        mu0 = mu1 = mv0 = mv1 = splat (0.0f);
        ic1 = ic2 = last = 0.0f;
        decimator.clear();
        idle = true;
    }

    void wheel (float bendSemitones, float modWheel) { wheelSemis = bendSemitones, wheelFold = modWheel; }

    // `frames` at the host's rate into `out` (mono). True if they were all silence.
    bool run (float* out, uint32_t frames)
    {
        for (uint32_t i = 0; i < frames; ++i)
        {
            if (idle && decimator.silent())
            {
                // Asleep: the rest is silence, nothing to work out. The control ticks it skips are counted (the
                // accent capacitor and the light's history catch up when it wakes), so it is the same whatever the
                // block size.
                const uint32_t n = frames - i;
                std::memset (out + i, 0, sizeof (float) * n);
                skippedTicks += ((tick + n + 3) >> 2) - ((tick + 3) >> 2);
                tick += n;
                return i == 0;
            }
            if ((tick++ & 3) == 0)
                control();
            if (idle)
            {
                out[i] = decimator.push (0.0f, 0.0f);
                continue;
            }
            const float a = inner(), b = inner();
            out[i] = decimator.push (a, b);
        }
        return false;
    }

    // For the screen and tests.
    float lightLevel() const { return light; }
    uint32_t strikeCount() const { return strikes; }
    bool isIdle() const { return idle && decimator.silent(); }
    float frequency() const { return lastF0; }
    float lastStrikeForce() const { return lastForce; }
    bool lastStrikeAccented() const { return lastAccent; }

private:
    static int clampBody (int b) { return b < 0 ? 0 : (b >= bodyCount ? bodyCount - 1 : b); }

    // From sleep: the gate and oscillator from rest, the knobs as they are (nothing to ease from), and the accent
    // capacitor and the light's history drained as they would have been over the sleep (a control tick each 4 frames).
    void wake()
    {
        idle = false;
        ic1 = ic2 = last = 0.0f;
        smoothed = false;
        easing = true;
        if (skippedTicks > 0)
        {
            cap *= (float) std::pow ((double) capDrain, (double) skippedTicks);
            history *= (float) std::pow (1.0 - (double) historyCoef, (double) skippedTicks);
            skippedTicks = 0;
        }
    }

    // Every 8 inner samples: the slow parts.
    void control()
    {
        if (dirty)
            retune();
        // Knobs ease (about 1.5 ms); a step that no longer moves the float lands exactly. Nothing to do once all landed.
        if (easing || ! smoothed)
        {
            const float ease = smoothed ? 0.12f : 1.0f;
            smoothed = true;
            bool moving = false;
            auto glideTo = [ease, &moving] (float& value, float target) {
                const float next = value + (target - value) * ease;
                value = (next == value) ? target : next;
                moving |= value != target;
            };
            glideTo (oscLevel, oscTarget);
            glideTo (skinLevel, skinTarget);
            glideTo (snapLevel, snapTarget);
            glideTo (indexKnob, indexTarget);
            glideTo (feedbackLevel, feedbackTarget);
            glideTo (foldKnob, foldTarget);
            glideTo (biasKnob, biasTarget);
            glideTo (modeKnob, modeTarget);
            glideTo (resKnob, resTarget);
            glideTo (openLevel, openTarget);
            glideTo (tuneSemis, tuneTarget);
            glideTo (level, levelTarget);
            easing = moving;
        }

        // The accent capacitor drains (300 ms); the light's history follows the light (2 s).
        cap *= capDrain;
        history += (std::min (light, 1.0f) - history) * historyCoef;

        if (idle)
            return;

        // Pitch: glide, bend, press, drift, the wheel.
        if (sliding)
        {
            const float next = note + (noteTarget - note) * glideCoef;
            note = next == note ? noteTarget : next;
            sliding = note != noteTarget;
        }
        bendSemis *= bendDecay;
        if (std::fabs (bendSemis) < 1e-4f) bendSemis = 0.0f;
        pressSemis += (pressTarget - pressSemis) * pressCoef;
        if (driftDepth > 0.0f)
        {
            if (++driftTicks >= driftEvery)
            {
                driftTicks = 0;
                driftTarget = 2.0f * unit (++driftCount, 0x6d2b79f5u) - 1.0f;
            }
            driftValue += (driftTarget - driftValue) * driftCoef;
        }
        const float semis = tuneSemis + note + bendSemis + pressSemis + driftDepth * driftValue + wheelSemis;
        const float f0 = clampf (tuneRootHz * exp2Fast (semis * (1.0f / 12.0f)), 8.0f, 6000.0f);
        if (f0 != lastF0)
        {
            lastF0 = f0;
            incP = f0 * invFs;
            incM = incP * ratioValues[knobs.ratio & 15];
        }
        // The membrane: each mode's angle a sample (as a share of pi), faded out near the inner Nyquist; worked out again
        // when the pitch has moved 0.02 cents (a drift's slow wander, a bend's fall) or a strike set new decays.
        if (modesDirty || std::fabs (f0 - modeF0) > 1e-5f * modeF0)
        {
            modesDirty = false;
            modeF0 = f0;
            const v4 t0 = splat (2.0f * incP) * ratio0 * detune0, t1 = splat (2.0f * incP) * ratio1 * detune1;
            auto modes = [] (v4 t, v4 r, v4& c, v4& s, v4& w) {
                for (int i = 0; i < 4; ++i)
                    w[i] = clampf ((0.9f - t[i]) * 10.0f, 0.0f, 1.0f), t[i] = std::min (t[i], 0.9f);
                const v4 sn = sinPi4 (t), cs = sinPi4 (0.5f - t);
                c = r * cs, s = r * sn;
            };
            modes (t0, mr0, mc0, ms0, mw0);
            modes (t1, mr1, mc1, ms1, mw1);
        }

        // The vactrol: quicker when bright; Decay, this stroke's damping and the light's history set the fall.
        const float lit = std::min (light, 1.0f);
        const float rise = 0.012f * (1.0f - 0.5f * lit);
        const float dark = 1.0f - lit;
        const float fall = decayTau * gateDamp * (0.4f + 0.6f * dark * dark) * std::min (6.0f, 1.0f + 12.0f * knobs.memory * history);
        cRise = onePoleCoeff (invFs / rise);
        cFall = onePoleCoeff (invFs / fall);
        ledBase = openLevel + (held ? holdLevel : 0.0f);

        // Bloom: the light opens the fold and the modulation further.
        const float bloomLit = knobs.bloom * lit;
        const float depth = clampf (foldKnob + bloomLit + wheelFold, 0.0f, 1.0f);
        foldMix = clampf (20.0f * (depth + std::fabs (biasKnob)), 0.0f, 1.0f); // from bypass over the first 5 %
        folding = foldMix > 0.0f;
        const float g = 1.0f + 11.0f * depth * depth;
        foldGain = g * (1.0f / (2.0f * (float) M_PI));
        foldBias = biasKnob * 0.25f; // Symmetry * pi/2, in turns
        foldDc = sinTurns (foldBias);
        const float index = indexKnob * (1.0f + 2.0f * bloomLit);
        pmIndex = std::min (6.0f * index * index, 8.0f) * (1.0f / (2.0f * (float) M_PI));
        pmFeedback = 0.5f * feedbackLevel * feedbackLevel;

        // The gate's coefficients from its resistance, when the light moved 0.1 % (its cutoff 0.14 %) or the knobs moved.
        if (gateDirty || std::fabs (light - gateLight) > 1e-7f + 1e-3f * gateLight || modeKnob != gateMode || resKnob != gateRes)
            gateCoefficients();

        // Idle: closed, nothing driving it, the membrane still, nothing coming out.
        const v4 energy = vabs (mu0) + vabs (mv0) + vabs (mu1) + vabs (mv1);
        skinActive = (exciting || sum (energy) > 1e-7f) && (skinLevel != 0.0f || skinTarget != 0.0f); // Source at 0: no skin
        oscOn = oscLevel != 0.0f || oscTarget != 0.0f;                                                   // at 100 %: no oscillator
        if (! skinActive)
            mu0 = mu1 = mv0 = mv1 = splat (0.0f);
        if (light < 2e-4f && ledBase == 0.0f && strikeLeft == 0 && ! skinActive && quietOut < 1e-6f)
        {
            idle = true;
            ic1 = ic2 = last = light = 0.0f; // dark: the next strike starts from nothing, whenever it comes
        }
        quietOut = 0.0f;
    }

    void gateCoefficients()
    {
        gateDirty = false;
        gateLight = light, gateMode = modeKnob, gateRes = resKnob;
        // Rf from the light (the LED current's power law), in float with the fast log and exp.
        const float current = std::max (light, 2.5e-7f) * 0.040f;
        const float rf = 3.464f * exp2Fast (-1.4f * log2Fast (current)) + 1136.212f;
        if (modeKnob != circuitMode || resKnob != circuitRes)
            circuit = gateCircuit (modeKnob, resKnob), circuitMode = modeKnob, circuitRes = resKnob;
        const GateResponse r = gateResponse (rf, circuit);
        gain = (float) r.gain;
        // The poles. Real (Q <= 0.5): prewarp each, held under 0.49 of the rate; complex: the pair.
        const double disc = r.a2 * r.a2 - 4.0 * r.a1 * r.a3;
        const double toTurns = 1.0 / (2.0 * M_PI * (double) fs);
        float g, k;
        if (disc >= 0.0)
        {
            const double root = std::sqrt (disc);
            const double p1 = 2.0 * r.a1 / (r.a2 + root), p2 = (r.a2 + root) / (2.0 * r.a3); // no cancellation
            const float g1 = tanPi (std::min ((float) (p1 * toTurns), 0.49f)), g2 = tanPi (std::min ((float) (p2 * toTurns), 0.49f));
            g = std::sqrt (g1 * g2);
            k = (g1 + g2) / g;
        }
        else
        {
            g = tanPi (std::min ((float) (r.w0 * toTurns), 0.49f));
            k = (float) (1.0 / r.q);
        }
        a1 = 1.0f / (1.0f + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
        limiting = k < 0.5f;
    }

    // What the knobs ask for: worked out when one moved.
    void retune()
    {
        dirty = false;
        const float src = knobs.source;
        oscTarget = 1.0f - src;
        skinTarget = src;
        snapTarget = 0.9f * knobs.snap;
        skinNoise = 0.5f * knobs.snap; // Snap is the hand's noise: through the skin, and straight into the gate
        indexTarget = knobs.fmIndex;
        feedbackTarget = knobs.feedback;
        foldTarget = knobs.timbre;
        biasTarget = knobs.symmetry;
        modeTarget = knobs.gateMode;
        resTarget = knobs.resonance;
        openTarget = openLight (knobs.open);
        tuneTarget = knobs.tune;
        decayTau = decaySeconds (knobs.decay);
        glideCoef = onePoleCoeff (8.0f * invFs / glideSeconds (knobs.glide));
        pressCoef = onePoleCoeff (8.0f * invFs / 0.09f);
        driftDepth = 0.6f * knobs.drift * knobs.drift;
        driftEvery = std::max (1, (int) (0.12f * fs / 8.0f));
        driftCoef = onePoleCoeff (8.0f * invFs / 0.2f);
        capDrain = (float) std::exp (-8.0 / (0.3 * fs));
        historyCoef = onePoleCoeff (8.0f * invFs / 0.5f);
        const Body& b = bodies[clampBody (knobs.body)];
        for (int i = 0; i < 4; ++i)
            ratio0[i] = b.ratio[i], ratio1[i] = b.ratio[4 + i];
        lastF0 = -1.0f;
        modesDirty = true;
        gateDirty = true;
        easing = true;
    }

    // One inner sample.
    [[gnu::always_inline]] float inner()
    {
        // The LED and the light.
        float u = ledBase;
        if (strikeLeft > 0)
            u += strikeLevel, --strikeLeft;
        light += (u - light) * (u > light ? cRise : cFall);

        // The complex oscillator: the principal's phase moved by the modulator and by the gate's own output.
        float osc = 0.0f;
        if (oscOn)
        {
            phaseM += incM;
            phaseM -= std::floor (phaseM);
            phaseP += incP;
            phaseP -= std::floor (phaseP);
            const float pm = pmIndex != 0.0f ? pmIndex * sinTurns (phaseM) : 0.0f;
            osc = sinTurns (phaseP + pm + pmFeedback * last);
        }

        // The hand: the contact pulse and the burst.
        float excite = 0.0f, crack = 0.0f;
        if (exciting)
        {
            if (pulseN < pulseLen)
            {
                const float s = sinTurns ((float) pulseN * pulseStep);
                excite = pulseAmp * s * s;
                ++pulseN;
            }
            noise ^= noise << 13, noise ^= noise >> 17, noise ^= noise << 5;
            const float n = (float) (int32_t) noise * (1.0f / 2147483648.0f) * burst;
            burstLp += (n - burstLp) * burstCoef;
            burst *= burstDecay;
            // The crack: the burst through a band-pass (a trapezoidal SVF, unity at its centre).
            const float c3 = n - crackS2;
            const float c1 = crackA1 * crackS1 + crackA2 * c3;
            const float c2 = crackS2 + crackA2 * crackS1 + crackA3 * c3;
            crackS1 = 2.0f * c1 - crackS1;
            crackS2 = 2.0f * c2 - crackS2;
            crack = crackGain * crackK * c1;
            excite += skinNoise * burstLp;
            exciting = pulseN < pulseLen || burst > 2e-4f; // the burst's last 74 dB are left out
        }

        // The membrane: eight rotations, two lanes of four.
        float skin = 0.0f;
        if (skinActive)
        {
            const v4 e = splat (excite);
            const v4 u0 = mc0 * mu0 - ms0 * mv0 + mg0 * e, v0 = ms0 * mu0 + mc0 * mv0;
            const v4 u1 = mc1 * mu1 - ms1 * mv1 + mg1 * e, v1 = ms1 * mu1 + mc1 * mv1;
            mu0 = u0, mv0 = v0, mu1 = u1, mv1 = v1;
            skin = sum (mw0 * v0 + mw1 * v1);
        }

        float x = oscLevel * osc + skinLevel * skinGain * skin + snapLevel * crack;
        if (folding)
            x += foldMix * (sinTurns (foldGain * x + foldBias) - foldDc - x);
        x *= level; // after the fold, which would even out a softer hand

        // The gate: a trapezoidal SVF (Simper's form), its band-pass state held soft when resonant.
        const float v3 = x - ic2;
        const float v1 = a1 * ic1 + a2 * v3;
        const float v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.0f * v1 - ic1;
        ic2 = 2.0f * v2 - ic2;
        if (limiting)
            ic1 = 3.0f * tanhFast (ic1 * (1.0f / 3.0f));
        const float y = v2 * gain;
        last = y;
        quietOut = std::max (quietOut, std::fabs (y));
        return y;
    }

    // The membrane's level against the oscillator's, the contact's drive into it, the crack's level (a band of noise
    // has a fraction of white noise's power).
    static constexpr float skinGain = 1.6f, skinDrive = 1.0f, crackGain = 3.0f;

    VoiceKnobs knobs;
    bool dirty = true, smoothed = false, idle = true, exciting = false, skinActive = false, oscOn = true, folding = false, limiting = false;
    bool held = false, sliding = false, gateDirty = true, lastAccent = false, easing = true, modesDirty = true;
    float modeF0 = 1.0f;
    uint64_t skippedTicks = 0;
    float fs = 96000.0f, invFs = 1.0f / 96000.0f;
    // Oscillator.
    float phaseP = 0, phaseM = 0, incP = 0, incM = 0, pmIndex = 0, pmFeedback = 0, last = 0;
    // Membrane: states, rotations (r cos, r sin), decays, input gains, output weights, ratios.
    v4 mu0 {}, mu1 {}, mv0 {}, mv1 {}, mc0 {}, mc1 {}, ms0 {}, ms1 {}, mr0 {}, mr1 {}, mg0 {}, mg1 {}, mw0 {}, mw1 {};
    v4 ratio0 {}, ratio1 {}, detune0 { 1, 1, 1, 1 }, detune1 { 1, 1, 1, 1 };
    // Exciter.
    int pulseN = 0, pulseLen = 0;
    float crackK = 1, crackA1 = 1, crackA2 = 0, crackA3 = 0, crackS1 = 0, crackS2 = 0;
    float skinNoise = 0.06f, pulseAmp = 0, pulseStep = 0, burst = 0, burstDecay = 0, burstLp = 0, burstCoef = 0;
    uint32_t noise = 0x2545f491u;
    // Fold.
    float foldGain = 0, foldBias = 0, foldDc = 0, foldMix = 0;
    // Vactrol.
    float light = 0, history = 0, cap = 0, cRise = 0, cFall = 0, ledBase = 0, strikeLevel = 0, holdLevel = 0, gateDamp = 1;
    float decayTau = 0.2f, capDrain = 1, historyCoef = 0;
    int strikeLeft = 0;
    // Gate (SVF).
    float ic1 = 0, ic2 = 0, a1 = 1, a2 = 0, a3 = 0, gain = 0, gateLight = -1, gateMode = -1, gateRes = -1, quietOut = 0;
    GateCircuit circuit;
    float circuitMode = -1, circuitRes = -1;
    // Pitch.
    float note = 0, noteTarget = 0, bendSemis = 0, bendDecay = 0, pressSemis = 0, pressTarget = 0, pressCoef = 0;
    float driftValue = 0, driftTarget = 0, driftDepth = 0, driftCoef = 0, glideCoef = 0, lastF0 = -1, wheelSemis = 0,
          wheelFold = 0;
    int driftTicks = 0, driftEvery = 1;
    int64_t driftCount = 0;
    // Eased knobs and their targets.
    float oscLevel = 0, oscTarget = 0, skinLevel = 0, skinTarget = 0, snapLevel = 0, snapTarget = 0, indexKnob = 0,
          indexTarget = 0, feedbackLevel = 0, feedbackTarget = 0, foldKnob = 0, foldTarget = 0, biasKnob = 0, biasTarget = 0,
          modeKnob = 0.5f, modeTarget = 0.5f, resKnob = 0, resTarget = 0, openLevel = 0, openTarget = 0, tuneSemis = 0,
          tuneTarget = 0, level = 1, levelTarget = 1;
    float lastForce = 0;
    uint32_t tick = 0, strikes = 0;
    Decimator decimator;
};
} // namespace mystik
