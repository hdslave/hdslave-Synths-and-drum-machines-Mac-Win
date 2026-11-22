// Nexus's parameters: one table for the CLAP side (names, ranges, text) and the engine (what a value means).
// Ids are fixed forever: a saved project keeps its values by id. New parameters take new ids (append only), and the
// numbered choices only ever grow at their ends. The ids follow the order of the controls of the plug-in Nexus was
// modelled on (MODEL first, the assign matrix last).
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <strings.h>

#ifndef NEXUS_VERSION
#define NEXUS_VERSION "0.1.1"
#endif

namespace nexus
{
constexpr int routeSources = 4, routeTargets = 4, assignSlots = 4;

namespace ParamId
{
enum : uint32_t
{
    model = 0, frequency, harmonics, timbre, morph, timbreAtt, fmAtt, morphAtt, auxMix, ampMode, lpgColor, lpgDecay,
    trigRate, voiceMode, notes, unison, detune, spread, glide, bendRange, velCurve, atCurve, filterMode, cutoff, resonance,
    volume, pan,
    env1Attack, env1Decay, env1Sustain, env1Release, env1Retrig,
    env2Attack, env2Decay, env2Sustain, env2Release, env2Retrig,
    lfo1Shape, lfo1Rate, lfo1Sync, lfo1Div, lfo1Retrig, lfo1PerVoice, lfo1Phase,
    lfo2Shape, lfo2Rate, lfo2Sync, lfo2Div, lfo2Retrig, lfo2PerVoice, lfo2Phase,
    cycRise, cycFall, cycShape, cycBipolar, cycRetrig,
    rndMode, rndRate, rndSync, rndDiv, rndSlew, rndRetrig,
    route0, // the fixed routes: source s (LFO 1, Env 2, Cycle, Random) to target t (Pitch, Harmonics, Timbre, Morph),
            // amount at route0 + 2 (4 s + t), its switch at the next id
    assignSource0 = route0 + 2 * routeSources * routeTargets, // 4 sources
    assignTarget0 = assignSource0 + assignSlots,               // 4 targets
    assign0 = assignTarget0 + assignSlots, // source i to target j: amount at assign0 + 2 (4 i + j), its switch next
    count = assign0 + 2 * assignSlots * assignSlots
};
static_assert (route0 == 62 && assignSource0 == 94 && assignTarget0 == 98 && assign0 == 102 && count == 134, "the ids are fixed");
inline constexpr uint32_t route (int source, int target) { return route0 + 2u * (uint32_t) (4 * source + target); }
inline constexpr uint32_t assign (int source, int target) { return assign0 + 2u * (uint32_t) (4 * source + target); }
} // namespace ParamId

enum class Kind : uint8_t { continuous, integer, choice };
// How a value reads.
enum class Unit : uint8_t { unit, bipolar, semis, choice, onOff, count, semisInt, curve, cutoff, gain, pan, envTime, glideTime,
                            cycleTime, rate, phase, amount };

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

// The 24 models, in the module's order: the green bank of firmware 1.2, then the two original banks.
inline const char* const modelChoices[] = { "VA VCF", "Phase Dist", "6-Op FM I", "6-Op FM II", "6-Op FM III", "Wave Terrain",
                                            "String Mach", "Chiptune", "Virtual Analog", "Waveshaping", "FM 2-Op", "Grain Formant",
                                            "Harmonic", "Wavetable", "Chords", "Speech", "Swarm", "Noise", "Particle",
                                            "Inharm String", "Modal Reso", "Bass Drum", "Snare Drum", "Hi-Hat" };
constexpr int modelCount = 24;
// What HARMONICS, TIMBRE and MORPH do in each model (the module's manual), for the page.
struct ModelRole { const char* harmonics; const char* timbre; const char* morph; const char* aux; };
inline constexpr ModelRole modelRoles[modelCount] = {
    { "Resonance", "Cutoff", "Shape", "LP / HP" },        { "Distortion", "PD amount", "Modulation", "Synced" },
    { "Preset", "Modulation", "Env stretch", "Unused" },  { "Preset", "Modulation", "Env stretch", "Unused" },
    { "Preset", "Modulation", "Env stretch", "Unused" },  { "Terrain", "Path radius", "Path offset", "Terrain z" },
    { "Chord", "Chorus/filt", "Waveform", "Odd voices" }, { "Chord", "Arp/chord", "Pulse width", "NES triangle" },
    { "Detune", "Square shape", "Saw shape", "Sync sum" }, { "Waveform", "Fold amount", "Asymmetry", "Variant" },
    { "Ratio", "FM index", "Feedback", "Sub osc" },       { "Formant ratio", "Formant freq", "Formant width", "Variant" },
    { "Bumps", "Prominent", "Bump shape", "Organ" },      { "Bank", "Row", "Column", "Lo-fi" },
    { "Chord", "Inversion", "Waveform", "Root" },         { "Mode", "Species", "Phoneme", "Unfiltered" },
    { "Pitch spread", "Grain rate", "Grain shape", "Sine" }, { "Bandwidth", "Clock", "Resonance", "Dual BP" },
    { "Randomness", "Density", "Filter", "Raw dust" },    { "Inharmonic", "Excitation", "Decay", "Raw exciter" },
    { "Inharmonic", "Excitation", "Decay", "Raw exciter" }, { "Attack/punch", "Brightness", "Decay", "808 style" },
    { "Snappy", "Body/noise", "Decay", "808 style" },     { "Metallic", "HP cutoff", "Decay", "808 style" },
};

inline const char* const offOn[] = { "Off", "On" };
inline const char* const ampChoices[] = { "Gate", "Ping", "Env", "Drone" };
namespace Amp { enum : int { gate, ping, env, drone }; }
inline const char* const trigRateChoices[] = { "Off", "1/4", "1/4T", "1/8", "1/8T", "1/16", "1/16T", "1/32" };
constexpr double trigRateBeats[] = { 0.0, 1.0, 2.0 / 3.0, 0.5, 1.0 / 3.0, 0.25, 1.0 / 6.0, 0.125 };
inline const char* const voiceModeChoices[] = { "Mono", "Poly", "Legato" };
namespace VoiceMode { enum : int { mono, poly, legato }; }
inline const char* const filterChoices[] = { "LP", "BP", "HP" };
inline const char* const lfoShapeChoices[] = { "Sine", "Triangle", "Saw", "Square", "Random", "Smooth" };
namespace LfoShape { enum : int { sine, triangle, saw, square, random, smooth }; }
inline const char* const divChoices[] = { "4 Bars", "2 Bars", "1 Bar", "1/2", "1/4", "1/4T", "1/8", "1/8T", "1/16", "1/16T", "1/32" };
constexpr double divBeats[] = { 16.0, 8.0, 4.0, 2.0, 1.0, 2.0 / 3.0, 0.5, 1.0 / 3.0, 0.25, 1.0 / 6.0, 0.125 };
inline const char* const cycShapeChoices[] = { "Linear", "Exp", "Log" };
inline const char* const rndModeChoices[] = { "S&H", "Smooth", "Drift" };
namespace RndMode { enum : int { sampleHold, smooth, drift }; }
// The modulation sources and targets of the assign matrix.
inline const char* const sourceChoices[] = { "Off", "LFO 1", "LFO 2", "Env 1", "Env 2", "Cycle", "Random", "Velocity",
                                             "Aftertouch", "Mod Wheel" };
namespace Source { enum : int { off, lfo1, lfo2, env1, env2, cycle, random, velocity, aftertouch, wheel, count }; }
inline const char* const targetChoices[] = { "Off", "Pitch", "Harmonics", "Timbre", "Morph", "FM", "LPG Decay", "LPG Color",
                                             "Cutoff", "Resonance", "Volume", "Pan", "Aux Mix" };
namespace Target { enum : int { off, pitch, harmonics, timbre, morph, fm, lpgDecay, lpgColor, cutoff, resonance, volume, pan, aux, count }; }
// The fixed routes' rows and columns.
inline const char* const routeSourceNames[] = { "LFO 1", "Env 2", "Cycle", "Random" };
inline const char* const routeTargetNames[] = { "Pitch", "Harm", "Timbre", "Morph" };
constexpr int routeSourceOf[routeSources] = { Source::lfo1, Source::env2, Source::cycle, Source::random };
constexpr int routeTargetOf[routeTargets] = { Target::pitch, Target::harmonics, Target::timbre, Target::morph };

static_assert (sizeof (modelChoices) / sizeof (modelChoices[0]) == modelCount, "a name a model");
static_assert (sizeof (trigRateBeats) / sizeof (trigRateBeats[0]) == sizeof (trigRateChoices) / sizeof (trigRateChoices[0]), "a length a rate");
static_assert (sizeof (divBeats) / sizeof (divBeats[0]) == sizeof (divChoices) / sizeof (divChoices[0]), "a length a division");

// Times and rates are knobs (0..1) on a cubic curve, so that short times have room: the page and the host show the
// time itself.
inline float cube (float k) { return k * k * k; }
inline float cubeRoot (float x) { return std::cbrt (std::max (x, 0.0f)); }
inline float envMs (float k) { return 5000.0f * cube (k); }  // 0 to 5 s
inline float glideMs (float k) { return 2000.0f * cube (k); } // 0 to 2 s
inline float cycleMs (float k) { return 1.0f + 4999.0f * cube (k); } // 1 ms to 5 s
inline float rateHz (float k) { return 40.0f * cube (k); }   // 0 to 40 Hz
inline float envKnob (float ms) { return cubeRoot (ms / 5000.0f); }
inline float glideKnob (float ms) { return cubeRoot (ms / 2000.0f); }
inline float cycleKnob (float ms) { return cubeRoot ((ms - 1.0f) / 4999.0f); }
inline float rateKnob (float hz) { return cubeRoot (hz / 40.0f); }
// Cutoff: 20 Hz to 20 kHz. Resonance: Q 0.5 (none) to 20, linearly (as measured on the plug-in Nexus follows).
inline float cutoffHz (float k) { return 20.0f * std::pow (1000.0f, k); }
inline float resonanceQ (float k) { return 0.5f + 19.5f * k; }
// How deep a modulation amount reaches: the module's attenuverter curve (fine near zero, full at the ends).
inline float depth (float amount) { return amount * std::max (std::fabs (amount) - 0.05f, 0.05f) * 1.05f; }
// A modulation of full depth moves pitch by two octaves.
constexpr float pitchRange = 24.0f;

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
    constexpr void add (uint32_t id, const char* name, const char* label, const char* module, float min, float max, float def, K kind, U unit,
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
    b.add (model, "Model", "MODEL", "Plaits", 0, 23, 8, K::choice, U::choice, modelChoices);
    b.add (frequency, "Frequency", "FREQ", "Plaits", -48, 48, 0, K::continuous, U::semis);
    b.add (harmonics, "Harmonics", "HARM", "Plaits", 0, 1, 0.5f, K::continuous, U::unit);
    b.add (timbre, "Timbre", "TIMBRE", "Plaits", 0, 1, 0.5f, K::continuous, U::unit);
    b.add (morph, "Morph", "MORPH", "Plaits", 0, 1, 0.5f, K::continuous, U::unit);
    b.add (timbreAtt, "Timbre Att", "T.ATT", "Plaits", -1, 1, 0, K::continuous, U::bipolar);
    b.add (fmAtt, "FM Att", "FM ATT", "Plaits", -1, 1, 0, K::continuous, U::bipolar);
    b.add (morphAtt, "Morph Att", "M.ATT", "Plaits", -1, 1, 0, K::continuous, U::bipolar);
    b.add (auxMix, "Out/Aux", "AUX", "Plaits", 0, 1, 0, K::continuous, U::unit);
    b.add (ampMode, "Amp", "AMP", "Plaits", 0, 3, 0, K::choice, U::choice, ampChoices);
    b.add (lpgColor, "LPG Color", "COLOR", "Plaits", 0, 1, 0.55f, K::continuous, U::unit);
    b.add (lpgDecay, "LPG Decay", "DECAY", "Plaits", 0, 1, 0.35f, K::continuous, U::unit);
    b.add (trigRate, "Trig Rate", "RETRIG", "Env", 0, 7, 0, K::choice, U::choice, trigRateChoices);
    b.add (voiceMode, "Voice Mode", "MODE", "Voice", 0, 2, 1, K::choice, U::choice, voiceModeChoices);
    b.add (notes, "Notes", "NOTES", "Voice", 1, 8, 4, K::integer, U::count);
    b.add (unison, "Unison", "UNISON", "Voice", 1, 8, 1, K::integer, U::count);
    b.add (detune, "Detune", "DETUNE", "Voice", 0, 1, 0.1f, K::continuous, U::unit);
    b.add (spread, "Spread", "SPREAD", "Voice", 0, 1, 0.25f, K::continuous, U::unit);
    b.add (glide, "Glide", "GLIDE", "Voice", 0, 1, 0, K::continuous, U::glideTime);
    b.add (bendRange, "Bend Range", "BEND", "Voice", 0, 24, 2, K::integer, U::semisInt);
    b.add (velCurve, "Vel Curve", "VEL", "Voice", 0.1f, 4.0f, 1.0f, K::continuous, U::curve);
    b.add (atCurve, "AT Curve", "AT", "Voice", -1, 1, 0, K::continuous, U::bipolar);
    b.add (filterMode, "Filter", "FILTER", "Voice", 0, 2, 0, K::choice, U::choice, filterChoices);
    b.add (cutoff, "Cutoff", "CUTOFF", "Voice", 0, 1, 1, K::continuous, U::cutoff);
    b.add (resonance, "Resonance", "RESO", "Voice", 0, 1, 0, K::continuous, U::unit);
    b.add (volume, "Volume", "VOLUME", "Env", 0, 2, 1, K::continuous, U::gain);
    b.add (pan, "Pan", "PAN", "Voice", -1, 1, 0, K::continuous, U::pan);
    b.add (env1Attack, "Env1 Attack", "ATTACK", "Env 1", 0, 1, envKnob (5), K::continuous, U::envTime);
    b.add (env1Decay, "Env1 Decay", "DECAY", "Env 1", 0, 1, envKnob (200), K::continuous, U::envTime);
    b.add (env1Sustain, "Env1 Sustain", "SUST", "Env 1", 0, 1, 0.7f, K::continuous, U::unit);
    b.add (env1Release, "Env1 Release", "REL", "Env 1", 0, 1, envKnob (300), K::continuous, U::envTime);
    b.add (env1Retrig, "Env1 Retrig", "RETRIG", "Env 1", 0, 1, 1, K::choice, U::onOff, offOn);
    b.add (env2Attack, "Env2 Attack", "ATTACK", "Env 2", 0, 1, envKnob (5), K::continuous, U::envTime);
    b.add (env2Decay, "Env2 Decay", "DECAY", "Env 2", 0, 1, envKnob (200), K::continuous, U::envTime);
    b.add (env2Sustain, "Env2 Sustain", "SUST", "Env 2", 0, 1, 0, K::continuous, U::unit);
    b.add (env2Release, "Env2 Release", "REL", "Env 2", 0, 1, envKnob (300), K::continuous, U::envTime);
    b.add (env2Retrig, "Env2 Retrig", "RETRIG", "Env 2", 0, 1, 1, K::choice, U::onOff, offOn);
    for (int l = 0; l < 2; ++l)
    {
        const uint32_t o = l == 0 ? lfo1Shape : lfo2Shape;
        const char* const module = l == 0 ? "LFO 1" : "LFO 2";
        b.add (o + 0, l == 0 ? "LFO1 Shape" : "LFO2 Shape", "SHAPE", module, 0, 5, 0, K::choice, U::choice, lfoShapeChoices);
        b.add (o + 1, l == 0 ? "LFO1 Rate" : "LFO2 Rate", "RATE", module, 0, 1, rateKnob (2), K::continuous, U::rate);
        b.add (o + 2, l == 0 ? "LFO1 Sync" : "LFO2 Sync", "SYNC", module, 0, 1, 0, K::choice, U::onOff, offOn);
        b.add (o + 3, l == 0 ? "LFO1 Div" : "LFO2 Div", "DIV", module, 0, 10, 6, K::choice, U::choice, divChoices);
        b.add (o + 4, l == 0 ? "LFO1 Retrig" : "LFO2 Retrig", "RETRIG", module, 0, 1, 0, K::choice, U::onOff, offOn);
        b.add (o + 5, l == 0 ? "LFO1 Per Voice" : "LFO2 Per Voice", "VOICE", module, 0, 1, 0, K::choice, U::onOff, offOn);
        b.add (o + 6, l == 0 ? "LFO1 Phase" : "LFO2 Phase", "PHASE", module, 0, 1, 0, K::continuous, U::phase);
    }
    b.add (cycRise, "Cycle Rise", "RISE", "Cycle", 0, 1, cycleKnob (300), K::continuous, U::cycleTime);
    b.add (cycFall, "Cycle Fall", "FALL", "Cycle", 0, 1, cycleKnob (300), K::continuous, U::cycleTime);
    b.add (cycShape, "Cycle Shape", "SHAPE", "Cycle", 0, 2, 0, K::choice, U::choice, cycShapeChoices);
    b.add (cycBipolar, "Cycle Bipolar", "BIPOL", "Cycle", 0, 1, 1, K::choice, U::onOff, offOn);
    b.add (cycRetrig, "Cycle Retrig", "RETRIG", "Cycle", 0, 1, 0, K::choice, U::onOff, offOn);
    b.add (rndMode, "Random Mode", "MODE", "Random", 0, 2, 0, K::choice, U::choice, rndModeChoices);
    b.add (rndRate, "Random Rate", "RATE", "Random", 0, 1, rateKnob (3), K::continuous, U::rate);
    b.add (rndSync, "Random Sync", "SYNC", "Random", 0, 1, 0, K::choice, U::onOff, offOn);
    b.add (rndDiv, "Random Div", "DIV", "Random", 0, 10, 8, K::choice, U::choice, divChoices);
    b.add (rndSlew, "Random Slew", "SLEW", "Random", 0, 1, 0.3f, K::continuous, U::unit);
    b.add (rndRetrig, "Random Retrig", "RETRIG", "Random", 0, 1, 1, K::choice, U::onOff, offOn);
    {
        static char names[routeSources * routeTargets][2][32];
        static const char* const rowShort[] = { "LFO1", "ENV2", "CYC", "RND" };
        static const char* const colShort[] = { "PITCH", "HARM", "TIMBRE", "MORPH" };
        for (int s = 0; s < routeSources; ++s)
            for (int t = 0; t < routeTargets; ++t)
            {
                char(&n)[2][32] = names[4 * s + t];
                std::snprintf (n[0], 32, "%s > %s", rowShort[s], colShort[t]);
                std::snprintf (n[1], 32, "%s > %s On", rowShort[s], colShort[t]);
                b.add (route (s, t), n[0], colShort[t], "Mod", -1, 1, 0, K::continuous, U::amount);
                b.add (route (s, t) + 1, n[1], "ON", "Mod", 0, 1, 1, K::choice, U::onOff, offOn);
            }
    }
    static const char* const sourceNames[] = { "Source 1", "Source 2", "Source 3", "Source 4" };
    static const char* const targetNames[] = { "Dest 1", "Dest 2", "Dest 3", "Dest 4" };
    static const char* const sourceLabels[] = { "SRC 1", "SRC 2", "SRC 3", "SRC 4" };
    static const char* const targetLabels[] = { "DST 1", "DST 2", "DST 3", "DST 4" };
    const float sourceDefaults[] = { Source::lfo2, Source::velocity, Source::aftertouch, Source::wheel };
    const float targetDefaults[] = { Target::fm, Target::lpgDecay, Target::cutoff, Target::volume };
    for (int i = 0; i < assignSlots; ++i)
        b.add (assignSource0 + (uint32_t) i, sourceNames[i], sourceLabels[i], "Assign", 0, Source::count - 1, sourceDefaults[i], K::choice,
               U::choice, sourceChoices);
    for (int i = 0; i < assignSlots; ++i)
        b.add (assignTarget0 + (uint32_t) i, targetNames[i], targetLabels[i], "Assign", 0, Target::count - 1, targetDefaults[i], K::choice,
               U::choice, targetChoices);
    {
        static char names[assignSlots * assignSlots][2][32];
        for (int s = 0; s < assignSlots; ++s)
            for (int t = 0; t < assignSlots; ++t)
            {
                char(&n)[2][32] = names[4 * s + t];
                std::snprintf (n[0], 32, "S%d > D%d", s + 1, t + 1);
                std::snprintf (n[1], 32, "S%d > D%d On", s + 1, t + 1);
                b.add (assign (s, t), n[0], "AMT", "Assign", -1, 1, 0, K::continuous, U::amount);
                b.add (assign (s, t) + 1, n[1], "ON", "Assign", 0, 1, 1, K::choice, U::onOff, offOn);
            }
    }
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
        case Unit::semis:
            if (std::fabs (v - std::round (v)) < 0.005f) std::snprintf (text, size, "%+d st", (int) std::lround (v));
            else std::snprintf (text, size, "%+.2f st", v);
            break;
        case Unit::semisInt: std::snprintf (text, size, "%d st", (int) v); break;
        case Unit::choice: case Unit::onOff: std::snprintf (text, size, "%s", p.choices[(int) v - (int) p.min]); break;
        case Unit::count: std::snprintf (text, size, "%d", (int) v); break;
        case Unit::curve: std::snprintf (text, size, "%.2f", v); break;
        case Unit::cutoff:
        {
            const float hz = cutoffHz (v);
            if (hz < 999.5f) std::snprintf (text, size, "%.0f Hz", hz);
            else std::snprintf (text, size, "%.1f kHz", hz / 1000.0f);
            break;
        }
        case Unit::gain:
            if (v <= 0.0001f) std::snprintf (text, size, "-inf dB");
            else std::snprintf (text, size, "%+.1f dB", 20.0f * std::log10 (v));
            break;
        case Unit::pan:
        {
            const int pc = (int) std::lround (v * 100.0f);
            if (pc == 0) std::snprintf (text, size, "C");
            else std::snprintf (text, size, "%c%d", pc < 0 ? 'L' : 'R', std::abs (pc));
            break;
        }
        case Unit::envTime: msText (envMs (v), text, size); break;
        case Unit::glideTime: msText (glideMs (v), text, size); break;
        case Unit::cycleTime: msText (cycleMs (v), text, size); break;
        case Unit::rate:
        {
            const float hz = rateHz (v);
            std::snprintf (text, size, hz < 9.995f ? "%.2f Hz" : "%.1f Hz", hz);
            break;
        }
        case Unit::phase: std::snprintf (text, size, "%.0f deg", v * 360.0f); break;
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
    if (p.unit == Unit::pan)
    {
        if ((text[0] == 'C' || text[0] == 'c') && text[1] == 0) return v = 0.0f, true;
        if (text[0] == 'L' || text[0] == 'l' || text[0] == 'R' || text[0] == 'r')
        {
            const double x = std::strtod (text + 1, nullptr);
            if (! std::isfinite (x)) return false;
            v = clampParam (p, (float) ((text[0] == 'L' || text[0] == 'l' ? -x : x) / 100.0));
            return true;
        }
    }
    if (p.unit == Unit::gain && strncasecmp (text, "-inf", 4) == 0) return v = 0.0f, true;
    char* end = nullptr;
    const double x = std::strtod (text, &end);
    if (end == text || ! std::isfinite (x)) return false;
    while (*end == ' ') ++end;
    const bool seconds = end[0] == 's' || end[0] == 'S';
    const bool kilo = end[0] == 'k' || end[0] == 'K';
    const double ms = seconds ? x * 1000.0 : x;
    switch (p.unit)
    {
        case Unit::unit: case Unit::bipolar: case Unit::amount: v = (float) (x / 100.0); break;
        case Unit::cutoff: v = (float) (std::log (std::max (kilo ? x * 1000.0 : x, 1e-3) / 20.0) / std::log (1000.0)); break;
        case Unit::gain: v = (float) std::pow (10.0, x / 20.0); break;
        case Unit::envTime: v = envKnob ((float) ms); break;
        case Unit::glideTime: v = glideKnob ((float) ms); break;
        case Unit::cycleTime: v = cycleKnob ((float) ms); break;
        case Unit::rate: v = rateKnob ((float) x); break;
        case Unit::phase: v = (float) (x / 360.0); break;
        default: v = (float) x; break;
    }
    if (! std::isfinite (v)) return false;
    v = clampParam (p, v);
    return true;
}
} // namespace nexus
