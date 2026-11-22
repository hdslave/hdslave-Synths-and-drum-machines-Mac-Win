// Foundry's parameters: one table for the CLAP side (names, ranges, text) and the engine (what a knob means).
// Ids are fixed forever: a saved project keeps its values by id. New parameters take new ids.
//
// Ids: the kit's own controls are 0-63 (M::), voice v's are 64 + 64 v + its offset (V::). The table's order is only
// the order a host lists them in: the kit's levels first, then each voice in pad order, then the effects.
#pragma once

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <strings.h>

#ifndef FOUNDRY_VERSION
#define FOUNDRY_VERSION "0.6.3"
#endif

namespace foundry
{
// --- The kit -----------------------------------------------------------------------------------------------------

constexpr int voiceCount = 16;

// The circuits a voice can be. Each voice has one, and three models of it (808, 909 and 606 style).
enum class Type : uint8_t { kick, rim, snare, clap, tom, hat, cymbal, cowbell, clave, maraca };
enum Model : uint8_t { m808 = 0, m909 = 1, m606 = 2 };
constexpr int modelCount = 3;

enum class Kind : uint8_t { continuous, integer, choice };
// How a value reads, and for the mapped ones (a knob 0..1 to a time or a frequency) what it means: see the mapping
// functions below, shared by the text and the engine.
enum class Unit : uint8_t
{
    percent, bipolar, semitones, db, pan, choice, plainInt, ratio,
    ms,        // the value is milliseconds
    msOff,     // milliseconds, 0 reads "Off"
    muteMs,    // 0 "Kit" (the kit's Choke Time), else 1 .. 200 ms
    attackMs,  // 0..1 -> 0..100 ms (squared)
    gateMs,    // 0 Off, else 5 ms .. 2 s
    envMs,     // 5 ms .. 2 s
    chokeMs,   // 1 .. 200 ms
    releaseMs, // 20 ms .. 1 s
    revTime,   // 0.3 .. 12 s
    compAtkMs, // 0.1 .. 30 ms
    cutoffHz,  // 20 Hz .. 20 kHz
    lowCutHz,  // 20 Hz .. 1 kHz
    afxFilter, // -1..0 low pass from 20 kHz to 100 Hz, 0..1 high pass from 20 Hz to 8 kHz
};

// A voice's own controls, by type: the TR-1000 names where it has the control (its analog, 8X/9X and 606 models'),
// new ones where it does not. Each type has 2, 5, 8 or 11 so that every voice's block is whole rows of three.
constexpr int maxSlots = 11;
struct SlotSpec
{
    const char* name;
    float min, max;
    Unit unit;
};
struct TypeSpec
{
    const char* name;
    uint8_t slots;
    SlotSpec slot[maxSlots];
};
inline constexpr TypeSpec typeSpecs[] = {
    { "Kick", 5, { { "Attack", 0, 1, Unit::percent }, { "Tone", 0, 1, Unit::percent }, { "Body Depth", 0, 1, Unit::percent },
                   { "Body Decay", 0, 1, Unit::percent }, { "Excite", 0, 1, Unit::percent } } },
    { "Rim", 11, { { "Tone", -1, 1, Unit::bipolar }, { "Body", 0, 1, Unit::percent }, { "Noise", 0, 1, Unit::percent },
                   { "Freq Mod", 0, 1, Unit::percent }, { "Click", 0, 1, Unit::percent }, { "O1 Tune", 0, 1, Unit::percent },
                   { "O2 Tune", 0, 1, Unit::percent }, { "O3 Tune", 0, 1, Unit::percent }, { "O1 Decay", 0, 1, Unit::percent },
                   { "O2 Decay", 0, 1, Unit::percent }, { "O3 Decay", 0, 1, Unit::percent } } },
    { "Snare", 8, { { "Tone", -1, 1, Unit::bipolar }, { "Snappy", 0, 1, Unit::percent }, { "Snappy Decay", 0, 1, Unit::percent },
                    { "Detune", -1, 1, Unit::bipolar }, { "Osc Mix", -1, 1, Unit::bipolar }, { "Pitch Env", 0, 1, Unit::percent },
                    { "Pitch Decay", 0, 1, Unit::percent }, { "Freq Mod", 0, 1, Unit::percent } } },
    { "Clap", 8, { { "Claps", 0, 1, Unit::percent }, { "Speed", 0, 1, Unit::percent }, { "Clap Decay", 0, 1, Unit::percent },
                   { "Mix", -1, 1, Unit::bipolar }, { "Filter", -1, 1, Unit::bipolar }, { "Nuance", 0, 1, Unit::percent },
                   { "Size", -1, 1, Unit::bipolar }, { "Tail Tone", 0, 1, Unit::percent } } },
    { "Tom", 11, { { "Color", 0, 1, Unit::percent }, { "Color Tone", 0, 1, Unit::percent }, { "Color Decay", 0, 1, Unit::percent },
                   { "Pitch Env", 0, 1, Unit::percent }, { "Pitch Decay", 0, 1, Unit::percent }, { "Detune", -1, 1, Unit::bipolar },
                   { "Osc Mix", -1, 1, Unit::bipolar }, { "Shape", 0, 1, Unit::percent }, { "Detail", 0, 1, Unit::percent },
                   { "Attack", 0, 1, Unit::percent }, { "Tone", -1, 1, Unit::bipolar } } },
    { "Hat", 8, { { "Tone", -1, 1, Unit::bipolar }, { "Sculpt", 0, 1, Unit::percent }, { "XMod", 0, 1, Unit::percent },
                  { "Blend", 0, 1, Unit::percent }, { "Detune", 0, 1, Unit::percent }, { "Amp Mod", 0, 1, Unit::percent },
                  { "Error", 0, 1, Unit::percent }, { "Noise", 0, 1, Unit::percent } } },
    { "Cymbal", 8, { { "Tone", -1, 1, Unit::bipolar }, { "Sculpt", -1, 1, Unit::bipolar }, { "Filter", 0, 1, Unit::percent },
                     { "Blend", 0, 1, Unit::percent }, { "Detune", 0, 1, Unit::percent }, { "Amp Mod", 0, 1, Unit::percent },
                     { "Error", 0, 1, Unit::percent }, { "Noise", 0, 1, Unit::percent } } },
    { "Cowbell", 8, { { "Detune", -1, 1, Unit::bipolar }, { "Osc Mix", -1, 1, Unit::bipolar }, { "XMod", 0, 1, Unit::percent },
                      { "Ratio", 0.01f, 2, Unit::ratio }, { "Noise", 0, 1, Unit::percent }, { "Shape", 0, 1, Unit::percent },
                      { "Unison", -12, 12, Unit::semitones }, { "Tone", -1, 1, Unit::bipolar } } },
    { "Clave", 2, { { "Attack", 0, 1, Unit::percent }, { "Tone", -1, 1, Unit::bipolar } } },
    { "Maraca", 2, { { "Attack", 0, 1, Unit::percent }, { "Tone", -1, 1, Unit::bipolar } } },
};
inline constexpr const TypeSpec& typeSpec (Type t) { return typeSpecs[(int) t]; }

// The sixteen voices, in the order of their ids (where each sits on the pads: padVoice below).
struct VoiceSpec
{
    const char* tag;  // "BD": the parameters' prefix and the link/side-chain lists' name
    const char* name; // "Bass Drum": the pad's name (15 characters at most: Melodee's pad names)
    Type type;
    uint8_t model;        // the model it opens with
    float hz[modelCount]; // its pitch at Tune 0 in each model (the metal's: a multiplier of its oscillators)
    float decay[modelCount][2]; // Decay 0 and 100 %: the time to fall 60 dB, seconds, per model
    float level, pan, decayKnob;
    float slot[maxSlots]; // the slots' defaults
    uint8_t choke; // its choke group at first (0 none)
    int8_t gm[4];  // its General MIDI keys (Key Map: GM), -1 after the last
    float aux;     // hats: the 808 style's high pass after the VCA; cymbals: the 909 style's DAC low pass (Hz)
    float knee[modelCount]; // hats and cymbals: where the 808 and 606 styles' swing VCAs let go, a share of the
                            // envelope's peak; for the 909 style 1 holds the VCA's envelope before it falls (the crash)
};

// clang-format off
inline constexpr VoiceSpec voiceSpecs[voiceCount] = {
    // Pitches: the 808's and 606's measured from the machines (kick, snare, toms, congas, rim, claves, cowbell), the 909's
    // where measured (kick, snare body, rim) and estimated elsewhere; see the README.
    { "BD", "Bass Drum",   Type::kick,    m808, {   53.0f,   53.0f,   62.0f }, { { 0.10f, 5.0f }, { 0.15f, 3.0f }, { 0.08f, 1.5f } }, 0.0f,  0.0f,  0.45f, { 0.35f, 0.5f, 0.2f, 0.35f, 0.0f }, 0, { 36, -1 }, 0.0f, { 0.0f, 0.0f, 0.0f } },
    { "RS", "Rim Shot",    Type::rim,     m808, {  455.0f,  220.0f, 1100.0f }, { { 0.010f, 0.20f }, { 0.012f, 0.25f }, { 0.008f, 0.15f } }, -6.0f, 0.1f, 0.35f, { 0.0f, 0.3f, 0.35f, 0.0f, 0.15f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f }, 0, { 37, -1 }, 0.0f, { 0.0f, 0.0f, 0.0f } },
    { "SD", "Snare Drum",  Type::snare,   m909, {  175.0f,  230.0f,  205.0f }, { { 0.06f, 1.00f }, { 0.06f, 0.70f }, { 0.05f, 0.60f } }, -2.0f, 0.0f, 0.4f, { 0.0f, 0.55f, 0.4f, 0.0f, 0.0f, 0.25f, 0.3f, 0.0f }, 0, { 38, 40, -1 }, 0.0f, { 0.0f, 0.0f, 0.0f } },
    { "CP", "Hand Clap",   Type::clap,    m808, { 1100.0f, 1200.0f, 1400.0f }, { { 0.08f, 1.50f }, { 0.07f, 1.20f }, { 0.05f, 0.80f } }, -3.0f, 0.0f, 0.52f, { 0.43f, 0.5f, 0.4f, 0.0f, 0.4f, 0.4f, 0.0f, 0.5f }, 0, { 39, -1 }, 0.0f, { 0.0f, 0.0f, 0.0f } },
    { "LT", "Low Tom",     Type::tom,     m808, {   90.0f,  100.0f,  145.0f }, { { 0.10f, 2.00f }, { 0.12f, 2.50f }, { 0.06f, 1.20f } }, -3.0f, -0.25f, 0.45f, { 0.1f, 0.4f, 0.4f, 0.2f, 0.4f, 0.0f, -1.0f, 0.0f }, 0, { 41, 43, -1 }, 0.0f, { 0.0f, 0.0f, 0.0f } },
    { "MT", "Mid Tom",     Type::tom,     m808, {  132.0f,  140.0f,  175.0f }, { { 0.09f, 1.80f }, { 0.11f, 2.20f }, { 0.05f, 1.10f } }, -3.0f, 0.0f, 0.45f, { 0.0f, 0.45f, 0.35f, 0.12f, 0.4f, 0.0f, -1.0f, 0.0f }, 0, { 45, 47, -1 }, 0.0f, { 0.0f, 0.0f, 0.0f } },
    { "HT", "High Tom",    Type::tom,     m808, {  185.0f,  200.0f,  205.0f }, { { 0.08f, 1.60f }, { 0.10f, 2.00f }, { 0.05f, 1.00f } }, -3.0f, 0.25f, 0.45f, { 0.0f, 0.5f, 0.35f, 0.05f, 0.4f, 0.0f, -1.0f, 0.0f }, 0, { 48, 50, -1 }, 0.0f, { 0.0f, 0.0f, 0.0f } },
    { "CH", "Closed Hat",  Type::hat,     m808, { 1.0f, 1.0f, 1.0f }, { { 0.40f, 4.00f }, { 0.08f, 0.60f }, { 0.04f, 0.40f } }, -6.0f, 0.15f, 0.48f, { 0.0f, 0.35f, 0.0f, 1.0f, 0.4286f, 0.0f, 0.0f, 0.0f }, 1, { 42, 44, -1 }, 6500.0f, { 0.6f, 0.0f, 0.0f } },
    { "OH", "Open Hat",    Type::hat,     m808, { 1.0f, 1.0f, 1.0f }, { { 0.60f, 8.00f }, { 0.10f, 1.50f }, { 0.60f, 15.0f } }, -7.0f, 0.15f, 0.78f, { 0.0f, 0.35f, 0.0f, 1.0f, 0.4286f, 0.0f, 0.0f, 0.0f }, 1, { 46, -1 }, 5500.0f, { 0.62f, 0.0f, 0.3f } },
    { "CY", "Crash",       Type::cymbal,  m808, { 1.0f, 1.0f, 1.0f }, { { 0.60f, 10.0f }, { 0.50f, 7.00f }, { 0.60f, 8.00f } }, -8.0f, -0.3f, 0.5f, { 0.0f, 0.0f, 0.5f, 1.0f, 0.4286f, 0.0f, 0.0f, 0.0f }, 0, { 49, 52, 55, 57 }, 8500.0f, { 0.03f, 1.0f, 0.0f } },
    { "RD", "Ride",        Type::cymbal,  m909, { 1.3f, 1.3f, 1.3f }, { { 0.60f, 10.0f }, { 0.40f, 5.00f }, { 0.25f, 4.00f } }, -9.0f, 0.3f, 0.5f, { 1.0f, -0.3f, 0.6f, 1.0f, 0.3f, 0.0f, 0.0f, 0.0f }, 0, { 51, 53, 59, -1 }, 15000.0f, { 0.03f, 0.0f, 0.0f } },
    { "CB", "Cowbell",     Type::cowbell, m808, {  540.0f,  587.0f,  690.0f }, { { 0.20f, 2.50f }, { 0.20f, 2.50f }, { 0.10f, 1.20f } }, -8.0f, -0.2f, 0.55f, { 0.0f, 0.7f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f }, 0, { 56, -1 }, 0.0f, { 0.0f, 0.0f, 0.0f } },
    { "CL", "Claves",      Type::clave,   m808, { 2450.0f, 1800.0f, 3300.0f }, { { 0.02f, 0.25f }, { 0.02f, 0.25f }, { 0.015f, 0.18f } }, -8.0f, 0.35f, 0.35f, { 0.5f, 0.0f }, 0, { 75, 76, 77, -1 }, 0.0f, { 0.0f, 0.0f, 0.0f } },
    { "MA", "Maracas",     Type::maraca,  m808, { 7000.0f, 9000.0f, 10000.0f }, { { 0.02f, 0.30f }, { 0.02f, 0.30f }, { 0.015f, 0.20f } }, -10.0f, -0.35f, 0.2f, { 0.6f, 0.0f }, 0, { 70, 69, -1 }, 0.0f, { 0.0f, 0.0f, 0.0f } },
    { "CG", "Conga",       Type::tom,     m808, {  215.0f,  250.0f,  300.0f }, { { 0.05f, 1.00f }, { 0.06f, 1.20f }, { 0.04f, 0.60f } }, -5.0f, -0.4f, 0.3f, { 0.0f, 0.5f, 0.2f, 0.05f, 0.3f, 0.0f, -1.0f, 0.0f }, 0, { 62, 63, 64, -1 }, 0.0f, { 0.0f, 0.0f, 0.0f } },
    { "B2", "Bass Drum 2", Type::kick,    m909, {   53.0f,   53.0f,   62.0f }, { { 0.10f, 5.0f }, { 0.15f, 3.0f }, { 0.08f, 1.5f } }, -3.0f, 0.0f, 0.4f, { 0.5f, 0.6f, 0.45f, 0.35f, 0.0f }, 0, { 35, -1 }, 0.0f, { 0.0f, 0.0f, 0.0f } },
};
// clang-format on

// The pads, from A01 (bottom left) along each row and up, and the voice each plays: BD, SD, CP, CH; B2, RS, CY, OH;
// LT, MT, HT, CB; RD, CL, MA, CG. Pad k is MIDI note 36 + k (Key Map: Pads). The voices keep their ids (their order
// above); 0.6.0 moved where they sit.
inline constexpr int8_t padVoice[voiceCount] = { 0, 2, 3, 7, 15, 1, 9, 8, 4, 5, 6, 11, 10, 12, 13, 14 };
constexpr int padOf (int voice)
{
    for (int k = 0; k < voiceCount; ++k)
        if (padVoice[k] == voice)
            return k;
    return -1;
}
constexpr bool padsWhole()
{
    for (int v = 0; v < voiceCount; ++v)
        if (padOf (v) < 0)
            return false;
    return true;
}
static_assert (padsWhole(), "every voice on a pad");

// --- Ids ---------------------------------------------------------------------------------------------------------

// The kit's controls.
namespace M
{
enum : uint32_t
{
    volume = 0, kitTune, kitDecay, accent, accentVel, chokeTime,
    revLevel, revTime, revPreDelay, revTone, revLowCut, revMod,
    dlyLevel, dlyTime, dlyFeedback, dlyTone, dlyMode, dlyToRev,
    scSource, scRelease, comp, compAttack, compRelease, compMix,
    afxFilter, afxReso, afxDrive, clip, keyMap, keysVoice, step, velCurve, humanize,
    compKey, // what the bus compressor's detector hears
    count
};
}
// A voice's controls: offsets from the voice's first id. Slots 0-7 are its type's own (TypeSpec).
namespace V
{
enum : uint32_t
{
    level = 0, pan, model, tune, decay, slot0,
    bendType = slot0 + 8, bend, drive, filter, cutoff, reso, fltEnv, fltDecay, attack, gate, velSens, accent, choke,
    reverb, delay, duck, prob, flam, subStep, cycle, nudge, link, lfoTarget, lfoWave, lfoRate, lfoDepth,
    phase, bendMix, muteDecay, // 0.5.0: the TR-1000's PHASE, its reformer's amount, the 808 open hat's MUTE DCY
    slot8,                     // slots 8-10 (the 11-slot types')
    count = slot8 + 3
};
}
static_assert (M::count <= 64 && V::count <= 64, "ids fit their ranges");
constexpr uint32_t voiceIdBase = 64, voiceIdStride = 64, idLimit = voiceIdBase + voiceIdStride * voiceCount;
constexpr uint32_t voiceId (int voice, uint32_t offset) { return voiceIdBase + (uint32_t) voice * voiceIdStride + offset; }
// A type's slot k: the first eight follow the voice's first controls, the rest came later and sit after the others.
constexpr uint32_t slotId (int k) { return k < 8 ? V::slot0 + (uint32_t) k : V::slot8 + (uint32_t) (k - 8); }

// --- Choices -----------------------------------------------------------------------------------------------------

inline const char* const modelChoices[] = { "808", "909", "606" };
inline const char* const phaseChoices[] = { "Normal", "Inverse" };
inline const char* const filterChoices[] = { "Off", "Low Pass", "High Pass", "Band Pass" };
inline const char* const chokeChoices[] = { "Off", "1", "2", "3", "4" };
inline const char* const subStepChoices[] = { "1", "2", "3", "4", "6", "8" };
constexpr int subStepCounts[] = { 1, 2, 3, 4, 6, 8 };
// Cycle: "n/m" plays in the nth of every m bars counted from the song's top (the TR-1000's step cycle, by bars).
inline const char* const cycleChoices[] = { "Every", "1/2", "2/2", "1/3", "2/3", "3/3", "1/4", "2/4", "3/4", "4/4",
                                            "1/8", "2/8", "3/8", "4/8", "5/8", "6/8", "7/8", "8/8", "1st Only" };
constexpr int8_t cycleOf[][2] = { { 1, 1 }, { 1, 2 }, { 2, 2 }, { 1, 3 }, { 2, 3 }, { 3, 3 }, { 1, 4 }, { 2, 4 }, { 3, 4 }, { 4, 4 },
                                  { 1, 8 }, { 2, 8 }, { 3, 8 }, { 4, 8 }, { 5, 8 }, { 6, 8 }, { 7, 8 }, { 8, 8 }, { 1, 0 } };
// "Off" and the voices' tags: Link, the side-chain source and the Keys voice.
inline const char* const voiceChoices[] = { "Off", "BD", "RS", "SD", "CP", "LT", "MT", "HT", "CH", "OH", "CY", "RD", "CB", "CL", "MA", "CG", "B2" };
inline const char* const lfoTargetChoices[] = { "Off", "Tune", "Decay", "Cutoff", "Level", "Pan", "Bend", "Drive" };
enum LfoTarget : uint8_t { lfoOff, lfoTune, lfoDecay, lfoCutoff, lfoLevel, lfoPan, lfoBend, lfoDrive };
inline const char* const lfoWaveChoices[] = { "Sine", "Triangle", "Saw", "Square", "Random" };
inline const char* const lfoRateChoices[] = { "8 Bars", "4 Bars", "2 Bars", "1 Bar", "1/2", "1/4", "1/4T", "1/8", "1/8T", "1/16", "1/16T", "1/32" };
constexpr double lfoRateBeats[] = { 32.0, 16.0, 8.0, 4.0, 2.0, 1.0, 2.0 / 3.0, 0.5, 1.0 / 3.0, 0.25, 1.0 / 6.0, 0.125 };
inline const char* const dlyTimeChoices[] = { "1/32", "1/16T", "1/16", "1/8T", "1/16D", "1/8", "1/4T", "1/8D", "1/4", "1/2T", "1/4D", "1/2" };
constexpr double dlyTimeBeats[] = { 0.125, 1.0 / 6.0, 0.25, 1.0 / 3.0, 0.375, 0.5, 2.0 / 3.0, 0.75, 1.0, 4.0 / 3.0, 1.5, 2.0 };
inline const char* const dlyModeChoices[] = { "Mono", "Ping-Pong", "Tape" };
inline const char* const clipChoices[] = { "Off", "Soft", "Limit" };
inline const char* const keyMapChoices[] = { "Pads", "GM" };
inline const char* const stepChoices[] = { "1/8", "1/8T", "1/16", "1/16T", "1/32" };
constexpr double stepBeats[] = { 0.5, 1.0 / 3.0, 0.25, 1.0 / 6.0, 0.125 };
// Comp Key: the mix, or the side-chain source voice alone (the kick by default) keying the bus compressor.
inline const char* const compKeyChoices[] = { "Mix", "SC Source" };
inline const char* const velCurveChoices[] = { "Soft", "Linear", "Hard" };
constexpr float velCurveExponent[] = { 0.6f, 1.0f, 1.8f };

#define FOUNDRY_COUNT(list) ((uint8_t) (sizeof (list) / sizeof (list[0])))
static_assert (sizeof (cycleOf) / sizeof (cycleOf[0]) == FOUNDRY_COUNT (cycleChoices), "a cycle a choice");
static_assert (sizeof (lfoRateBeats) / sizeof (lfoRateBeats[0]) == FOUNDRY_COUNT (lfoRateChoices), "a rate a choice");
static_assert (sizeof (dlyTimeBeats) / sizeof (dlyTimeBeats[0]) == FOUNDRY_COUNT (dlyTimeChoices), "a time a choice");
static_assert (sizeof (stepBeats) / sizeof (stepBeats[0]) == FOUNDRY_COUNT (stepChoices), "a step a choice");
static_assert (FOUNDRY_COUNT (voiceChoices) == voiceCount + 1, "Off and every voice");

// --- What the knobs mean -----------------------------------------------------------------------------------------

inline float attackMs (float k) { return 100.0f * k * k; }
inline float gateMs (float k) { return 5.0f * std::pow (400.0f, k); } // k > 0; 0 is Off
inline float envMs (float k) { return 5.0f * std::pow (400.0f, k); }
inline float chokeMs (float k) { return std::pow (200.0f, k); }
inline float releaseMs (float k) { return 20.0f * std::pow (50.0f, k); }
inline float revSeconds (float k) { return 0.3f * std::pow (40.0f, k); }
inline float compAttackMs (float k) { return 0.1f * std::pow (300.0f, k); }
inline float cutoffHz (float k) { return 20.0f * std::pow (1000.0f, k); }
inline float lowCutHz (float k) { return 20.0f * std::pow (50.0f, k); }
inline float afxLowPassHz (float k) { return 20000.0f * std::pow (0.005f, -k); } // k in [-1, 0]
inline float afxHighPassHz (float k) { return 20.0f * std::pow (400.0f, k); }   // k in [0, 1]
constexpr float silentDb = -60.0f; // a level at its bottom is off
inline float dbGain (float db) { return db <= silentDb ? 0.0f : std::pow (10.0f, db * 0.05f); }

// --- The table ---------------------------------------------------------------------------------------------------

struct Param
{
    uint32_t id;
    char name[32];
    const char* module;
    float min, max, def;
    Kind kind;
    Unit unit;
    const char* const* choices; // Kind::choice: max - min + 1 names
};

// How many rows: the kit's and each voice's (34 shared controls and its type's slots).
constexpr uint32_t sharedVoiceControls = 34;
constexpr uint32_t countRows()
{
    uint32_t n = M::count;
    for (const auto& v : voiceSpecs)
        n += sharedVoiceControls + typeSpecs[(int) v.type].slots;
    return n;
}
constexpr uint32_t paramCount = countRows();

struct ParamTable
{
    Param rows[paramCount];
    int16_t row[idLimit]; // the row of an id, -1 none
    uint32_t count = 0;

    void add (uint32_t id, const char* prefix, const char* name, const char* module, float min, float max, float def, Kind kind,
              Unit unit, const char* const* choices = nullptr)
    {
        Param& p = rows[count];
        p.id = id;
        if (prefix != nullptr)
            std::snprintf (p.name, sizeof (p.name), "%s %s", prefix, name);
        else
            std::snprintf (p.name, sizeof (p.name), "%s", name);
        p.module = module;
        p.min = min, p.max = max, p.def = def;
        p.kind = kind, p.unit = unit, p.choices = choices;
        row[id] = (int16_t) count;
        ++count;
    }
    void cont (uint32_t id, const char* pre, const char* name, const char* module, float lo, float hi, float def, Unit unit)
    {
        add (id, pre, name, module, lo, hi, def, Kind::continuous, unit);
    }
    void choice (uint32_t id, const char* pre, const char* name, const char* module, const char* const* list, uint8_t n, float def)
    {
        add (id, pre, name, module, 0.0f, (float) (n - 1), def, Kind::choice, Unit::choice, list);
    }
};

#define FOUNDRY_CHOICE(t, id, pre, name, module, list, def) t.choice (id, pre, name, module, list, FOUNDRY_COUNT (list), def)

inline ParamTable buildTable()
{
    ParamTable t {};
    for (auto& r : t.row)
        r = -1;
    const char* kit = "Kit";
    t.cont (M::volume, nullptr, "Volume", kit, silentDb, 6.0f, 0.0f, Unit::db);
    t.cont (M::kitTune, nullptr, "Kit Tune", kit, -12.0f, 12.0f, 0.0f, Unit::semitones);
    t.cont (M::kitDecay, nullptr, "Kit Decay", kit, -1.0f, 1.0f, 0.0f, Unit::bipolar);
    t.cont (M::accent, nullptr, "Accent", kit, 0.0f, 1.0f, 0.5f, Unit::percent);
    t.add (M::accentVel, nullptr, "Accent Vel", kit, 1.0f, 127.0f, 100.0f, Kind::integer, Unit::plainInt);
    t.cont (M::chokeTime, nullptr, "Choke Time", kit, 0.0f, 1.0f, 0.45f, Unit::chokeMs);

    for (int pad = 0; pad < voiceCount; ++pad) // listed in pad order
    {
        const int v = padVoice[pad];
        const VoiceSpec& s = voiceSpecs[v];
        const TypeSpec& ts = typeSpecs[(int) s.type];
        const char* pre = s.tag;
        const char* mod = s.name;
        auto id = [v] (uint32_t off) { return voiceId (v, off); };
        t.cont (id (V::level), pre, "Level", mod, silentDb, 6.0f, s.level, Unit::db);
        t.cont (id (V::tune), pre, "Tune", mod, -24.0f, 24.0f, 0.0f, Unit::semitones);
        t.cont (id (V::decay), pre, "Decay", mod, 0.0f, 1.0f, s.decayKnob, Unit::percent);
        for (int k = 0; k < ts.slots; ++k)
            t.cont (id (slotId (k)), pre, ts.slot[k].name, mod, ts.slot[k].min, ts.slot[k].max, s.slot[k], ts.slot[k].unit);
        FOUNDRY_CHOICE (t, id (V::model), pre, "Model", mod, modelChoices, (float) s.model);
        t.cont (id (V::pan), pre, "Pan", mod, -1.0f, 1.0f, s.pan, Unit::pan);
        FOUNDRY_CHOICE (t, id (V::phase), pre, "Phase", mod, phaseChoices, 0.0f);
        t.cont (id (V::bendType), pre, "Bend Type", mod, 0.0f, 1.0f, 0.0f, Unit::percent);
        t.cont (id (V::bend), pre, "Bend", mod, 0.0f, 1.0f, 0.0f, Unit::percent);
        t.cont (id (V::bendMix), pre, "Bend Mix", mod, 0.0f, 1.0f, 1.0f, Unit::percent);
        t.cont (id (V::drive), pre, "Drive", mod, 0.0f, 1.0f, 0.0f, Unit::percent);
        FOUNDRY_CHOICE (t, id (V::filter), pre, "Flt Type", mod, filterChoices, 0.0f);
        t.cont (id (V::cutoff), pre, "Cutoff", mod, 0.0f, 1.0f, 0.75f, Unit::cutoffHz);
        t.cont (id (V::reso), pre, "Reso", mod, 0.0f, 1.0f, 0.1f, Unit::percent);
        t.cont (id (V::fltEnv), pre, "Flt Env", mod, -1.0f, 1.0f, 0.0f, Unit::bipolar);
        t.cont (id (V::fltDecay), pre, "Flt Decay", mod, 0.0f, 1.0f, 0.4f, Unit::envMs);
        t.cont (id (V::attack), pre, "Attack Time", mod, 0.0f, 1.0f, 0.0f, Unit::attackMs);
        t.cont (id (V::gate), pre, "Gate", mod, 0.0f, 1.0f, 0.0f, Unit::gateMs);
        t.cont (id (V::velSens), pre, "Vel Sens", mod, 0.0f, 1.0f, 1.0f, Unit::percent);
        t.cont (id (V::accent), pre, "Accent", mod, 0.0f, 1.0f, 0.5f, Unit::percent);
        t.cont (id (V::reverb), pre, "Reverb", mod, 0.0f, 1.0f, 0.0f, Unit::percent);
        t.cont (id (V::delay), pre, "Delay", mod, 0.0f, 1.0f, 0.0f, Unit::percent);
        t.cont (id (V::duck), pre, "Duck", mod, 0.0f, 1.0f, 0.0f, Unit::percent);
        FOUNDRY_CHOICE (t, id (V::choke), pre, "Choke", mod, chokeChoices, (float) s.choke);
        t.cont (id (V::muteDecay), pre, "Mute Decay", mod, 0.0f, 1.0f, 0.0f, Unit::muteMs);
        FOUNDRY_CHOICE (t, id (V::link), pre, "Link", mod, voiceChoices, 0.0f);
        t.cont (id (V::nudge), pre, "Nudge", mod, 0.0f, 80.0f, 0.0f, Unit::ms);
        t.cont (id (V::prob), pre, "Prob", mod, 0.0f, 1.0f, 1.0f, Unit::percent);
        t.cont (id (V::flam), pre, "Flam", mod, 0.0f, 60.0f, 0.0f, Unit::msOff);
        FOUNDRY_CHOICE (t, id (V::subStep), pre, "Sub-Step", mod, subStepChoices, 0.0f);
        FOUNDRY_CHOICE (t, id (V::cycle), pre, "Cycle", mod, cycleChoices, 0.0f);
        FOUNDRY_CHOICE (t, id (V::lfoTarget), pre, "LFO Target", mod, lfoTargetChoices, 0.0f);
        FOUNDRY_CHOICE (t, id (V::lfoWave), pre, "LFO Wave", mod, lfoWaveChoices, 0.0f);
        FOUNDRY_CHOICE (t, id (V::lfoRate), pre, "LFO Rate", mod, lfoRateChoices, 3.0f);
        t.cont (id (V::lfoDepth), pre, "LFO Depth", mod, -1.0f, 1.0f, 0.0f, Unit::bipolar);
    }

    const char* fx = "Effects";
    t.cont (M::revLevel, nullptr, "Reverb Level", fx, 0.0f, 1.0f, 0.5f, Unit::percent);
    t.cont (M::revTime, nullptr, "Reverb Time", fx, 0.0f, 1.0f, 0.4f, Unit::revTime);
    t.cont (M::revPreDelay, nullptr, "Reverb Pre-Delay", fx, 0.0f, 200.0f, 10.0f, Unit::ms);
    t.cont (M::revTone, nullptr, "Reverb Tone", fx, 0.0f, 1.0f, 0.5f, Unit::percent);
    t.cont (M::revLowCut, nullptr, "Reverb Low Cut", fx, 0.0f, 1.0f, 0.4f, Unit::lowCutHz);
    t.cont (M::revMod, nullptr, "Reverb Mod", fx, 0.0f, 1.0f, 0.2f, Unit::percent);
    t.cont (M::dlyLevel, nullptr, "Delay Level", fx, 0.0f, 1.0f, 0.5f, Unit::percent);
    FOUNDRY_CHOICE (t, M::dlyTime, nullptr, "Delay Time", fx, dlyTimeChoices, 7.0f);
    t.cont (M::dlyFeedback, nullptr, "Delay Feedback", fx, 0.0f, 0.95f, 0.35f, Unit::percent);
    t.cont (M::dlyTone, nullptr, "Delay Tone", fx, 0.0f, 1.0f, 0.6f, Unit::percent);
    FOUNDRY_CHOICE (t, M::dlyMode, nullptr, "Delay Mode", fx, dlyModeChoices, 1.0f);
    t.cont (M::dlyToRev, nullptr, "Delay > Reverb", fx, 0.0f, 1.0f, 0.0f, Unit::percent);
    FOUNDRY_CHOICE (t, M::scSource, nullptr, "SC Source", fx, voiceChoices, 1.0f);
    t.cont (M::scRelease, nullptr, "SC Release", fx, 0.0f, 1.0f, 0.45f, Unit::releaseMs);
    t.cont (M::comp, nullptr, "Comp", fx, 0.0f, 1.0f, 0.0f, Unit::percent);
    t.cont (M::compAttack, nullptr, "Comp Attack", fx, 0.0f, 1.0f, 0.5f, Unit::compAtkMs);
    t.cont (M::compRelease, nullptr, "Comp Release", fx, 0.0f, 1.0f, 0.45f, Unit::releaseMs);
    t.cont (M::compMix, nullptr, "Comp Mix", fx, 0.0f, 1.0f, 1.0f, Unit::percent);
    FOUNDRY_CHOICE (t, M::compKey, nullptr, "Comp Key", fx, compKeyChoices, 0.0f);
    t.cont (M::afxFilter, nullptr, "Analog Filter", fx, -1.0f, 1.0f, 0.0f, Unit::afxFilter);
    t.cont (M::afxReso, nullptr, "Analog Reso", fx, 0.0f, 1.0f, 0.2f, Unit::percent);
    t.cont (M::afxDrive, nullptr, "Analog Drive", fx, 0.0f, 1.0f, 0.0f, Unit::percent);
    FOUNDRY_CHOICE (t, M::clip, nullptr, "Clip", kit, clipChoices, 1.0f);
    FOUNDRY_CHOICE (t, M::keyMap, nullptr, "Key Map", kit, keyMapChoices, 0.0f);
    FOUNDRY_CHOICE (t, M::keysVoice, nullptr, "Keys Voice", kit, voiceChoices, 0.0f);
    FOUNDRY_CHOICE (t, M::step, nullptr, "Step", kit, stepChoices, 2.0f);
    FOUNDRY_CHOICE (t, M::velCurve, nullptr, "Vel Curve", kit, velCurveChoices, 1.0f);
    t.cont (M::humanize, nullptr, "Humanize", kit, 0.0f, 1.0f, 0.0f, Unit::percent);
    return t;
}
#undef FOUNDRY_CHOICE

// Built once, at the entry's init (or first use).
inline const ParamTable& table()
{
    static const ParamTable t = buildTable();
    return t;
}
inline int paramIndex (uint32_t id) { return id < idLimit ? table().row[id] : -1; }

inline float clampParam (const Param& p, float v)
{
    if (! (v >= p.min)) v = p.min; // NaN too
    if (v > p.max) v = p.max;
    return p.kind == Kind::continuous ? v : std::round (v);
}

inline void formatParam (const Param& p, float v, char* text, uint32_t size)
{
    v = clampParam (p, v);
    auto time = [&] (float ms) {
        if (ms < 10.0f) std::snprintf (text, size, "%.1f ms", ms);
        else if (ms < 1000.0f) std::snprintf (text, size, "%.0f ms", ms);
        else std::snprintf (text, size, "%.2f s", ms / 1000.0f);
    };
    auto hz = [&] (float f) {
        if (f < 1000.0f) std::snprintf (text, size, "%.0f Hz", f);
        else std::snprintf (text, size, "%.2f kHz", f / 1000.0f);
    };
    switch (p.unit)
    {
        case Unit::percent: std::snprintf (text, size, "%.0f %%", v * 100.0f); break;
        case Unit::bipolar: std::snprintf (text, size, "%+.0f %%", v * 100.0f); break;
        case Unit::semitones: std::snprintf (text, size, "%+.2f st", v); break;
        case Unit::db:
            if (v <= silentDb) std::snprintf (text, size, "-inf dB");
            else std::snprintf (text, size, "%+.1f dB", v);
            break;
        case Unit::pan:
            if (std::fabs (v) < 0.005f) std::snprintf (text, size, "C");
            else std::snprintf (text, size, "%c%.0f", v < 0.0f ? 'L' : 'R', std::fabs (v) * 100.0f);
            break;
        case Unit::choice: std::snprintf (text, size, "%s", p.choices[(int) v]); break;
        case Unit::plainInt: std::snprintf (text, size, "%d", (int) v); break;
        case Unit::ratio: std::snprintf (text, size, "%.2f", v); break;
        case Unit::ms: time (v); break;
        case Unit::msOff:
            if (v <= 0.0f) std::snprintf (text, size, "Off");
            else time (v);
            break;
        case Unit::attackMs: time (attackMs (v)); break;
        case Unit::gateMs:
            if (v <= 0.0f) std::snprintf (text, size, "Off");
            else time (gateMs (v));
            break;
        case Unit::envMs: time (envMs (v)); break;
        case Unit::chokeMs: time (chokeMs (v)); break;
        case Unit::muteMs:
            if (v <= 0.0f) std::snprintf (text, size, "Kit");
            else time (chokeMs (v));
            break;
        case Unit::releaseMs: time (releaseMs (v)); break;
        case Unit::revTime: time (revSeconds (v) * 1000.0f); break;
        case Unit::compAtkMs: time (compAttackMs (v)); break;
        case Unit::cutoffHz: hz (cutoffHz (v)); break;
        case Unit::lowCutHz: hz (lowCutHz (v)); break;
        case Unit::afxFilter:
            if (std::fabs (v) < 0.01f) std::snprintf (text, size, "Off");
            else
            {
                const float f = v < 0.0f ? afxLowPassHz (v) : afxHighPassHz (v);
                char t[24];
                if (f < 1000.0f) std::snprintf (t, sizeof (t), "%.0f Hz", f);
                else std::snprintf (t, sizeof (t), "%.2f kHz", f / 1000.0f);
                std::snprintf (text, size, "%s %s", v < 0.0f ? "LP" : "HP", t);
            }
            break;
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
    if (strcasecmp (text, "Off") == 0 && (p.unit == Unit::msOff || p.unit == Unit::gateMs || p.unit == Unit::afxFilter))
        return v = 0.0f, true;
    if (p.unit == Unit::muteMs && strcasecmp (text, "Kit") == 0)
        return v = 0.0f, true;
    if (p.unit == Unit::db && strncasecmp (text, "-inf", 4) == 0)
        return v = p.min, true;
    if (p.unit == Unit::pan && (strcasecmp (text, "C") == 0))
        return v = 0.0f, true;
    const char* at = text;
    float sign = 1.0f;
    if (p.unit == Unit::pan && (*at == 'L' || *at == 'l' || *at == 'R' || *at == 'r'))
        sign = (*at == 'L' || *at == 'l') ? -1.0f : 1.0f, ++at;
    if (p.unit == Unit::afxFilter && (strncasecmp (at, "LP", 2) == 0 || strncasecmp (at, "HP", 2) == 0))
        sign = (at[0] == 'L' || at[0] == 'l') ? -1.0f : 1.0f, at += 2;
    char* end = nullptr;
    double x = std::strtod (at, &end);
    if (end == at)
        return false;
    while (*end == ' ')
        ++end;
    const bool kilo = (*end == 'k' || *end == 'K');
    const bool seconds = (end[0] == 's' || end[0] == 'S');
    const double ms = seconds ? x * 1000.0 : x;
    const double hz = kilo ? x * 1000.0 : x;
    auto inv = [] (double value, double base, double span) { return (float) (std::log (std::fmax (value, 1e-9) / base) / std::log (span)); };
    switch (p.unit)
    {
        case Unit::percent: v = (float) (x / 100.0); break;
        case Unit::bipolar: v = (float) (x / 100.0); break;
        case Unit::pan: v = sign * (float) (x / 100.0); break;
        case Unit::ms:
        case Unit::msOff: v = (float) ms; break;
        case Unit::attackMs: v = (float) std::sqrt (std::fmax (ms, 0.0) / 100.0); break;
        case Unit::gateMs: v = ms <= 0.0 ? 0.0f : std::fmax (inv (ms, 5.0, 400.0), 0.001f); break;
        case Unit::envMs: v = inv (ms, 5.0, 400.0); break;
        case Unit::chokeMs: v = inv (ms, 1.0, 200.0); break;
        case Unit::muteMs: v = std::fmax (inv (ms, 1.0, 200.0), 0.001f); break;
        case Unit::releaseMs: v = inv (ms, 20.0, 50.0); break;
        case Unit::revTime: v = inv (ms / 1000.0, 0.3, 40.0); break;
        case Unit::compAtkMs: v = inv (ms, 0.1, 300.0); break;
        case Unit::cutoffHz: v = inv (hz, 20.0, 1000.0); break;
        case Unit::lowCutHz: v = inv (hz, 20.0, 50.0); break;
        case Unit::afxFilter:
            if (sign < 0.0f) v = -inv (hz, 20000.0, 0.005);
            else v = inv (hz, 20.0, 400.0);
            break;
        default: v = (float) x; break;
    }
    v = clampParam (p, v);
    return true;
}
} // namespace foundry
