// hdslave Machine's parameters: one table for the CLAP side (names, ranges, text), the engine (what a value means)
// and the screen (each row's short name, its steps, whether it is drawn from the middle).
//
// Ids are fixed forever: a saved project keeps its values by id, and new parameters take new ids. Data A-H mean
// something else for each machine, as on the hardware this is in the style of: their CLAP names stay "Data A".."Data
// H" (hosts do not reread names) and their value text says what they are for the machine in use.
#pragma once

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cctype>
#include <strings.h>

#ifndef MACHINE_VERSION
#define MACHINE_VERSION "0.1.3"
#endif

namespace machine
{
// The machines, in the order the MACHINE parameter lists them: a saved project keeps the number, so new machines go
// at the end.
enum Kind : uint8_t { fmTone, bd, sd, cp, hh, cycb, efm, gnd, kit, terrain, phase, vox, modal, pluck, gendy, pulsar, organ, kick, kindCount };
constexpr uint8_t drumFirst = bd, drumCount = 7; // the drum machines, BD to GND
inline const char* const kindNames[kindCount] = { "FM TONE", "BD", "SD", "CP", "HH", "CY/CB", "EFM DRUM", "GND", "KIT",
                                                  "TERRAIN", "PHASE", "VOX", "MODAL", "PLUCK", "GENDY", "PULSAR", "ORGAN", "KICK" };
inline const char* const kindLongNames[kindCount] = { "FM Tone", "Bass Drum", "Snare Drum", "Clap", "Hi-Hat", "Cymbal / Cowbell",
                                                      "EFM Drum", "Ground", "Kit", "Wave Terrain", "Vector Phaseshaping", "VOSIM",
                                                      "Modal", "Plucked String", "Dynamic Stochastic", "Pulsar", "Organ", "Resonator Kick" };
// What a KIT slot may be, in its MACH list's order: the drums, then the pitched machines that also strike.
inline constexpr uint8_t slotKinds[] = { bd, sd, cp, hh, cycb, efm, gnd, modal, pluck, vox, pulsar, kick };
constexpr uint8_t slotKindCount = sizeof (slotKinds) / sizeof (slotKinds[0]);
inline const char* const slotNames[slotKindCount] = { "BD", "SD", "CP", "HH", "CY/CB", "EFM DRUM", "GND", "MODAL", "PLUCK", "VOX", "PULSAR", "KICK" };
constexpr uint8_t slotIndexOf (uint8_t kind)
{
    for (uint8_t i = 0; i < slotKindCount; ++i)
        if (slotKinds[i] == kind) return i;
    return 0;
}
// Tonal and struck machines follow the note, eight at once, with amp release on note-off. In KIT, struck
// machines play one-shots and tonal ones still hear note-off. Drums play one hit at a time and ignore note-off.
inline bool tonal (uint8_t k) { return k == fmTone || k == terrain || k == phase || k == vox || k == gendy || k == pulsar || k == organ; }
inline bool struck (uint8_t k) { return k == modal || k == pluck; }
inline bool drum (uint8_t k) { return (k >= bd && k <= gnd) || k == kick; } // KICK: one at a time, but on the note
constexpr int kickBaseKey = 36; // KICK on its own: PITCH at C2 (a drum track's first pad), the note moving it from there
constexpr uint32_t dataCount = 10; // Data A-J: I and J only for machines with more than eight (KICK)
constexpr int slotBaseKey = 48; // a pitched machine in a KIT slot plays C3, moved by its TUNE
constexpr uint32_t kitSlots = 8;
constexpr int kitFirstKey = 36; // KIT: notes 36-43 play slots 1-8 (a Melodee drum track's first eight pads)

// What a value is, for its text and for the engine.
enum class Unit : uint8_t {
    unit,     // 0..100
    bipolar,  // -100..+100 from a 0..1 value centred on 0.5
    signedUnit, // -100..+100 from a -1..1 value
    hz,       // lo..hi Hz
    seconds,  // 0..10 s (time(), 0 at the bottom)
    decayInf, // as seconds, INF at the top
    ratio,    // FM ratios, from ratioTable
    octaves,  // 0..hi octaves
    count,    // a number lo..hi in steps
    choice,   // names
    semis,    // lo..hi semitones in steps: "+12st"
    ratioX,   // lo..hi times, exponential: "4.00x"
    rateHz,   // 0..hi Hz, x squared: "5.00Hz"
    number,   // lo..hi, plain: "1.50"
    vowel,    // A, E, I, O, U and between: "A>E"
    material, // MODAL's: STRING, BAR, DRUM, BELL and between
};
enum class Curve : uint8_t { linear, exp };

// FM ratios, the steps RATIO C, A and B choose from.
inline constexpr float ratioTable[] = { 0.25f, 0.5f, 0.75f, 1.0f, 1.25f, 1.5f, 2.0f, 2.5f, 3.0f, 3.5f, 4.0f, 5.0f,
                                        6.0f,  7.0f, 8.0f,  9.0f, 10.0f, 11.0f, 12.0f, 13.0f, 14.0f, 15.0f, 16.0f };
constexpr uint8_t ratioCount = sizeof (ratioTable) / sizeof (ratioTable[0]);

inline const char* const hhModes[] = { "CLOSED", "OPEN" };
inline const char* const cycbModes[] = { "CY", "CB" };
inline const char* const gndSources[] = { "SINE", "NOISE", "IMPULSE" };
inline const char* const surfaces[] = { "RIPPLE", "SADDLE", "LATTICE", "DUNE", "KNOT", "CRATER" };
inline const char* const masks[] = { "ALL", "1/2", "2/3", "3/4", "5/8", "RND 50", "RND 25", "EUCLID" };
constexpr float countDef (int value, int lo, int hi) { return (float) (value - lo) / (float) (hi - lo); }

// One Data knob as a machine uses it.
struct DataSpec
{
    const char* name;  // as the screen lists it, capitals, at most 8 letters
    Unit unit;
    float lo, hi;
    Curve curve;
    float def;         // 0..1
    uint8_t steps;     // 0 continuous; else the number of choices (round (x * (steps - 1)))
    const char* const* choices;
    bool used() const { return name != nullptr; }
    bool bipolar() const { return unit == Unit::bipolar; }
};

constexpr DataSpec none { nullptr, Unit::unit, 0, 1, Curve::linear, 0, 0, nullptr };
constexpr float ratioDef (float r)
{
    for (uint8_t i = 0; i < ratioCount; ++i)
        if (ratioTable[i] == r)
            return (float) i / (float) (ratioCount - 1);
    return 0.0f;
}

// Each machine's Data A-H (unused ones none). Defaults make a good sound of each.
inline constexpr DataSpec dataSpecs[kindCount][dataCount] = {
    { // FM TONE: four operators (C, A, B1, B2) in one of eight algorithms
      { "ALGO", Unit::count, 1, 8, Curve::linear, 2.0f / 7.0f, 8, nullptr },
      { "RATIO C", Unit::ratio, 0, 0, Curve::linear, ratioDef (1.0f), ratioCount, nullptr },
      { "RATIO A", Unit::ratio, 0, 0, Curve::linear, ratioDef (2.0f), ratioCount, nullptr },
      { "RATIO B", Unit::ratio, 0, 0, Curve::linear, ratioDef (1.0f), ratioCount, nullptr },
      { "HARM", Unit::bipolar, 0, 1, Curve::linear, 0.5f, 0, nullptr },
      { "DETUNE", Unit::unit, 0, 1, Curve::linear, 0.15f, 0, nullptr },
      { "FDBK", Unit::unit, 0, 1, Curve::linear, 0.0f, 0, nullptr },
      { "MIX", Unit::unit, 0, 1, Curve::linear, 0.0f, 0, nullptr } },
    { // BD: a swept sine, a click, drive, FM and noise
      { "TUNE", Unit::hz, 28, 140, Curve::exp, 0.4f, 0, nullptr },
      { "DECAY", Unit::seconds, 0, 0, Curve::linear, 0.65f, 0, nullptr },
      { "SWEEP", Unit::octaves, 0, 5, Curve::linear, 0.45f, 0, nullptr },
      { "SWP TIME", Unit::seconds, 0, 0, Curve::linear, 0.4f, 0, nullptr },
      { "CLICK", Unit::unit, 0, 1, Curve::linear, 0.35f, 0, nullptr },
      { "DRIVE", Unit::unit, 0, 1, Curve::linear, 0.2f, 0, nullptr },
      { "FM", Unit::unit, 0, 1, Curve::linear, 0.0f, 0, nullptr },
      { "NOISE", Unit::unit, 0, 1, Curve::linear, 0.0f, 0, nullptr } },
    { // SD: two tuned partials and filtered noise
      { "TUNE", Unit::hz, 90, 450, Curve::exp, 0.4f, 0, nullptr },
      { "TONE DEC", Unit::seconds, 0, 0, Curve::linear, 0.52f, 0, nullptr },
      { "NOIS DEC", Unit::seconds, 0, 0, Curve::linear, 0.6f, 0, nullptr },
      { "SNAPPY", Unit::unit, 0, 1, Curve::linear, 0.6f, 0, nullptr },
      { "BODY", Unit::unit, 0, 1, Curve::linear, 0.6f, 0, nullptr },
      { "FILTER", Unit::hz, 600, 9000, Curve::exp, 0.5f, 0, nullptr },
      { "DRIVE", Unit::unit, 0, 1, Curve::linear, 0.15f, 0, nullptr },
      none },
    { // CP: bursts of band-passed noise, then a tail
      { "BURSTS", Unit::count, 1, 8, Curve::linear, 3.0f / 7.0f, 8, nullptr },
      { "SPREAD", Unit::seconds, 0, 0, Curve::linear, 0.3f, 0, nullptr },
      { "DECAY", Unit::seconds, 0, 0, Curve::linear, 0.58f, 0, nullptr },
      { "TONE", Unit::hz, 500, 5000, Curve::exp, 0.45f, 0, nullptr },
      { "FILTER", Unit::unit, 0, 1, Curve::linear, 0.35f, 0, nullptr },
      { "ROOM", Unit::unit, 0, 1, Curve::linear, 0.3f, 0, nullptr },
      none, none },
    { // HH: six square waves at metallic ratios and noise, high-passed
      { "MODE", Unit::choice, 0, 1, Curve::linear, 0.0f, 2, hhModes },
      { "TONE", Unit::bipolar, 0, 1, Curve::linear, 0.5f, 0, nullptr },
      { "DECAY", Unit::unit, 0, 1, Curve::linear, 0.4f, 0, nullptr },
      { "FILTER", Unit::hz, 2000, 14000, Curve::exp, 0.4f, 0, nullptr },
      { "METAL", Unit::unit, 0, 1, Curve::linear, 0.7f, 0, nullptr },
      { "NOISE", Unit::unit, 0, 1, Curve::linear, 0.3f, 0, nullptr },
      none, none },
    { // CY/CB: a cymbal's metal or a cowbell's two tones
      { "MODE", Unit::choice, 0, 1, Curve::linear, 0.0f, 2, cycbModes },
      { "TONE", Unit::bipolar, 0, 1, Curve::linear, 0.5f, 0, nullptr },
      { "DECAY", Unit::unit, 0, 1, Curve::linear, 0.55f, 0, nullptr },
      { "RING", Unit::unit, 0, 1, Curve::linear, 0.5f, 0, nullptr },
      { "FILTER", Unit::hz, 1000, 12000, Curve::exp, 0.5f, 0, nullptr },
      none, none, none },
    { // EFM DRUM: two-operator FM percussion
      { "PITCH", Unit::hz, 30, 2000, Curve::exp, 0.35f, 0, nullptr },
      { "RATIO", Unit::ratio, 0, 0, Curve::linear, ratioDef (1.5f), ratioCount, nullptr },
      { "INDEX", Unit::unit, 0, 1, Curve::linear, 0.4f, 0, nullptr },
      { "P DECAY", Unit::seconds, 0, 0, Curve::linear, 0.38f, 0, nullptr },
      { "I DECAY", Unit::seconds, 0, 0, Curve::linear, 0.45f, 0, nullptr },
      { "A DECAY", Unit::seconds, 0, 0, Curve::linear, 0.62f, 0, nullptr },
      { "SWEEP", Unit::octaves, 0, 5, Curve::linear, 0.3f, 0, nullptr },
      { "NOISE", Unit::unit, 0, 1, Curve::linear, 0.0f, 0, nullptr } },
    { // GND: the plain sources
      { "SOURCE", Unit::choice, 0, 2, Curve::linear, 0.0f, 3, gndSources },
      { "PITCH", Unit::hz, 20, 5000, Curve::exp, 0.3f, 0, nullptr },
      { "DECAY", Unit::seconds, 0, 0, Curve::linear, 0.6f, 0, nullptr },
      { "SWEEP", Unit::octaves, 0, 5, Curve::linear, 0.0f, 0, nullptr },
      none, none, none, none },
    { none, none, none, none, none, none, none, none }, // KIT: its slots' Data are their own
    { // TERRAIN: a point orbits a surface; the height under it is the sound
      { "SURFACE", Unit::choice, 0, 5, Curve::linear, 0.4f, 6, surfaces },
      { "X", Unit::bipolar, 0, 1, Curve::linear, 0.62f, 0, nullptr },
      { "Y", Unit::bipolar, 0, 1, Curve::linear, 0.5f, 0, nullptr },
      { "WIDTH", Unit::unit, 0, 1, Curve::linear, 0.6f, 0, nullptr },
      { "HEIGHT", Unit::unit, 0, 1, Curve::linear, 0.4f, 0, nullptr },
      { "WARP", Unit::ratioX, 0.5f, 6.0f, Curve::exp, 0.442f, 0, nullptr },
      { "SPIN", Unit::rateHz, 0, 8, Curve::linear, 0.15f, 0, nullptr },
      { "DRIFT", Unit::unit, 0, 1, Curve::linear, 0.2f, 0, nullptr } },
    { // PHASE: a cosine read through a phase bent at the point (D, V)
      { "D", Unit::unit, 0, 1, Curve::linear, 0.5f, 0, nullptr },
      { "V", Unit::number, 0, 6, Curve::linear, 0.25f, 0, nullptr },
      { "V ENV", Unit::bipolar, 0, 1, Curve::linear, 0.62f, 0, nullptr },
      { "ENV DEC", Unit::seconds, 0, 0, Curve::linear, 0.5f, 0, nullptr },
      { "TWIN", Unit::unit, 0, 1, Curve::linear, 0.25f, 0, nullptr },
      { "SUB", Unit::unit, 0, 1, Curve::linear, 0.3f, 0, nullptr },
      { "FOLD", Unit::unit, 0, 1, Curve::linear, 0.0f, 0, nullptr },
      { "GRIT", Unit::unit, 0, 1, Curve::linear, 0.0f, 0, nullptr } },
    { // VOX: VOSIM, bursts of decaying sin^2 pulses each period, two formants for a vowel
      { "TUNE", Unit::semis, -24, 24, Curve::linear, 0.5f, 49, nullptr },
      { "VOWEL", Unit::vowel, 0, 1, Curve::linear, 0.0f, 0, nullptr },
      { "SHIFT", Unit::ratioX, 0.5f, 2.0f, Curve::exp, 0.5f, 0, nullptr },
      { "PULSES", Unit::count, 1, 8, Curve::linear, countDef (3, 1, 8), 8, nullptr },
      { "P DECAY", Unit::unit, 0, 1, Curve::linear, 0.5f, 0, nullptr },
      { "F ENV", Unit::bipolar, 0, 1, Curve::linear, 0.5f, 0, nullptr },
      { "F DEC", Unit::seconds, 0, 0, Curve::linear, 0.5f, 0, nullptr },
      { "LENGTH", Unit::decayInf, 0, 0, Curve::linear, 1.0f, 0, nullptr } },
    { // MODAL: a struck object, twelve resonating modes
      { "TUNE", Unit::semis, -24, 24, Curve::linear, 0.5f, 49, nullptr },
      { "MATERIAL", Unit::material, 0, 1, Curve::linear, 0.33f, 0, nullptr },
      { "BRIGHT", Unit::unit, 0, 1, Curve::linear, 0.5f, 0, nullptr },
      { "DAMP", Unit::unit, 0, 1, Curve::linear, 0.45f, 0, nullptr },
      { "POSITION", Unit::unit, 0, 1, Curve::linear, 0.3f, 0, nullptr },
      { "MALLET", Unit::unit, 0, 1, Curve::linear, 0.5f, 0, nullptr },
      { "NOISE", Unit::unit, 0, 1, Curve::linear, 0.1f, 0, nullptr },
      { "SCATTER", Unit::unit, 0, 1, Curve::linear, 0.0f, 0, nullptr } },
    { // PLUCK: a plucked string, a delay line round a damping filter
      { "TUNE", Unit::semis, -24, 24, Curve::linear, 0.5f, 49, nullptr },
      { "DECAY", Unit::seconds, 0, 0, Curve::linear, 0.81f, 0, nullptr },
      { "BRIGHT", Unit::unit, 0, 1, Curve::linear, 0.6f, 0, nullptr },
      { "PICK", Unit::unit, 0, 1, Curve::linear, 0.6f, 0, nullptr },
      { "POSITION", Unit::unit, 0, 1, Curve::linear, 0.2f, 0, nullptr },
      { "STIFF", Unit::unit, 0, 1, Curve::linear, 0.0f, 0, nullptr },
      { "BODY", Unit::unit, 0, 1, Curve::linear, 0.3f, 0, nullptr },
      { "TENSION", Unit::bipolar, 0, 1, Curve::linear, 0.5f, 0, nullptr } },
    { // GENDY: a waveform of breakpoints that wander, its pitch held
      { "POINTS", Unit::count, 3, 16, Curve::linear, countDef (8, 3, 16), 14, nullptr },
      { "AMP WALK", Unit::unit, 0, 1, Curve::linear, 0.15f, 0, nullptr },
      { "TIMEWALK", Unit::unit, 0, 1, Curve::linear, 0.1f, 0, nullptr },
      { "WILD", Unit::unit, 0, 1, Curve::linear, 0.2f, 0, nullptr },
      { "LOCK", Unit::unit, 0, 1, Curve::linear, 1.0f, 0, nullptr },
      { "SMOOTH", Unit::unit, 0, 1, Curve::linear, 0.5f, 0, nullptr },
      { "BOUNDS", Unit::unit, 0, 1, Curve::linear, 0.8f, 0, nullptr },
      { "RATE", Unit::unit, 0, 1, Curve::linear, 0.6f, 0, nullptr } },
    { // PULSAR: pulsarets at a rate between rhythm and tone, some masked out
      { "TUNE", Unit::semis, -24, 24, Curve::linear, 0.5f, 49, nullptr },
      { "FORMANT", Unit::ratioX, 1.0f, 16.0f, Curve::exp, 0.396f, 0, nullptr },
      { "DENSITY", Unit::unit, 0, 1, Curve::linear, 0.0f, 0, nullptr },
      { "SHAPE", Unit::unit, 0, 1, Curve::linear, 0.0f, 0, nullptr },
      { "ENVELOPE", Unit::unit, 0, 1, Curve::linear, 0.5f, 0, nullptr },
      { "MASK", Unit::choice, 0, 7, Curve::linear, 0.0f, 8, masks },
      { "JITTER", Unit::unit, 0, 1, Curve::linear, 0.0f, 0, nullptr },
      { "LENGTH", Unit::decayInf, 0, 0, Curve::linear, 1.0f, 0, nullptr } },
    { // ORGAN: eight drawbars, 0-8 each
      { "16'", Unit::count, 0, 8, Curve::linear, 1.0f, 9, nullptr },
      { "5 1/3'", Unit::count, 0, 8, Curve::linear, 1.0f, 9, nullptr },
      { "8'", Unit::count, 0, 8, Curve::linear, 1.0f, 9, nullptr },
      { "4'", Unit::count, 0, 8, Curve::linear, 0.0f, 9, nullptr },
      { "2 2/3'", Unit::count, 0, 8, Curve::linear, 0.0f, 9, nullptr },
      { "2'", Unit::count, 0, 8, Curve::linear, 0.0f, 9, nullptr },
      { "1 3/5'", Unit::count, 0, 8, Curve::linear, 0.0f, 9, nullptr },
      { "1 1/3'", Unit::count, 0, 8, Curve::linear, 0.0f, 9, nullptr } },
    { // KICK: a resonator struck by a pulse, its pitch bent down, through an amp with drive and tone (all ten
      // controls of the compact analog kick it is styled on)
      { "PITCH", Unit::hz, 30, 250, Curve::exp, 0.35f, 0, nullptr },
      { "BEND", Unit::octaves, 0, 4, Curve::linear, 0.45f, 0, nullptr },
      { "TIME", Unit::seconds, 0, 0, Curve::linear, 0.42f, 0, nullptr },
      { "DECAY", Unit::seconds, 0, 0, Curve::linear, 0.66f, 0, nullptr },
      { "ATTACK", Unit::seconds, 0, 0, Curve::linear, 0.0f, 0, nullptr },
      { "COLOUR", Unit::unit, 0, 1, Curve::linear, 0.3f, 0, nullptr },
      { "PULSE", Unit::unit, 0, 1, Curve::linear, 0.5f, 0, nullptr },
      { "DRIVE", Unit::unit, 0, 1, Curve::linear, 0.25f, 0, nullptr },
      { "TONE", Unit::hz, 150, 18000, Curve::exp, 0.75f, 0, nullptr },
      { "ACCENT", Unit::unit, 0, 1, Curve::linear, 0.5f, 0, nullptr } },
};
inline const DataSpec& dataSpec (uint8_t machineKind, uint32_t k) { return dataSpecs[machineKind < kindCount ? machineKind : 0][k < dataCount ? k : 0]; }

// --- Times and curves ------------------------------------------------------------------------------------------
// Times: 0 at the bottom, then exponential to 10 s at the top (1 ms near a fifth of the turn, 100 ms near the middle).
inline float timeSeconds (float x) { return x <= 0.0f ? 0.0f : 0.0005f * (std::pow (20000.0f, x > 1.0f ? 1.0f : x) - 1.0f); }
inline float timeKnob (float s) { return s <= 0.0f ? 0.0f : std::log (s / 0.0005f + 1.0f) / std::log (20000.0f); }
constexpr float infAbove = 0.995f; // AMP DECAY: INF from here up

// A Data knob's value in its units (Hz, seconds, octaves, the ratio, a count, a choice's number, or 0..1).
inline float dataValue (const DataSpec& d, float x)
{
    x = x < 0.0f ? 0.0f : x > 1.0f ? 1.0f : x;
    if (d.steps > 0)
    {
        const int i = (int) std::lround (x * (float) (d.steps - 1));
        if (d.unit == Unit::ratio) return ratioTable[i];
        if (d.unit == Unit::count || d.unit == Unit::semis) return d.lo + (float) i;
        return (float) i;
    }
    switch (d.unit)
    {
        case Unit::hz: return d.curve == Curve::exp ? d.lo * std::pow (d.hi / d.lo, x) : d.lo + (d.hi - d.lo) * x;
        case Unit::seconds:
        case Unit::decayInf: return timeSeconds (x);
        case Unit::octaves:
        case Unit::number: return d.lo + (d.hi - d.lo) * x;
        case Unit::ratioX: return d.lo * std::pow (d.hi / d.lo, x);
        case Unit::rateHz: return d.hi * x * x;
        case Unit::bipolar: return x * 2.0f - 1.0f;
        default: return x;
    }
}

// --- The parameters --------------------------------------------------------------------------------------------
namespace ParamId
{
enum : uint32_t
{
    machine = 0,
    dataA, dataB, dataC, dataD, dataE, dataF, dataG, dataH,                    // 1-8
    fmAAttack, fmADecay, fmAEnd, fmALevel, fmBAttack, fmBDecay, fmBEnd, fmBLevel, // 9-16 (FM TONE alone)
    filterFreq, filterReso, filterType, filterEnv, filterAttack, filterDecay,    // 17-22
    ampAttack, ampHold, ampDecay, ampRelease, ampDrive, volume,                  // 23-28
    lfoSpeed, lfoDepth, lfoWave, lfoDest,                                        // 29-32
    dataI, dataJ,                                                                // 33-34 (KICK's ninth and tenth)
    sharedEnd,
    // KIT: slot s (0-7) at kitBase + s * kitStride: its machine, Data A-H, level, pan, then Data I and J.
    kitBase = 100, kitStride = 16,
    kitMachine = 0, kitData = 1, kitLevel = 9, kitPan = 10, kitDataI = 11, kitDataJ = 12, kitFields = 13,
    end = kitBase + kitSlots * kitStride,
};
}
constexpr uint32_t kitId (uint32_t slot, uint32_t field) { return ParamId::kitBase + slot * ParamId::kitStride + field; }
// Data k (0-9, A-J) as a parameter: the main machine's, or a KIT slot's.
constexpr uint32_t dataId (uint32_t k) { return k < 8 ? ParamId::dataA + k : ParamId::dataI + (k - 8); }
constexpr uint32_t kitDataId (uint32_t slot, uint32_t k) { return kitId (slot, k < 8 ? ParamId::kitData + k : ParamId::kitDataI + (k - 8)); }
// Which Data (0-9) a parameter is, or -1.
constexpr int dataOf (uint32_t id)
{
    if (id >= ParamId::dataA && id <= ParamId::dataH) return (int) (id - ParamId::dataA);
    if (id == ParamId::dataI || id == ParamId::dataJ) return 8 + (int) (id - ParamId::dataI);
    if (id >= ParamId::kitBase && id < ParamId::kitBase + 8 * ParamId::kitStride)
    {
        const uint32_t f = (id - ParamId::kitBase) % ParamId::kitStride;
        if (f >= ParamId::kitData && f < ParamId::kitData + 8) return (int) (f - ParamId::kitData);
        if (f == ParamId::kitDataI || f == ParamId::kitDataJ) return 8 + (int) (f - ParamId::kitDataI);
    }
    return -1;
}

enum class ParamKind : uint8_t { continuous, choice };
// How a parameter's own value reads (Data ones read through their machine's DataSpec instead).
enum class Text : uint8_t { machine, data, kitMachine, kitData, unit, signedUnit, freq, time, timeInf, choice, octavesEnv, lfoHz, volume };

inline const char* const filterTypes[] = { "LP", "HP", "BP" };
inline const char* const lfoWaves[] = { "TRI", "SAW", "SQR", "S+H", "SINE" };
inline const char* const lfoDests[] = { "PITCH", "FREQ", "AMP", "DATA A", "DATA B", "DATA C", "DATA D", "DATA E", "DATA F", "DATA G", "DATA H" };

struct Param
{
    uint32_t id;
    const char* name;   // the host's name
    const char* screen; // the screen's, capitals (Data and KIT ones come from their machine)
    const char* module;
    float min, max, def;
    ParamKind kind;
    Text text;
    const char* const* choices;
};

// Filter cutoff: 20 Hz to about 20 kHz, exponential.
inline float filterHz (float x) { return 20.0f * std::exp2 (x * 10.0f); }
inline float filterKnob (float hz) { return std::log2 (hz / 20.0f) / 10.0f; }
constexpr float filterEnvOctaves = 7.0f; // ENV at +-100 %
// LFO speed: 0.05 Hz to about 30 Hz.
inline float lfoHz (float x) { return 0.05f * std::exp2 (x * 9.25f); }
// VOLUME: the gain, 1 at its default (three quarters), +5 dB at the top.
inline float volumeGain (float x) { return (x / 0.75f) * (x / 0.75f); }

// The slots' machines and Data defaults: a kit to start from (kick, snare, clap, closed and open hats, cymbal, an
// FM tom, a ground tone).
inline constexpr uint8_t kitDefaultMachines[kitSlots] = { bd, sd, cp, hh, hh, cycb, efm, gnd };
inline float kitDataDefault (uint32_t slot, uint32_t k)
{
    const uint8_t m = kitDefaultMachines[slot];
    if (slot == 4 && k == 0) return 1.0f;  // the second HH open
    if (slot == 4 && k == 2) return 0.45f; // and longer
    if (slot == 6 && k == 0) return 0.18f; // the FM tom lower
    if (slot == 7 && k == 1) return 0.45f; // the ground tone higher
    return dataSpec (m, k).used() ? dataSpec (m, k).def : 0.0f;
}

struct ParamTable
{
    Param rows[ParamId::sharedEnd + kitSlots * ParamId::kitFields] {};
    uint32_t count = 0;
    char kitNames[kitSlots][ParamId::kitFields][24] {};
    char kitModules[kitSlots][8] {};
    ParamTable()
    {
        using namespace ParamId;
        auto add = [&] (uint32_t id, const char* name, const char* screen, const char* module, float lo, float hi, float def, ParamKind kind,
                        Text text, const char* const* choices = nullptr) {
            rows[count++] = { id, name, screen, module, lo, hi, def, kind, text, choices };
        };
        add (machine, "Machine", "MACHINE", "Machine", 0, kindCount - 1, fmTone, ParamKind::choice, Text::machine, kindNames);
        static const char* const dataNames[8] = { "Data A", "Data B", "Data C", "Data D", "Data E", "Data F", "Data G", "Data H" };
        for (uint32_t k = 0; k < 8; ++k)
            add (dataA + k, dataNames[k], nullptr, "Machine", 0, 1, dataSpec (fmTone, k).def, ParamKind::continuous, Text::data);
        add (fmAAttack, "FM A Attack", "A ATK", "FM Env", 0, 1, 0.0f, ParamKind::continuous, Text::time);
        add (fmADecay, "FM A Decay", "A DEC", "FM Env", 0, 1, timeKnob (0.6f), ParamKind::continuous, Text::time);
        add (fmAEnd, "FM A End", "A END", "FM Env", 0, 1, 0.25f, ParamKind::continuous, Text::unit);
        add (fmALevel, "FM A Level", "A LEV", "FM Env", 0, 1, 0.45f, ParamKind::continuous, Text::unit);
        add (fmBAttack, "FM B Attack", "B ATK", "FM Env", 0, 1, 0.0f, ParamKind::continuous, Text::time);
        add (fmBDecay, "FM B Decay", "B DEC", "FM Env", 0, 1, timeKnob (0.25f), ParamKind::continuous, Text::time);
        add (fmBEnd, "FM B End", "B END", "FM Env", 0, 1, 0.0f, ParamKind::continuous, Text::unit);
        add (fmBLevel, "FM B Level", "B LEV", "FM Env", 0, 1, 0.3f, ParamKind::continuous, Text::unit);
        add (filterFreq, "Filter Freq", "FREQ", "Filter", 0, 1, 1.0f, ParamKind::continuous, Text::freq);
        add (filterReso, "Filter Reso", "RESO", "Filter", 0, 1, 0.0f, ParamKind::continuous, Text::unit);
        add (filterType, "Filter Type", "TYPE", "Filter", 0, 2, 0, ParamKind::choice, Text::choice, filterTypes);
        add (filterEnv, "Filter Env", "ENV", "Filter", -1, 1, 0.0f, ParamKind::continuous, Text::octavesEnv);
        add (filterAttack, "Filter Attack", "ATTACK", "Filter", 0, 1, 0.0f, ParamKind::continuous, Text::time);
        add (filterDecay, "Filter Decay", "DECAY", "Filter", 0, 1, timeKnob (0.3f), ParamKind::continuous, Text::time);
        add (ampAttack, "Amp Attack", "ATTACK", "Amp", 0, 1, 0.0f, ParamKind::continuous, Text::time);
        add (ampHold, "Amp Hold", "HOLD", "Amp", 0, 1, 0.0f, ParamKind::continuous, Text::time);
        add (ampDecay, "Amp Decay", "DECAY", "Amp", 0, 1, 1.0f, ParamKind::continuous, Text::timeInf);
        add (ampRelease, "Amp Release", "RELEASE", "Amp", 0, 1, timeKnob (0.25f), ParamKind::continuous, Text::time);
        add (ampDrive, "Amp Drive", "DRIVE", "Amp", 0, 1, 0.0f, ParamKind::continuous, Text::unit);
        add (volume, "Volume", "VOLUME", "Amp", 0, 1, 0.75f, ParamKind::continuous, Text::volume);
        add (lfoSpeed, "LFO Speed", "SPEED", "LFO", 0, 1, 0.5f, ParamKind::continuous, Text::lfoHz);
        add (lfoDepth, "LFO Depth", "DEPTH", "LFO", -1, 1, 0.0f, ParamKind::continuous, Text::signedUnit);
        add (lfoWave, "LFO Wave", "WAVE", "LFO", 0, 4, 0, ParamKind::choice, Text::choice, lfoWaves);
        add (lfoDest, "LFO Dest", "DEST", "LFO", 0, 10, 0, ParamKind::choice, Text::choice, lfoDests);
        add (dataI, "Data I", nullptr, "Machine", 0, 1, 0.0f, ParamKind::continuous, Text::data);
        add (dataJ, "Data J", nullptr, "Machine", 0, 1, 0.0f, ParamKind::continuous, Text::data);
        for (uint32_t s = 0; s < kitSlots; ++s)
        {
            std::snprintf (kitModules[s], sizeof (kitModules[s]), "Kit %u", s + 1);
            auto name = [&] (uint32_t field, const char* what) {
                std::snprintf (kitNames[s][field], sizeof (kitNames[s][field]), "Kit %u %s", s + 1, what);
                return kitNames[s][field];
            };
            add (kitId (s, kitMachine), name (kitMachine, "Machine"), "MACH", kitModules[s], 0, slotKindCount - 1,
                 (float) slotIndexOf (kitDefaultMachines[s]), ParamKind::choice, Text::kitMachine, slotNames);
            for (uint32_t k = 0; k < 8; ++k)
                add (kitId (s, kitData + k), name (kitData + k, dataNames[k]), nullptr, kitModules[s], 0, 1, kitDataDefault (s, k),
                     ParamKind::continuous, Text::kitData);
            add (kitId (s, kitLevel), name (kitLevel, "Level"), "LEVEL", kitModules[s], 0, 1, 0.8f, ParamKind::continuous, Text::unit);
            add (kitId (s, kitPan), name (kitPan, "Pan"), "PAN", kitModules[s], -1, 1, 0.0f, ParamKind::continuous, Text::signedUnit);
            add (kitId (s, kitDataI), name (kitDataI, "Data I"), nullptr, kitModules[s], 0, 1, kitDataDefault (s, 8), ParamKind::continuous, Text::kitData);
            add (kitId (s, kitDataJ), name (kitDataJ, "Data J"), nullptr, kitModules[s], 0, 1, kitDataDefault (s, 9), ParamKind::continuous, Text::kitData);
        }
    }
};
inline const ParamTable params {};
inline const uint32_t paramCount = sizeof (ParamTable::rows) / sizeof (Param);

struct IndexOfId
{
    int16_t row[ParamId::end] {};
    IndexOfId()
    {
        for (auto& r : row) r = -1;
        for (uint32_t i = 0; i < params.count; ++i) row[params.rows[i].id] = (int16_t) i;
    }
};
inline const IndexOfId indexOfId {};
inline int paramIndex (uint32_t id) { return id < ParamId::end ? indexOfId.row[id] : -1; }
inline const Param& paramAt (uint32_t index) { return params.rows[index]; }

inline float clampParam (const Param& p, float v)
{
    if (! (v >= p.min)) v = p.min; // NaN too
    if (v > p.max) v = p.max;
    return p.kind == ParamKind::continuous ? v : std::round (v);
}

// A KIT field's slot and field, or false.
inline bool kitField (uint32_t id, uint32_t& slot, uint32_t& field)
{
    if (id < ParamId::kitBase || id >= ParamId::end) return false;
    slot = (id - ParamId::kitBase) / ParamId::kitStride;
    field = (id - ParamId::kitBase) % ParamId::kitStride;
    return field < ParamId::kitFields;
}

// Text for a Data knob under its machine: "55.0Hz", "120ms", "1.25s", "+2.0oc", "3.50", "+25", "CLOSED". At most 7
// characters, for the screen's rows.
inline void formatData (const DataSpec& d, float x, char* text, uint32_t size)
{
    if (! d.used()) { std::snprintf (text, size, "-"); return; }
    const float v = dataValue (d, x);
    switch (d.unit)
    {
        case Unit::count: std::snprintf (text, size, "%d", (int) v); break;
        case Unit::choice: std::snprintf (text, size, "%s", d.choices[(int) v]); break;
        case Unit::ratio: std::snprintf (text, size, v < 10.0f ? "%.2f" : "%.1f", v); break;
        case Unit::hz:
            if (v < 100.0f) std::snprintf (text, size, "%.1fHz", v);
            else if (v < 1000.0f) std::snprintf (text, size, "%.0fHz", v);
            else std::snprintf (text, size, "%.1fk", v / 1000.0f);
            break;
        case Unit::seconds:
        case Unit::decayInf:
            if (d.unit == Unit::decayInf && x >= infAbove) std::snprintf (text, size, "INF");
            else if (v < 0.0995f) std::snprintf (text, size, "%.1fms", v * 1000.0f);
            else if (v < 0.9995f) std::snprintf (text, size, "%.0fms", v * 1000.0f);
            else std::snprintf (text, size, "%.2fs", v);
            break;
        case Unit::octaves: std::snprintf (text, size, "%.1foc", v); break;
        case Unit::semis: std::snprintf (text, size, v == 0.0f ? "0st" : "%+dst", (int) v); break;
        case Unit::ratioX: std::snprintf (text, size, v < 10.0f ? "%.2fx" : "%.1fx", v); break;
        case Unit::rateHz: std::snprintf (text, size, v < 10.0f ? "%.2fHz" : "%.1fHz", v); break;
        case Unit::number: std::snprintf (text, size, "%.2f", v); break;
        case Unit::vowel:
        {
            static const char letters[] = "AEIOU";
            const float at = x * 4.0f;
            const int lo = (int) at, hi = lo < 4 ? lo + 1 : 4;
            const float between = at - (float) lo;
            if (between < 0.15f || lo == 4) std::snprintf (text, size, "%c", letters[lo]);
            else if (between > 0.85f) std::snprintf (text, size, "%c", letters[hi]);
            else std::snprintf (text, size, "%c>%c", letters[lo], letters[hi]);
            break;
        }
        case Unit::material:
        {
            static const char* const names[] = { "STRING", "BAR", "DRUM", "BELL" };
            const int nearest = (int) std::lround (x * 3.0f);
            const bool on = std::fabs (x * 3.0f - (float) nearest) < 0.08f;
            std::snprintf (text, size, on ? "%s" : "~%s", names[nearest]);
            break;
        }
        case Unit::bipolar: std::snprintf (text, size, "%+d", (int) std::lround (v * 100.0f)); break;
        case Unit::signedUnit: std::snprintf (text, size, "%+d", (int) std::lround (x * 100.0f)); break;
        case Unit::unit: std::snprintf (text, size, "%d", (int) std::lround (x * 100.0f)); break;
    }
}

// A time's text: "0ms", "4.2ms", "120ms", "1.25s".
inline void formatTime (float s, char* text, uint32_t size)
{
    if (s <= 0.0f) std::snprintf (text, size, "0ms");
    else if (s < 0.0995f) std::snprintf (text, size, "%.1fms", s * 1000.0f);
    else if (s < 0.9995f) std::snprintf (text, size, "%.0fms", s * 1000.0f);
    else std::snprintf (text, size, "%.2fs", s);
}

// A parameter's text. `machineKind`: the machine in use (Data A-H), `slotKind`: the slot's (a KIT slot's Data).
inline void formatParam (const Param& p, float v, uint8_t machineKind, uint8_t slotKind, char* text, uint32_t size)
{
    v = clampParam (p, v);
    switch (p.text)
    {
        case Text::machine: std::snprintf (text, size, "%s", kindNames[(int) v]); break;
        case Text::kitMachine: std::snprintf (text, size, "%s", slotNames[(int) v]); break;
        case Text::data:
            if (machineKind == kit) std::snprintf (text, size, "%d", (int) std::lround (v * 100.0f));
            else formatData (dataSpec (machineKind, (uint32_t) dataOf (p.id)), v, text, size);
            break;
        case Text::kitData:
            formatData (dataSpec (slotKind, (uint32_t) dataOf (p.id)), v, text, size);
            break;
        case Text::unit: std::snprintf (text, size, "%d", (int) std::lround (v * 100.0f)); break;
        case Text::signedUnit: std::snprintf (text, size, "%+d", (int) std::lround (v * 100.0f)); break;
        case Text::freq:
        {
            const float hz = filterHz (v);
            if (hz < 1000.0f) std::snprintf (text, size, "%.0fHz", hz);
            else std::snprintf (text, size, "%.1fk", hz / 1000.0f);
            break;
        }
        case Text::time: formatTime (timeSeconds (v), text, size); break;
        case Text::timeInf:
            if (v >= infAbove) std::snprintf (text, size, "INF");
            else formatTime (timeSeconds (v), text, size);
            break;
        case Text::choice: std::snprintf (text, size, "%s", p.choices[(int) v]); break;
        case Text::octavesEnv: std::snprintf (text, size, "%+.1foc", v * filterEnvOctaves); break;
        case Text::lfoHz:
        {
            const float hz = lfoHz (v);
            std::snprintf (text, size, hz < 10.0f ? "%.2fHz" : "%.1fHz", hz);
            break;
        }
        case Text::volume:
        {
            const float g = volumeGain (v);
            if (g <= 0.0f) std::snprintf (text, size, "-INF");
            else
            {
                const float db = 20.0f * std::log10 (g);
                std::snprintf (text, size, db > -99.95f ? "%+.1fdB" : "%+.0fdB", db); // seven characters at most
            }
            break;
        }
    }
}

// Reads a Data knob's text (as formatData writes it, units optional) back to its position, 0..1.
inline bool parseData (const DataSpec& d, const char* text, float& x)
{
    if (! d.used() || text == nullptr) return false;
    while (*text == ' ') ++text;
    auto position = [&] (float v) { x = v < 0.0f ? 0.0f : v > 1.0f ? 1.0f : v; return true; };
    auto steps = [&] (int i) { return position (d.steps > 1 ? (float) i / (float) (d.steps - 1) : 0.0f); };
    if (d.unit == Unit::choice)
        for (int i = 0; i < d.steps; ++i)
            if (strcasecmp (text, d.choices[i]) == 0) return steps (i);
    if (d.unit == Unit::decayInf && strcasecmp (text, "INF") == 0) return position (1.0f);
    if (d.unit == Unit::vowel)
    {
        static const char letters[] = "AEIOU";
        const char* hit = std::strchr (letters, std::toupper ((unsigned char) text[0]));
        if (hit == nullptr || text[0] == 0) return false;
        float at = (float) (hit - letters);
        if (text[1] == '>' && text[2] != 0)
            if (const char* to = std::strchr (letters, std::toupper ((unsigned char) text[2]))) at = 0.5f * (at + (float) (to - letters));
        return position (at / 4.0f);
    }
    if (d.unit == Unit::material)
    {
        static const char* const names[] = { "STRING", "BAR", "DRUM", "BELL" };
        const char* name = text[0] == '~' ? text + 1 : text;
        for (int i = 0; i < 4; ++i)
            if (strcasecmp (name, names[i]) == 0) return position ((float) i / 3.0f);
        return false;
    }
    char* end = nullptr;
    double v = std::strtod (text, &end);
    if (end == text || ! std::isfinite (v)) return false; // "nan" and "inf" are numbers to strtod, not to a knob
    while (*end == ' ') ++end;
    if (*end == 'k' || *end == 'K') v *= 1000.0;
    if (end[0] == 'm' && end[1] == 's') v /= 1000.0;
    switch (d.unit)
    {
        case Unit::count:
        case Unit::semis: return steps ((int) std::lround (v - d.lo));
        case Unit::choice: return steps ((int) std::lround (v));
        case Unit::ratio:
        {
            int best = 0;
            for (int i = 1; i < ratioCount; ++i)
                if (std::fabs (ratioTable[i] - v) < std::fabs (ratioTable[best] - v)) best = i;
            return steps (best);
        }
        case Unit::hz:
            if (v <= 0.0) return position (0.0f);
            return position (d.curve == Curve::exp ? std::log ((float) v / d.lo) / std::log (d.hi / d.lo) : ((float) v - d.lo) / (d.hi - d.lo));
        case Unit::seconds:
        case Unit::decayInf: return position (timeKnob ((float) v));
        case Unit::octaves:
        case Unit::number: return position (((float) v - d.lo) / (d.hi - d.lo));
        case Unit::ratioX: return v <= 0.0 ? position (0.0f) : position (std::log ((float) v / d.lo) / std::log (d.hi / d.lo));
        case Unit::rateHz: return position (v <= 0.0 ? 0.0f : std::sqrt ((float) v / d.hi));
        case Unit::bipolar: return position (((float) v / 100.0f + 1.0f) * 0.5f);
        default: return position ((float) v / 100.0f);
    }
}

// Reads a parameter's text back: a choice by its name or number, else the number in the 0..100 (or -100..100) the
// text shows for most; times, frequencies and Data units fall back to the plain value.
inline bool parseParam (const Param& p, const char* text, float& v)
{
    if (p.text == Text::volume && strcasecmp (text, "-INF") == 0) return v = 0.0f, true;
    if (p.text == Text::timeInf && strcasecmp (text, "INF") == 0) return v = 1.0f, true;
    if (p.kind == ParamKind::choice)
    {
        const int n = (int) (p.max - p.min) + 1;
        for (int i = 0; i < n; ++i)
            if (strcasecmp (text, p.choices[i]) == 0)
                return v = p.min + (float) i, true;
    }
    char* end = nullptr;
    const double x = std::strtod (text, &end);
    if (end == text || ! std::isfinite (x)) return false; // "nan" and "inf" are numbers to strtod, not to a knob
    while (*end == ' ') ++end;
    switch (p.text)
    {
        case Text::unit:
        case Text::signedUnit:
        case Text::data:    // a KIT's own Data (the machine's are read by parseData)
        case Text::kitData: v = (float) (x / 100.0); break;
        case Text::time:
        case Text::timeInf: v = timeKnob ((float) ((end[0] == 'm' && end[1] == 's') ? x / 1000.0 : x)); break;
        case Text::freq: v = filterKnob ((float) ((*end == 'k' || *end == 'K') ? x * 1000.0 : x)); break;
        case Text::octavesEnv: v = (float) (x / filterEnvOctaves); break;
        case Text::lfoHz: v = x > 0.0 ? std::log2 ((float) x / 0.05f) / 9.25f : 0.0f; break;
        case Text::volume: v = 0.75f * std::pow (10.0f, (float) x / 40.0f); break; // dB: the gain is (v / 0.75)^2
        default: v = (float) x; break; // the machine's and choices' numbers
    }
    v = clampParam (p, v);
    return true;
}
} // namespace machine
