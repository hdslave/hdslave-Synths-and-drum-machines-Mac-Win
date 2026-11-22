// Reverie's parameters: one table for the CLAP side (names, ranges, text) and the engine (what a value means).
// Ids are fixed forever: a saved project keeps its values by id. New parameters take new ids (append only), and the
// numbered choices only ever grow at their ends. Ids 0-9 are the chord section: a sound preset leaves them alone,
// so the progression, key and voicing stay as they are while the sound changes.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <strings.h>

#include "progressions.h"

#ifndef REVERIE_VERSION
#define REVERIE_VERSION "0.1.1"
#endif

namespace reverie
{
namespace ParamId
{
enum : uint32_t
{
    // Chords
    progression = 0, key, voicing, octave, strum, humanize, bass, latch, glide, velocity,
    // Oscillators
    osc1Wave = 10, osc1Pos, osc1Octave, osc1Semi, osc1Fine, osc1Unison, osc1Detune, osc1Level,
    osc2Wave = 18, osc2Pos, osc2Octave, osc2Semi, osc2Fine, osc2Unison, osc2Detune, osc2Level,
    // Voice
    width = 26, air, airTone, drift,
    // Filter
    filterType = 30, cutoff, resonance, filterEnv, keyTrack, velCutoff,
    // Envelopes
    ampAttack = 36, ampDecay, ampSustain, ampRelease,
    fltAttack = 40, fltDecay, fltSustain, fltRelease,
    // LFO
    lfoShape = 44, lfoRate, lfoDelay, lfoPitch, lfoCutoff, lfoWave,
    // Motion
    motionShape = 50, motionRate, motionSync, motionDiv, motionDepth, motionSpread, motionRetrig, motionX, motionY,
    xDest1, xAmt1, xDest2, xAmt2, yDest1, yAmt1, yDest2, yAmt2,
    // Effects
    ensMix = 67, ensDepth, ensRate,
    dlyMix = 70, dlyTime, dlyFeedback,
    revMix = 73, revSize, revDecay, revDamp, revPredelay, shimmer,
    volume = 79,
    count = 80
};
static_assert (osc2Wave == osc1Wave + 8 && width == 26 && motionShape == 50 && yAmt2 == 66 && count == 80, "the ids are fixed");
constexpr uint32_t chordSection = 10; // ids below it belong to the chords, not the sound
inline constexpr uint32_t osc (int o, uint32_t field) { return (o == 0 ? osc1Wave : osc2Wave) + field; }
} // namespace ParamId

enum class Kind : uint8_t { continuous, integer, choice };
// How a value reads.
enum class Unit : uint8_t { unit, bipolar, choice, onOff, count, semis, octaves, cents, envTime, strumTime, glideTime, lfoRate,
                            lfoDelay, motionRate, cutoff, gain, detune, drift, ensRate, predelay, decay, damp, amount };

struct Param
{
    uint32_t id;
    const char* name;  // the host's list
    const char* label; // the page's (at most 6 letters)
    const char* module;
    float min, max, def;
    Kind kind;
    Unit unit;
    const char* const* choices; // for Kind::choice: max - min + 1 names
};

// --- What the values mean -------------------------------------------------------------------------------------------

inline const char* const offOn[] = { "Off", "On" };
inline const char* const keyChoices[] = { "C", "Db", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" };
inline const char* const voicingChoices[] = { "Neo Soul", "Open", "Close", "Air", "Quartal", "Rootless" };
namespace Voicing { enum : int { neoSoul, open, close, air, quartal, rootless, count }; }
inline const char* const waveChoices[] = { "Velvet Saw", "Hollow Pulse", "Choir", "Strings", "Drawbar", "Crystal",
                                           "Spectra", "Octaves", "Warm", "Breath", "Reed", "Sine" };
constexpr int waveCount = 12;
inline const char* const filterChoices[] = { "LP 12", "LP 24", "BP", "HP" };
namespace Filter { enum : int { lp12, lp24, bp, hp }; }
inline const char* const lfoShapeChoices[] = { "Sine", "Triangle", "Drift" };
namespace LfoShape { enum : int { sine, triangle, drift }; }
inline const char* const motionChoices[] = { "Off", "Orbit", "Figure 8", "Pendulum", "Spiral", "Wander", "Tide", "Star", "Lissajous" };
namespace Motion { enum : int { off, orbit, figure8, pendulum, spiral, wander, tide, star, lissajous, count }; }
inline const char* const motionDivChoices[] = { "16 Bars", "8 Bars", "4 Bars", "2 Bars", "1 Bar", "1/2", "1/4" };
constexpr double motionDivBeats[] = { 64.0, 32.0, 16.0, 8.0, 4.0, 2.0, 1.0 };
// What the X / Y axes of Motion move.
inline const char* const destChoices[] = { "Off", "Wave 1", "Wave 2", "Waves", "Osc Mix", "Cutoff", "Resonance", "Detune",
                                           "Width", "Air", "Pitch", "Level", "LFO Depth" };
namespace Dest { enum : int { off, wave1, wave2, waves, oscMix, cutoff, resonance, detune, width, air, pitch, level, lfoDepth, count }; }
inline const char* const delayChoices[] = { "1/32", "1/16T", "1/16", "1/8T", "1/16D", "1/8", "1/4T", "1/8D", "1/4", "1/2T", "1/4D", "1/2" };
constexpr double delayBeats[] = { 0.125, 1.0 / 6.0, 0.25, 1.0 / 3.0, 0.375, 0.5, 2.0 / 3.0, 0.75, 1.0, 4.0 / 3.0, 1.5, 2.0 };

static_assert (sizeof (waveChoices) / sizeof (waveChoices[0]) == waveCount, "a name a wave");
static_assert (sizeof (motionChoices) / sizeof (motionChoices[0]) == Motion::count, "a name a shape");
static_assert (sizeof (destChoices) / sizeof (destChoices[0]) == Dest::count, "a name a destination");
static_assert (sizeof (motionDivBeats) / sizeof (motionDivBeats[0]) == sizeof (motionDivChoices) / sizeof (motionDivChoices[0]), "a length a division");
static_assert (sizeof (delayBeats) / sizeof (delayBeats[0]) == sizeof (delayChoices) / sizeof (delayChoices[0]), "a length a division");

// Knob curves (0..1): the page and the host show the value itself.
inline float cube (float k) { return k * k * k; }
inline float cubeRoot (float x) { return std::cbrt (std::max (x, 0.0f)); }
inline float envMs (float k) { return 20000.0f * cube (k); }            // 0 to 20 s
inline float envKnob (float ms) { return cubeRoot (ms / 20000.0f); }
inline float strumMs (float k) { return 150.0f * k * k; }               // 0 to 150 ms between notes
inline float strumKnob (float ms) { return std::sqrt (std::max (ms, 0.0f) / 150.0f); }
inline float glideMs (float k) { return 3000.0f * k * k; }              // 0 to 3 s
inline float glideKnob (float ms) { return std::sqrt (std::max (ms, 0.0f) / 3000.0f); }
inline float lfoHz (float k) { return 0.05f * std::pow (400.0f, k); }    // 0.05 to 20 Hz
inline float lfoKnob (float hz) { return std::log (std::max (hz, 1e-4f) / 0.05f) / std::log (400.0f); }
inline float lfoDelayMs (float k) { return 5000.0f * k * k; }           // 0 to 5 s
inline float lfoDelayKnob (float ms) { return std::sqrt (std::max (ms, 0.0f) / 5000.0f); }
inline float motionHz (float k) { return 0.01f * std::pow (400.0f, k); } // 0.01 to 4 Hz
inline float motionKnob (float hz) { return std::log (std::max (hz, 1e-5f) / 0.01f) / std::log (400.0f); }
inline float cutoffHz (float k) { return 20.0f * std::pow (1000.0f, k); } // 20 Hz to 20 kHz
inline float cutoffKnob (float hz) { return std::log (std::max (hz, 1e-3f) / 20.0f) / std::log (1000.0f); }
inline float detuneCents (float k) { return 50.0f * k * k; }            // the unison's outer voices, 0 to 50 cents
inline float detuneKnob (float c) { return std::sqrt (std::max (c, 0.0f) / 50.0f); }
inline float driftCents (float k) { return 20.0f * k * k; }             // 0 to 20 cents of wander
inline float driftKnob (float c) { return std::sqrt (std::max (c, 0.0f) / 20.0f); }
inline float ensHz (float k) { return 0.1f * std::pow (30.0f, k); }      // 0.1 to 3 Hz
inline float ensKnob (float hz) { return std::log (std::max (hz, 1e-4f) / 0.1f) / std::log (30.0f); }
inline float predelayMs (float k) { return 250.0f * k; }
inline float decaySeconds (float k) { return 0.4f * std::pow (100.0f, k); } // 0.4 to 40 s (to -60 dB)
inline float decayKnob (float s) { return std::log (std::max (s, 1e-4f) / 0.4f) / std::log (100.0f); }
inline float dampHz (float k) { return 1500.0f * std::pow (12.0f, k); }  // 1.5 to 18 kHz
inline float dampKnob (float hz) { return std::log (std::max (hz, 1e-3f) / 1500.0f) / std::log (12.0f); }
// Velocity's share of the level: none at 0, the whole of it at 1.
inline float velocityGain (float velocity, float amount) { return 1.0f - amount * (1.0f - velocity * velocity); }

// --- The table ------------------------------------------------------------------------------------------------------
constexpr int paramCount = (int) ParamId::count;

namespace detail
{
using K = Kind;
using U = Unit;
struct Builder
{
    Param list[paramCount] {};
    int n = 0;
    void add (uint32_t id, const char* name, const char* label, const char* module, float min, float max, float def, K kind, U unit,
              const char* const* choices = nullptr)
    {
        list[n++] = Param { id, name, label, module, min, max, def, kind, unit, choices };
    }
};
} // namespace detail

inline const Param* makeTable()
{
    using namespace detail;
    static Builder b;
    if (b.n != 0) return b.list;
    using namespace ParamId;
    b.add (progression, "Progression", "PROG", "Chords", 0, (float) (progressionCount - 1), 0, K::choice, U::choice, progressionNames);
    b.add (key, "Key", "KEY", "Chords", 0, 11, 0, K::choice, U::choice, keyChoices);
    b.add (voicing, "Voicing", "VOICE", "Chords", 0, Voicing::count - 1, Voicing::neoSoul, K::choice, U::choice, voicingChoices);
    b.add (octave, "Octave", "OCT", "Chords", -2, 2, 0, K::integer, U::octaves);
    b.add (strum, "Strum", "STRUM", "Chords", 0, 1, strumKnob (10.0f), K::continuous, U::strumTime);
    b.add (humanize, "Humanize", "HUMAN", "Chords", 0, 1, 0.15f, K::continuous, U::unit);
    b.add (bass, "Bass", "BASS", "Chords", 0, 1, 0.7f, K::continuous, U::unit);
    b.add (latch, "Latch", "LATCH", "Chords", 0, 1, 0, K::choice, U::onOff, offOn);
    b.add (glide, "Chord Glide", "GLIDE", "Chords", 0, 1, 0, K::continuous, U::glideTime);
    b.add (velocity, "Velocity", "VEL", "Chords", 0, 1, 0.5f, K::continuous, U::unit);
    for (int o = 0; o < 2; ++o)
    {
        static const char* const names[2][8] = {
            { "Osc1 Wave", "Osc1 Position", "Osc1 Octave", "Osc1 Semi", "Osc1 Fine", "Osc1 Unison", "Osc1 Detune", "Osc1 Level" },
            { "Osc2 Wave", "Osc2 Position", "Osc2 Octave", "Osc2 Semi", "Osc2 Fine", "Osc2 Unison", "Osc2 Detune", "Osc2 Level" } };
        const char* const module = o == 0 ? "Osc 1" : "Osc 2";
        const uint32_t i = o == 0 ? osc1Wave : osc2Wave;
        b.add (i + 0, names[o][0], "WAVE", module, 0, waveCount - 1, o == 0 ? 0.0f : 2.0f, K::choice, U::choice, waveChoices);
        b.add (i + 1, names[o][1], "POS", module, 0, 1, o == 0 ? 0.35f : 0.4f, K::continuous, U::unit);
        b.add (i + 2, names[o][2], "OCT", module, -2, 2, 0, K::integer, U::octaves);
        b.add (i + 3, names[o][3], "SEMI", module, -12, 12, 0, K::integer, U::semis);
        b.add (i + 4, names[o][4], "FINE", module, -50, 50, o == 0 ? 0.0f : 4.0f, K::continuous, U::cents);
        b.add (i + 5, names[o][5], "UNISON", module, 1, 4, o == 0 ? 3.0f : 2.0f, K::integer, U::count);
        b.add (i + 6, names[o][6], "DETUNE", module, 0, 1, o == 0 ? detuneKnob (14.0f) : detuneKnob (9.0f), K::continuous, U::detune);
        b.add (i + 7, names[o][7], "LEVEL", module, 0, 1, o == 0 ? 0.8f : 0.6f, K::continuous, U::unit);
    }
    b.add (width, "Width", "WIDTH", "Voice", 0, 1, 0.7f, K::continuous, U::unit);
    b.add (air, "Air", "AIR", "Voice", 0, 1, 0.2f, K::continuous, U::unit);
    b.add (airTone, "Air Tone", "A.TONE", "Voice", 0, 1, 0.6f, K::continuous, U::unit);
    b.add (drift, "Drift", "DRIFT", "Voice", 0, 1, driftKnob (4.0f), K::continuous, U::drift);
    b.add (filterType, "Filter", "FILTER", "Filter", 0, 3, Filter::lp12, K::choice, U::choice, filterChoices);
    b.add (cutoff, "Cutoff", "CUTOFF", "Filter", 0, 1, cutoffKnob (3400.0f), K::continuous, U::cutoff);
    b.add (resonance, "Resonance", "RESO", "Filter", 0, 1, 0.15f, K::continuous, U::unit);
    b.add (filterEnv, "Filter Env", "ENV", "Filter", -1, 1, 0.25f, K::continuous, U::bipolar);
    b.add (keyTrack, "Key Track", "KEYTRK", "Filter", 0, 1, 0.35f, K::continuous, U::unit);
    b.add (velCutoff, "Vel > Cutoff", "VEL", "Filter", 0, 1, 0.3f, K::continuous, U::unit);
    b.add (ampAttack, "Attack", "ATTACK", "Amp Env", 0, 1, envKnob (700), K::continuous, U::envTime);
    b.add (ampDecay, "Decay", "DECAY", "Amp Env", 0, 1, envKnob (1800), K::continuous, U::envTime);
    b.add (ampSustain, "Sustain", "SUST", "Amp Env", 0, 1, 0.85f, K::continuous, U::unit);
    b.add (ampRelease, "Release", "REL", "Amp Env", 0, 1, envKnob (2600), K::continuous, U::envTime);
    b.add (fltAttack, "Flt Attack", "ATTACK", "Filter Env", 0, 1, envKnob (1300), K::continuous, U::envTime);
    b.add (fltDecay, "Flt Decay", "DECAY", "Filter Env", 0, 1, envKnob (3300), K::continuous, U::envTime);
    b.add (fltSustain, "Flt Sustain", "SUST", "Filter Env", 0, 1, 0.4f, K::continuous, U::unit);
    b.add (fltRelease, "Flt Release", "REL", "Filter Env", 0, 1, envKnob (2600), K::continuous, U::envTime);
    b.add (lfoShape, "LFO Shape", "SHAPE", "LFO", 0, 2, LfoShape::sine, K::choice, U::choice, lfoShapeChoices);
    b.add (lfoRate, "LFO Rate", "RATE", "LFO", 0, 1, lfoKnob (0.4f), K::continuous, U::lfoRate);
    b.add (lfoDelay, "LFO Delay", "DELAY", "LFO", 0, 1, lfoDelayKnob (450.0f), K::continuous, U::lfoDelay);
    b.add (lfoPitch, "LFO > Pitch", "PITCH", "LFO", 0, 1, 0.05f, K::continuous, U::unit);
    b.add (lfoCutoff, "LFO > Cutoff", "CUTOFF", "LFO", -1, 1, 0.1f, K::continuous, U::bipolar);
    b.add (lfoWave, "LFO > Wave", "WAVE", "LFO", -1, 1, 0.15f, K::continuous, U::bipolar);
    b.add (motionShape, "Motion", "PATH", "Motion", 0, Motion::count - 1, Motion::orbit, K::choice, U::choice, motionChoices);
    b.add (motionRate, "Motion Rate", "RATE", "Motion", 0, 1, motionKnob (0.08f), K::continuous, U::motionRate);
    b.add (motionSync, "Motion Sync", "SYNC", "Motion", 0, 1, 0, K::choice, U::onOff, offOn);
    b.add (motionDiv, "Motion Div", "DIV", "Motion", 0, 6, 2, K::choice, U::choice, motionDivChoices);
    b.add (motionDepth, "Motion Depth", "DEPTH", "Motion", 0, 1, 0.5f, K::continuous, U::unit);
    b.add (motionSpread, "Motion Spread", "SPREAD", "Motion", 0, 1, 0.5f, K::continuous, U::unit);
    b.add (motionRetrig, "Motion Retrig", "RETRIG", "Motion", 0, 1, 0, K::choice, U::onOff, offOn);
    b.add (motionX, "Motion X", "X", "Motion", -1, 1, 0, K::continuous, U::bipolar);
    b.add (motionY, "Motion Y", "Y", "Motion", -1, 1, 0, K::continuous, U::bipolar);
    b.add (xDest1, "X Dest 1", "X>1", "Motion", 0, Dest::count - 1, Dest::waves, K::choice, U::choice, destChoices);
    b.add (xAmt1, "X Amount 1", "X AMT1", "Motion", -1, 1, 0.5f, K::continuous, U::amount);
    b.add (xDest2, "X Dest 2", "X>2", "Motion", 0, Dest::count - 1, Dest::oscMix, K::choice, U::choice, destChoices);
    b.add (xAmt2, "X Amount 2", "X AMT2", "Motion", -1, 1, 0.3f, K::continuous, U::amount);
    b.add (yDest1, "Y Dest 1", "Y>1", "Motion", 0, Dest::count - 1, Dest::cutoff, K::choice, U::choice, destChoices);
    b.add (yAmt1, "Y Amount 1", "Y AMT1", "Motion", -1, 1, 0.35f, K::continuous, U::amount);
    b.add (yDest2, "Y Dest 2", "Y>2", "Motion", 0, Dest::count - 1, Dest::detune, K::choice, U::choice, destChoices);
    b.add (yAmt2, "Y Amount 2", "Y AMT2", "Motion", -1, 1, 0.3f, K::continuous, U::amount);
    b.add (ensMix, "Ensemble", "ENSEM", "Ensemble", 0, 1, 0.4f, K::continuous, U::unit);
    b.add (ensDepth, "Ens Depth", "DEPTH", "Ensemble", 0, 1, 0.5f, K::continuous, U::unit);
    b.add (ensRate, "Ens Rate", "RATE", "Ensemble", 0, 1, ensKnob (0.35f), K::continuous, U::ensRate);
    b.add (dlyMix, "Delay", "DELAY", "Delay", 0, 1, 0.12f, K::continuous, U::unit);
    b.add (dlyTime, "Delay Time", "TIME", "Delay", 0, 11, 10, K::choice, U::choice, delayChoices);
    b.add (dlyFeedback, "Delay Feedback", "FDBK", "Delay", 0, 1, 0.35f, K::continuous, U::unit);
    b.add (revMix, "Reverb", "REVERB", "Reverb", 0, 1, 0.38f, K::continuous, U::unit);
    b.add (revSize, "Rev Size", "SIZE", "Reverb", 0, 1, 0.7f, K::continuous, U::unit);
    b.add (revDecay, "Rev Decay", "DECAY", "Reverb", 0, 1, decayKnob (5.0f), K::continuous, U::decay);
    b.add (revDamp, "Rev Tone", "TONE", "Reverb", 0, 1, dampKnob (6500.0f), K::continuous, U::damp);
    b.add (revPredelay, "Pre-Delay", "PREDLY", "Reverb", 0, 1, 0.08f, K::continuous, U::predelay);
    b.add (shimmer, "Shimmer", "SHIMR", "Reverb", 0, 1, 0.15f, K::continuous, U::unit);
    b.add (volume, "Volume", "VOLUME", "Master", 0, 2, 0.7f, K::continuous, U::gain);
    return b.list;
}

inline const Param* const paramTable = makeTable();

// Ids are the table's indices (checked by the tests).
inline int paramIndex (uint32_t id) { return id < (uint32_t) paramCount ? (int) id : -1; }

inline float clampParam (const Param& p, float v)
{
    if (! std::isfinite (v)) v = p.def;
    v = std::clamp (v, p.min, p.max);
    if (p.kind != Kind::continuous) v = std::round (v);
    return v;
}

// --- Text -----------------------------------------------------------------------------------------------------------
inline void msText (float ms, char* text, uint32_t size)
{
    if (ms < 9.95f) std::snprintf (text, size, "%.1f ms", ms);
    else if (ms < 999.5f) std::snprintf (text, size, "%.0f ms", ms);
    else std::snprintf (text, size, "%.2f s", ms / 1000.0f);
}

inline void hzText (float hz, char* text, uint32_t size)
{
    if (hz < 0.995f) std::snprintf (text, size, "%.3f Hz", hz);
    else if (hz < 9.995f) std::snprintf (text, size, "%.2f Hz", hz);
    else if (hz < 999.5f) std::snprintf (text, size, "%.0f Hz", hz);
    else std::snprintf (text, size, "%.1f kHz", hz / 1000.0f);
}

inline void formatParam (const Param& p, float v, char* text, uint32_t size)
{
    if (size == 0) return;
    v = clampParam (p, v);
    switch (p.unit)
    {
        case Unit::unit: std::snprintf (text, size, "%.0f%%", v * 100.0f); break;
        case Unit::bipolar: case Unit::amount:
        {
            const float pc = v * 100.0f;
            std::snprintf (text, size, std::fabs (pc) < 0.5f ? "0%%" : "%+.0f%%", pc);
            break;
        }
        case Unit::choice: case Unit::onOff: std::snprintf (text, size, "%s", p.choices[(int) v - (int) p.min]); break;
        case Unit::count: std::snprintf (text, size, "%d", (int) v); break;
        case Unit::semis: std::snprintf (text, size, "%+d st", (int) v); break;
        case Unit::octaves: std::snprintf (text, size, "%+d oct", (int) v); break;
        case Unit::cents: std::snprintf (text, size, "%+.1f ct", v); break;
        case Unit::envTime: msText (envMs (v), text, size); break;
        case Unit::strumTime: msText (strumMs (v), text, size); break;
        case Unit::glideTime: if (v <= 0.0f) std::snprintf (text, size, "Off"); else msText (glideMs (v), text, size); break;
        case Unit::lfoRate: hzText (lfoHz (v), text, size); break;
        case Unit::lfoDelay: msText (lfoDelayMs (v), text, size); break;
        case Unit::motionRate: hzText (motionHz (v), text, size); break;
        case Unit::cutoff: hzText (cutoffHz (v), text, size); break;
        case Unit::gain:
            if (v <= 0.0001f) std::snprintf (text, size, "-inf dB");
            else std::snprintf (text, size, "%+.1f dB", 20.0f * std::log10 (v));
            break;
        case Unit::detune: std::snprintf (text, size, detuneCents (v) < 9.95f ? "%.1f ct" : "%.0f ct", detuneCents (v)); break;
        case Unit::drift: std::snprintf (text, size, "%.1f ct", driftCents (v)); break;
        case Unit::ensRate: hzText (ensHz (v), text, size); break;
        case Unit::predelay: msText (predelayMs (v), text, size); break;
        case Unit::decay: std::snprintf (text, size, decaySeconds (v) < 9.95f ? "%.1f s" : "%.0f s", decaySeconds (v)); break;
        case Unit::damp: hzText (dampHz (v), text, size); break;
    }
}

// Reads text as formatParam writes it (units optional); a choice by its name or its number. Text that is no number
// (nan, inf) is refused.
inline bool parseParam (const Param& p, const char* text, float& v)
{
    while (*text == ' ') ++text;
    if (p.kind == Kind::choice)
    {
        const int n = (int) (p.max - p.min) + 1;
        size_t len = std::strlen (text);
        while (len > 0 && text[len - 1] == ' ') --len;
        for (int i = 0; i < n; ++i)
            if (std::strlen (p.choices[i]) == len && strncasecmp (text, p.choices[i], len) == 0)
                return v = p.min + (float) i, true;
    }
    if (p.unit == Unit::gain && strncasecmp (text, "-inf", 4) == 0) return v = 0.0f, true;
    if (p.unit == Unit::glideTime && strncasecmp (text, "off", 3) == 0) return v = 0.0f, true;
    char* end = nullptr;
    const double x = std::strtod (text, &end);
    if (end == text || ! std::isfinite (x)) return false;
    while (*end == ' ') ++end;
    const bool seconds = (end[0] == 's' || end[0] == 'S') && end[1] != 't';
    const bool kilo = end[0] == 'k' || end[0] == 'K';
    const double ms = seconds ? x * 1000.0 : x;
    const double hz = kilo ? x * 1000.0 : x;
    switch (p.unit)
    {
        case Unit::unit: case Unit::bipolar: case Unit::amount: v = (float) (x / 100.0); break;
        case Unit::envTime: v = envKnob ((float) ms); break;
        case Unit::strumTime: v = strumKnob ((float) ms); break;
        case Unit::glideTime: v = glideKnob ((float) ms); break;
        case Unit::lfoRate: v = lfoKnob ((float) hz); break;
        case Unit::lfoDelay: v = lfoDelayKnob ((float) ms); break;
        case Unit::motionRate: v = motionKnob ((float) hz); break;
        case Unit::cutoff: v = cutoffKnob ((float) hz); break;
        case Unit::gain: v = (float) std::pow (10.0, x / 20.0); break;
        case Unit::detune: v = detuneKnob ((float) x); break;
        case Unit::drift: v = driftKnob ((float) x); break;
        case Unit::ensRate: v = ensKnob ((float) hz); break;
        case Unit::predelay: v = (float) (ms / 250.0); break;
        case Unit::decay: v = decayKnob ((float) (seconds || end[0] == 0 ? x : x / 1000.0)); break;
        case Unit::damp: v = dampKnob ((float) hz); break;
        default: v = (float) x; break;
    }
    if (! std::isfinite (v)) return false;
    v = clampParam (p, v);
    return true;
}
} // namespace reverie
