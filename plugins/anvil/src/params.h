// Anvil's parameters: one table for the CLAP side (names, ranges, text) and the engine (what a knob means).
// Ids are fixed forever: a saved project keeps its values by id. New parameters take new ids.
#pragma once

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <strings.h>

#ifndef ANVIL_VERSION
#define ANVIL_VERSION "0.5.4"
#endif

namespace anvil
{
namespace ParamId
{
enum : uint32_t
{
    vcoDecay = 0, seqPitchMod, hardSync, vco1Freq, vco1EgAmt, vco1Wave, vco2Freq, vco2EgAmt, vco2Wave, fmAmt, egFmAmt,
    vco1Level, noiseLevel, vco2Level, cutoff, resonance, vcfMode, vcfDecay, vcfEgAmt, noiseVcfMod, vcaEg, vcaDecay, volume,
    clockDiv, run, trigger, advance,
    pitch1, pitch2, pitch3, pitch4, pitch5, pitch6, pitch7, pitch8,
    velocity1, velocity2, velocity3, velocity4, velocity5, velocity6, velocity7, velocity8,
    noiseColor, // added after the rest: ids never move
    length, swing, // 0.3.0
    noteMode, direction, chance, ratchet, velCutoff, pitchCutoff, vcfEgVco2, // 0.4.0
    quantize, // 0.5.0
    count
};
}

enum class Kind : uint8_t { continuous, integer, choice, button };
// How a value reads: what the knob does, for its text.
enum class Unit : uint8_t { vcoHz, cutoffHz, decay, percent, bipolar, octaves, resonance, gain, semitones, plainInt, choice, button };

struct Param
{
    uint32_t id;
    const char* name;
    const char* module;
    float min, max, def;
    Kind kind;
    Unit unit;
    const char* const* choices; // for Kind::choice: max - min + 1 names
};

// --- What the knobs mean ---------------------------------------------------------------------------------------
// Knobs run 0..1 (amounts -1..1); these turn them into the engine's units. Each range is the hardware's where it
// is documented or measured, else chosen to cover what the panel does (see DESIGN.md).

// VCO frequency: exponential over about 10 octaves, its middle the middle of the step Pitch knobs (deep bass a little
// left of it), from below hearing (rhythmic clicks) to a high lead.
constexpr float vcoLowHz = 4.5f, vcoOctaves = 10.0f;
inline float vcoHz (float k) { return vcoLowHz * std::exp2 (k * vcoOctaves); }
inline float vcoKnob (float hz) { return std::log2 (hz / vcoLowHz) / vcoOctaves; }

// Cutoff: exponential, 20 Hz to about 20 kHz.
constexpr float cutoffLowHz = 20.0f, cutoffOctaves = 10.0f;
inline float cutoffHz (float k) { return cutoffLowHz * std::exp2 (k * cutoffOctaves); }
inline float cutoffKnob (float hz) { return std::log2 (hz / cutoffLowHz) / cutoffOctaves; }

// Decay: the time to fall 60 dB, exponential over the knob, 10 ms to 10 s.
constexpr float decayShortest = 0.010f, decayLongest = 10.0f;
inline float decaySeconds (float k) { return decayShortest * std::pow (decayLongest / decayShortest, k); }
inline float decayKnob (float s) { return std::log (s / decayShortest) / std::log (decayLongest / decayShortest); }

// Depths at full turn. The EGs peak at the step's velocity, so each depth is reached at full velocity.
constexpr float vcoEgOctaves = 8.0f;    // VCO 1 / VCO 2 EG Amt at +-100 %: an 8 V envelope into 1 V/octave
constexpr float vcfEgOctaves = 8.0f;    // VCF EG Amt at +-100 %
constexpr float noiseVcfOctaves = 4.0f; // Noise/VCF Mod at 100 %
constexpr float seqPitchOctaves = 5.0f; // a step's Pitch at +-100 %: +-5 V, about 10 octaves end to end
constexpr float fmIndex = 3.0f;         // 1-2 FM at 100 % (linear): VCO 2 swings by 3 times its frequency each way

// Resonance: the ladder's feedback. Low pass self-oscillates from about 3 o'clock (80 % of the turn); high pass is
// weaker and rings without quite oscillating.
constexpr float maxFeedback = 5.0f, maxFeedbackHighPass = 3.9f;

// Envelope attacks (rise to the peak): the VCO and VCF EGs and the VCA's Fast 1 ms, the VCA's Slow 100 ms.
constexpr float attackFast = 0.001f, attackSlow = 0.100f;

inline const char* const seqPitchModChoices[] = { "VCO 1&2", "Off", "VCO 2" };
inline const char* const offOn[] = { "Off", "On" };
inline const char* const waveChoices[] = { "Square", "Triangle" };
inline const char* const vcfModeChoices[] = { "Low Pass", "High Pass" };
inline const char* const vcaEgChoices[] = { "Fast", "Slow" };
inline const char* const runChoices[] = { "Stop", "Run" };
inline const char* const noteModeChoices[] = { "Trigger", "Steps" };
inline const char* const directionChoices[] = { "Forward", "Backward", "Ping-Pong", "Random" };
// Notes: Steps: MIDI notes 36 to 43 (a Melodee drum track's pads 1 to 8) play steps 1 to 8; every 8 notes up, again.
constexpr int firstStepNote = 36;
// The patch routings at full turn: velocity moves the cutoff 4 octaves; the VCF EG moves VCO 2 as far as a VCO EG
// Amt does (8 octaves); a step's pitch moves the cutoff by as much as it moves the VCO (1:1 tracking).
constexpr float velCutoffOctaves = 4.0f, vcfEgVco2Octaves = 8.0f;
inline const char* const noiseColorChoices[] = { "White", "Pink" };
// Quantize: the steps' Pitch knobs snap to semitones, or to the notes of a scale counted from the VCOs' own pitch (the
// Freq knobs: a step at 0 plays them as they are). Off: as the panel's, any pitch.
inline const char* const quantizeChoices[] = { "Off", "Semitones", "Major", "Minor", "Minor Pent", "Phrygian", "Fifths", "Octaves" };
struct QuantizeScale { uint8_t count; uint8_t notes[7]; };
inline constexpr QuantizeScale quantizeScales[] = {
    { 0, {} }, { 12, {} }, { 7, { 0, 2, 4, 5, 7, 9, 11 } }, { 7, { 0, 2, 3, 5, 7, 8, 10 } }, { 5, { 0, 3, 5, 7, 10 } },
    { 7, { 0, 1, 3, 5, 7, 8, 10 } }, { 2, { 0, 7 } }, { 1, { 0 } },
};
// A step's Pitch knob (-1..1, +-5 octaves) as Quantize plays it, in semitones; Off leaves it as it is.
inline float quantizedSemitones (float knobValue, int quantize)
{
    const float semis = knobValue * seqPitchOctaves * 12.0f;
    if (quantize <= 0)
        return semis;
    if (quantize == 1)
        return std::floor (semis + 0.5f);
    const QuantizeScale& scale = quantizeScales[quantize];
    const float octave = std::floor (semis / 12.0f), within = semis - 12.0f * octave;
    float best = 12.0f; // the next octave's root
    for (int i = 0; i < scale.count; ++i)
        if (std::fabs (within - scale.notes[i]) < std::fabs (within - best) || (std::fabs (within - scale.notes[i]) == std::fabs (within - best) && scale.notes[i] > best))
            best = scale.notes[i];
    return 12.0f * octave + best;
}

// The other values a parameter's text depends on: the steps' Pitch reads as Quantize plays it.
struct TextContext
{
    float quantize = 0.0f;
};
// The clock replaces the panel's Tempo: a step is this much of a bar's quarter notes at the host's tempo.
inline const char* const clockDivChoices[] = { "1/2", "1/4", "1/4T", "1/8", "1/8T", "1/16", "1/16T", "1/32", "1/32T", "1/64" };
constexpr double clockDivBeats[] = { 2.0, 1.0, 2.0 / 3.0, 0.5, 1.0 / 3.0, 0.25, 1.0 / 6.0, 0.125, 1.0 / 12.0, 0.0625 };
static_assert (sizeof (clockDivBeats) / sizeof (clockDivBeats[0]) == sizeof (clockDivChoices) / sizeof (clockDivChoices[0]), "a length a choice");

using P = Param;
#define CONT(id, name, module, lo, hi, def, unit) P { ParamId::id, name, module, lo, hi, def, Kind::continuous, Unit::unit, nullptr }
#define INT(id, name, module, lo, hi, def, unit) P { ParamId::id, name, module, lo, hi, def, Kind::integer, Unit::unit, nullptr }
#define CHOICE(id, name, module, list, def) P { ParamId::id, name, module, 0.0f, (float) (sizeof (list) / sizeof (list[0]) - 1), def, Kind::choice, Unit::choice, list }
#define BUTTON(id, name, module) P { ParamId::id, name, module, 0.0f, 1.0f, 0.0f, Kind::button, Unit::button, nullptr }

// In the order a host lists them: a panel section to each three, as Melodee's Edit page shows them (the VCO EG and
// what it does to FM; VCO 1; VCO 2; the switches; the mixer; the filter twice; the VCA; then the sequencer).
inline constexpr Param paramTable[] = {
    CONT (vcoDecay, "VCO Decay", "VCO", 0.0f, 1.0f, 0.35f, decay),
    CONT (fmAmt, "1-2 FM Amt", "VCO", 0.0f, 1.0f, 0.0f, percent),
    CONT (egFmAmt, "VCO EG > FM 1-2 Amt", "VCO", -1.0f, 1.0f, 0.0f, bipolar),
    CONT (vco1Freq, "VCO 1 Freq", "VCO", 0.0f, 1.0f, 0.35f, vcoHz),
    CONT (vco1EgAmt, "VCO 1 EG Amt", "VCO", -1.0f, 1.0f, 0.2f, bipolar),
    CHOICE (vco1Wave, "VCO 1 Wave", "VCO", waveChoices, 0.0f),
    CONT (vco2Freq, "VCO 2 Freq", "VCO", 0.0f, 1.0f, 0.42f, vcoHz),
    CONT (vco2EgAmt, "VCO 2 EG Amt", "VCO", -1.0f, 1.0f, 0.2f, bipolar),
    CHOICE (vco2Wave, "VCO 2 Wave", "VCO", waveChoices, 0.0f),
    CHOICE (seqPitchMod, "Seq Pitch Mod", "VCO", seqPitchModChoices, 0.0f),
    CHOICE (hardSync, "Hard Sync", "VCO", offOn, 0.0f),
    CHOICE (noiseColor, "Noise Color", "Mixer", noiseColorChoices, 0.0f),
    CONT (vco1Level, "VCO 1 Level", "Mixer", 0.0f, 1.0f, 0.8f, percent),
    CONT (noiseLevel, "Noise Level", "Mixer", 0.0f, 1.0f, 0.0f, percent),
    CONT (vco2Level, "VCO 2 Level", "Mixer", 0.0f, 1.0f, 0.5f, percent),
    CONT (cutoff, "Cutoff", "VCF", 0.0f, 1.0f, 0.6f, cutoffHz),
    CONT (resonance, "Resonance", "VCF", 0.0f, 1.0f, 0.15f, resonance),
    CHOICE (vcfMode, "VCF Mode", "VCF", vcfModeChoices, 0.0f),
    CONT (vcfDecay, "VCF Decay", "VCF", 0.0f, 1.0f, 0.35f, decay),
    CONT (vcfEgAmt, "VCF EG Amt", "VCF", -1.0f, 1.0f, 0.3f, bipolar),
    CONT (noiseVcfMod, "Noise/VCF Mod", "VCF", 0.0f, 1.0f, 0.0f, percent),
    CHOICE (vcaEg, "VCA EG", "VCA", vcaEgChoices, 0.0f),
    CONT (vcaDecay, "VCA Decay", "VCA", 0.0f, 1.0f, 0.4f, decay),
    CONT (volume, "Volume", "VCA", 0.0f, 1.0f, 0.7f, gain),
    CONT (velCutoff, "Vel > Cutoff", "Patch", -1.0f, 1.0f, 0.0f, bipolar),
    CONT (pitchCutoff, "Pitch > Cutoff", "Patch", -1.0f, 1.0f, 0.0f, bipolar),
    CONT (vcfEgVco2, "VCF EG > VCO 2", "Patch", -1.0f, 1.0f, 0.0f, bipolar),
    CHOICE (clockDiv, "Clock Div", "Sequencer", clockDivChoices, 3.0f),
    INT (length, "Length", "Sequencer", 1.0f, 8.0f, 8.0f, plainInt),
    CONT (swing, "Swing", "Sequencer", 0.5f, 0.75f, 0.5f, percent),
    CHOICE (direction, "Direction", "Sequencer", directionChoices, 0.0f),
    CONT (chance, "Chance", "Sequencer", 0.0f, 1.0f, 1.0f, percent),
    INT (ratchet, "Ratchet", "Sequencer", 1.0f, 4.0f, 1.0f, plainInt),
    CHOICE (run, "Run", "Sequencer", runChoices, 1.0f),
    BUTTON (trigger, "Trigger", "Sequencer"),
    BUTTON (advance, "Advance", "Sequencer"),
    CHOICE (noteMode, "Notes", "Sequencer", noteModeChoices, 0.0f),
    CHOICE (quantize, "Quantize", "Sequencer", quantizeChoices, 0.0f),
    CONT (pitch1, "Pitch 1", "Sequencer/Pitch", -1.0f, 1.0f, 0.0f, semitones),
    CONT (pitch2, "Pitch 2", "Sequencer/Pitch", -1.0f, 1.0f, 0.0f, semitones),
    CONT (pitch3, "Pitch 3", "Sequencer/Pitch", -1.0f, 1.0f, 0.0f, semitones),
    CONT (pitch4, "Pitch 4", "Sequencer/Pitch", -1.0f, 1.0f, 0.0f, semitones),
    CONT (pitch5, "Pitch 5", "Sequencer/Pitch", -1.0f, 1.0f, 0.0f, semitones),
    CONT (pitch6, "Pitch 6", "Sequencer/Pitch", -1.0f, 1.0f, 0.0f, semitones),
    CONT (pitch7, "Pitch 7", "Sequencer/Pitch", -1.0f, 1.0f, 0.0f, semitones),
    CONT (pitch8, "Pitch 8", "Sequencer/Pitch", -1.0f, 1.0f, 0.0f, semitones),
    CONT (velocity1, "Velocity 1", "Sequencer/Velocity", 0.0f, 1.0f, 1.0f, percent),
    CONT (velocity2, "Velocity 2", "Sequencer/Velocity", 0.0f, 1.0f, 0.6f, percent),
    CONT (velocity3, "Velocity 3", "Sequencer/Velocity", 0.0f, 1.0f, 0.8f, percent),
    CONT (velocity4, "Velocity 4", "Sequencer/Velocity", 0.0f, 1.0f, 0.6f, percent),
    CONT (velocity5, "Velocity 5", "Sequencer/Velocity", 0.0f, 1.0f, 1.0f, percent),
    CONT (velocity6, "Velocity 6", "Sequencer/Velocity", 0.0f, 1.0f, 0.6f, percent),
    CONT (velocity7, "Velocity 7", "Sequencer/Velocity", 0.0f, 1.0f, 0.8f, percent),
    CONT (velocity8, "Velocity 8", "Sequencer/Velocity", 0.0f, 1.0f, 0.6f, percent),
};
#undef CONT
#undef INT
#undef CHOICE
#undef BUTTON

constexpr uint32_t paramCount = sizeof (paramTable) / sizeof (paramTable[0]);
static_assert (paramCount == ParamId::count, "every id has one row");
static_assert (paramCount <= 64, "the screen keeps a bit a row");

// The table's row for an id (ids are the rows' order today, but a host gives ids, so they are looked up).
struct IndexOfId
{
    int16_t row[ParamId::count];
    constexpr IndexOfId() : row()
    {
        for (uint32_t i = 0; i < ParamId::count; ++i)
            row[i] = -1;
        for (uint32_t i = 0; i < paramCount; ++i)
            row[paramTable[i].id] = (int16_t) i;
    }
};
inline constexpr IndexOfId indexOfId {};
inline int paramIndex (uint32_t id) { return id < ParamId::count ? indexOfId.row[id] : -1; }

inline float clampParam (const Param& p, float v)
{
    if (! (v >= p.min)) v = p.min; // NaN too
    if (v > p.max) v = p.max;
    return p.kind == Kind::continuous ? v : std::round (v);
}

inline void formatParam (const Param& p, float v, char* text, uint32_t size, const TextContext& context = {})
{
    v = clampParam (p, v);
    auto time = [&] (float s) {
        if (s < 1.0f) std::snprintf (text, size, "%.0f ms", s * 1000.0f);
        else std::snprintf (text, size, "%.2f s", s);
    };
    auto hz = [&] (float f) {
        if (f < 1000.0f) std::snprintf (text, size, "%.1f Hz", f);
        else std::snprintf (text, size, "%.2f kHz", f / 1000.0f);
    };
    switch (p.unit)
    {
        case Unit::vcoHz: hz (vcoHz (v)); break;
        case Unit::cutoffHz: hz (cutoffHz (v)); break;
        case Unit::decay: time (decaySeconds (v)); break;
        case Unit::percent:
        case Unit::resonance:
        case Unit::gain: std::snprintf (text, size, "%.0f %%", v * 100.0f); break;
        case Unit::bipolar: std::snprintf (text, size, "%+.0f %%", v * 100.0f); break;
        case Unit::octaves: std::snprintf (text, size, "%+.2f oct", v); break;
        case Unit::semitones:
            if (context.quantize >= 0.5f)
                std::snprintf (text, size, "%+.0f st", quantizedSemitones (v, (int) context.quantize));
            else
                std::snprintf (text, size, "%+.1f st", v * seqPitchOctaves * 12.0f);
            break;
        case Unit::plainInt: std::snprintf (text, size, "%d", (int) v); break;
        case Unit::choice: std::snprintf (text, size, "%s", p.choices[(int) v]); break;
        case Unit::button: std::snprintf (text, size, "%s", v >= 0.5f ? "Pressed" : "Press"); break;
    }
}

// Reads text as formatParam writes it (units optional); a choice by its name or its number.
inline bool parseParam (const Param& p, const char* text, float& v)
{
    if (p.kind == Kind::choice)
    {
        const int n = (int) (p.max - p.min) + 1;
        for (int i = 0; i < n; ++i)
            if (strcasecmp (text, p.choices[i]) == 0)
                return v = (float) i, true;
    }
    if (p.kind == Kind::button)
    {
        v = (strcasecmp (text, "Pressed") == 0 || std::atof (text) >= 0.5) ? 1.0f : 0.0f;
        return true;
    }
    char* end = nullptr;
    double x = std::strtod (text, &end);
    if (end == text)
        return false;
    while (*end == ' ')
        ++end;
    const bool kilo = (*end == 'k' || *end == 'K');
    const bool milli = (end[0] == 'm' && end[1] == 's');
    switch (p.unit)
    {
        case Unit::vcoHz: v = vcoKnob ((float) (kilo ? x * 1000.0 : x)); break;
        case Unit::cutoffHz: v = cutoffKnob ((float) (kilo ? x * 1000.0 : x)); break;
        case Unit::decay: v = decayKnob ((float) (milli ? x / 1000.0 : x)); break;
        case Unit::percent:
        case Unit::resonance:
        case Unit::gain:
        case Unit::bipolar: v = (float) (x / 100.0); break;
        case Unit::semitones: v = (float) (x / (seqPitchOctaves * 12.0)); break;
        default: v = (float) x; break;
    }
    v = clampParam (p, v);
    return true;
}
} // namespace anvil
