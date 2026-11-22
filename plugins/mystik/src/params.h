// Mystik's parameters: one table for the CLAP side (names, ranges, text) and the engine (what a value means).
// Ids are fixed forever: a saved project keeps its values by id. New parameters take new ids (append only), and the
// numbered choices (bodies, ratios, scales, rhythms, modes) only ever grow at their ends.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <strings.h>

#ifndef MYSTIK_VERSION
#define MYSTIK_VERSION "0.2.2"
#endif

namespace mystik
{
constexpr int steps = 16;

namespace ParamId
{
enum : uint32_t
{
    tune = 0, source, body, ring, position, snap,
    fmIndex, fmRatio, feedback, timbre, symmetry, bloom, bend, drift,
    gateMode, resonance, strike, decay, open, hold, memory, accent, glide,
    drive, crush, echo, echoTime, repeats, absorb, space, spaceDecay, volume,
    seqOn, seqMode, rate, length, direction, swing, gate, scale, density, rotate, mutate, range, accents, rule, rhythm,
    curve, seed, regenerate, notes,
    pitch1, // 16 of each: Pitch 1-16, Trig 1-16, Stroke 1-16, Chance 1-16
    trig1 = pitch1 + steps,
    stroke1 = trig1 + steps,
    chance1 = stroke1 + steps,
    human = chance1 + steps, // 0.2: added after the steps; ids never move
    count
};
}

enum class Kind : uint8_t { continuous, integer, choice, button };
// How a value reads.
enum class Unit : uint8_t { note, percent, bipolar, ringTime, strikeTime, decayTime, glideTime, gateMode, plainInt, choice,
                            button, degree, octaves, ratioCurve };

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

// --- What the values mean -------------------------------------------------------------------------------------------

// Tune: semitones from C2 (MIDI 48, 130.81 Hz; the MPC's names, C3 = MIDI 60).
constexpr float tuneRootHz = 130.812783f;
constexpr int tuneRootNote = 48;
// Ring: the membrane's fundamental falls 60 dB in 30 ms to 4 s.
inline float ringSeconds (float k) { return 0.03f * std::pow (4.0f / 0.03f, k); }
// Strike: the LED pulse, 0.5 to 30 ms.
inline float strikeSeconds (float k) { return 0.0005f * std::pow (60.0f, k); }
// Decay: the vactrol's falling time constant, 25 ms to 2.5 s (250 ms, the VTL5C3's, in the middle).
inline float decaySeconds (float k) { return 0.025f * std::pow (100.0f, k); }
// Glide: 5 to 500 ms.
inline float glideSeconds (float k) { return 0.005f * std::pow (100.0f, k); }
// Open: a light that stays on, 0 to 0.1 (about 8 kHz in Combo), cubed so the knob spreads the cutoff evenly.
inline float openLight (float k) { return 0.1f * k * k * k; }
// Range: how far generated pitches spread, 0.25 to 3 octaves.
inline float rangeOctaves (float k) { return 0.25f + 2.75f * k; }
// Curve: how much shorter each bounce is than the one before.
inline float bounceRatio (float k) { return 0.5f + 0.42f * k; }
// Gate Mode's morph: 0 VCA, 0.5 Combo, 1 Lowpass.

inline const char* const offOn[] = { "Off", "On" };
inline const char* const bodyChoices[] = { "Tabla", "Conga", "Frame", "Udu", "Slit", "Handpan", "Bowl", "Bell", "Goblet" };
inline const char* const ratioChoices[] = { "1/2", "1", "3/2", "2", "5/2", "3", "4", "5", "7",
                                            "1.414", "1.594", "2.136", "2.296", "2.653", "2.918", "3.156" };
constexpr float ratioValues[] = { 0.5f, 1.0f, 1.5f, 2.0f, 2.5f, 3.0f, 4.0f, 5.0f, 7.0f,
                                  1.41421356f, 1.594f, 2.136f, 2.296f, 2.653f, 2.918f, 3.156f };
inline const char* const seqModeChoices[] = { "Pattern", "Euclid", "Turing", "Automaton", "Bounce", "Rhythm" };
namespace SeqMode { enum : int { pattern, euclid, turing, automaton, bounce, rhythm }; }
inline const char* const rateChoices[] = { "1/2", "1/4", "1/4T", "1/8", "1/8T", "1/16", "1/16T", "1/32", "1/32T", "1/64" };
constexpr double rateBeats[] = { 2.0, 1.0, 2.0 / 3.0, 0.5, 1.0 / 3.0, 0.25, 1.0 / 6.0, 0.125, 1.0 / 12.0, 0.0625 };
inline const char* const directionChoices[] = { "Forward", "Backward", "Ping-Pong", "Brownian", "Random" };
inline const char* const echoTimeChoices[] = { "1/32", "1/16T", "1/16", "1/8T", "1/16D", "1/8", "1/4T", "1/8D", "1/4", "1/2T", "1/4D", "1/2" };
constexpr double echoTimeBeats[] = { 0.125, 1.0 / 6.0, 0.25, 1.0 / 3.0, 0.375, 0.5, 2.0 / 3.0, 0.75, 1.0, 4.0 / 3.0, 1.5, 2.0 };
inline const char* const notesChoices[] = { "Pitch", "Strokes", "Steps" };
inline const char* const trigChoices[] = { "Rest", "Hit", "Accent", "Slide", "Acc+Slide" };
namespace Trig { enum : int { rest, hit, accent, slide, accentSlide }; }
inline const char* const strokeChoices[] = { "Open", "Slap", "Bass", "Mute", "Ghost", "Flam", "Roll", "Press" };
namespace Stroke { enum : int { open, slap, bass, mute, ghost, flam, roll, press, count }; }
static_assert (sizeof (rateBeats) / sizeof (rateBeats[0]) == sizeof (rateChoices) / sizeof (rateChoices[0]), "a length a choice");
static_assert (sizeof (echoTimeBeats) / sizeof (echoTimeBeats[0]) == sizeof (echoTimeChoices) / sizeof (echoTimeChoices[0]), "a length a choice");
static_assert (sizeof (ratioValues) / sizeof (ratioValues[0]) == sizeof (ratioChoices) / sizeof (ratioChoices[0]), "a ratio a name");

// --- Scales: a step's Pitch is a degree of one, counted from Tune ---------------------------------------------------
struct Scale { const char* name; uint8_t count; float semis[12]; };
inline constexpr Scale scales[] = {
    { "Chromatic", 12, { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 } },
    { "Major", 7, { 0, 2, 4, 5, 7, 9, 11 } },
    { "Minor", 7, { 0, 2, 3, 5, 7, 8, 10 } },
    { "Dorian", 7, { 0, 2, 3, 5, 7, 9, 10 } },
    { "Phrygian", 7, { 0, 1, 3, 5, 7, 8, 10 } },
    { "Hijaz", 7, { 0, 1, 4, 5, 7, 8, 10 } },     // Phrygian dominant, the maqam's lower jins
    { "Bhairav", 7, { 0, 1, 4, 5, 7, 8, 11 } },   // the raga (double harmonic)
    { "Pent Minor", 5, { 0, 3, 5, 7, 10 } },
    { "Pent Major", 5, { 0, 2, 4, 7, 9 } },
    { "Hirajoshi", 5, { 0, 2, 3, 7, 8 } },
    { "Pelog", 5, { 0, 1.2f, 2.7f, 6.7f, 7.85f } }, // a selisir-like pelog, in cents 0 120 270 670 785
    { "Slendro", 5, { 0, 2.4f, 4.8f, 7.2f, 9.6f } }, // five equal steps of 240 cents
};
inline const char* const scaleChoices[] = { "Chromatic", "Major", "Minor", "Dorian", "Phrygian", "Hijaz", "Bhairav",
                                            "Pent Minor", "Pent Major", "Hirajoshi", "Pelog", "Slendro" };
static_assert (sizeof (scales) / sizeof (scales[0]) == sizeof (scaleChoices) / sizeof (scaleChoices[0]), "a scale a name");

// A degree of scale `s` in semitones from Tune.
inline float degreeSemis (int degree, int s)
{
    const Scale& scale = scales[s < 0 ? 0 : (s > 11 ? 11 : s)];
    const int n = scale.count;
    const int octave = degree >= 0 ? degree / n : -((-degree + n - 1) / n);
    return 12.0f * (float) octave + scale.semis[degree - octave * n];
}

// --- Rhythms: traditional timelines on the step grid, a stroke a place ('.' rests) ----------------------------------
// B bass, O open, S slap, M mute, G ghost. Strokes follow the usual way each is played on one drum: a doumbek's dum is
// Bass and its tek Slap; a tabla's dha Bass, dhin Open, tin Ghost, na and ta Slap, ka Mute; a conga's heel and toe
// Ghost. Lengths are the timelines' own (Length does not apply).
struct Rhythm { const char* name; const char* places; };
inline constexpr Rhythm rhythms[] = {
    { "Tresillo", "B..O..O." },             // E(3,8)
    { "Cinquillo", "B.OS.OS." },            // E(5,8)
    { "Son Clave", "S..S..S...S.S..." },    // 3-2
    { "Rumba Clave", "S..S...S..S.S..." },  // 3-2
    { "Bossa", "S..S..S...S..S.." },
    { "Bell 12/8", "S.S.SS.S.S.S" },        // the standard pattern, E(7,12)
    { "Tumbao", "GGSGGGOO" },               // heel toe slap toe heel toe open open
    { "Maqsum", "BS.SB.S." },               // dum tek . tek dum . tek .
    { "Baladi", "BB.SB.S." },
    { "Saidi", "BS.BB.S." },
    { "Malfuf", "B..S..S." },
    { "Teental", "BOOBBOOBBGGSSOOB" },      // dha dhin dhin dha / dha dhin dhin dha / dha tin tin ta / ta dhin dhin dha
    { "Keherwa", "BBSMSMOS" },              // dha ge na ti na ka dhi na
    { "Dadra", "BOSBOS" },                  // dha dhi na dha tu na
    { "Rupak", "GGSOSOS" },                 // tin tin na dhi na dhi na
};
inline const char* const rhythmChoices[] = { "Tresillo", "Cinquillo", "Son Clave", "Rumba Clave", "Bossa", "Bell 12/8",
                                             "Tumbao", "Maqsum", "Baladi", "Saidi", "Malfuf", "Teental", "Keherwa", "Dadra",
                                             "Rupak" };
static_assert (sizeof (rhythms) / sizeof (rhythms[0]) == sizeof (rhythmChoices) / sizeof (rhythmChoices[0]), "a rhythm a name");
constexpr int rhythmCount = (int) (sizeof (rhythms) / sizeof (rhythms[0]));
inline int rhythmLength (int r) { return (int) std::strlen (rhythms[r].places); }
// The stroke at a timeline's place, or -1 for a rest. A place past the timeline's end is a rest too (the page may ask for
// the places of a longer cycle it last heard of from the engine).
inline int rhythmStroke (int r, int place)
{
    if (r < 0 || r >= rhythmCount || place < 0 || place >= rhythmLength (r))
        return -1;
    switch (rhythms[r].places[place])
    {
        case 'B': return Stroke::bass;
        case 'O': return Stroke::open;
        case 'S': return Stroke::slap;
        case 'M': return Stroke::mute;
        case 'G': return Stroke::ghost;
        default: return -1;
    }
}

// What a parameter's text depends on beyond its own value: a step's Pitch reads as the note it plays.
struct TextContext
{
    float tune = 0.0f;
    float scale = 5.0f;
};

using P = Param;
#define CONT(id, name, module, lo, hi, def, unit) P { ParamId::id, name, module, lo, hi, def, Kind::continuous, Unit::unit, nullptr }
#define INT(id, name, module, lo, hi, def, unit) P { ParamId::id, name, module, lo, hi, def, Kind::integer, Unit::unit, nullptr }
#define CHOICE(id, name, module, list, def) P { ParamId::id, name, module, 0.0f, (float) (sizeof (list) / sizeof (list[0]) - 1), def, Kind::choice, Unit::choice, list }
#define BUTTON(id, name, module) P { ParamId::id, name, module, 0.0f, 1.0f, 0.0f, Kind::button, Unit::button, nullptr }
#define STEP(base, n, name, module, lo, hi, def, kind, unit, list) P { ParamId::base + n, name, module, lo, hi, def, Kind::kind, Unit::unit, list }

// The default groove: a conga line in Hijaz from A2, its pitches as degrees.
constexpr float defaultPitch[steps] = { 0, 0, 0, 4, 2, 0, 0, 4, 0, 0, 3, 4, 2, 0, 7, 5 };
constexpr float defaultTrig[steps] = { 2, 0, 1, 1, 1, 0, 1, 1, 1, 0, 1, 3, 2, 1, 1, 3 };
constexpr float defaultStroke[steps] = { 2, 0, 4, 0, 1, 0, 4, 0, 2, 0, 3, 0, 1, 4, 0, 7 };
constexpr float defaultChance[steps] = { 1, 1, 1, 1, 1, 1, .6f, 1, 1, 1, 1, 1, 1, .5f, 1, 1 };

// In the order a host lists them, three to a row as Melodee shows them: the skin, the oscillator, the gate, the effects,
// the sequencer, then the steps.
inline constexpr Param paramTable[] = {
    CONT (tune, "Tune", "Skin", -36.0f, 36.0f, 9.0f, note),
    CONT (source, "Source", "Skin", 0.0f, 1.0f, 0.6f, percent),
    CHOICE (body, "Body", "Skin", bodyChoices, 1.0f),
    CONT (ring, "Ring", "Skin", 0.0f, 1.0f, 0.45f, ringTime),
    CONT (position, "Position", "Skin", 0.0f, 1.0f, 0.35f, percent),
    CONT (snap, "Snap", "Skin", 0.0f, 1.0f, 0.3f, percent),
    CONT (bend, "Bend", "Skin", -1.0f, 1.0f, 0.2f, bipolar),
    CONT (drift, "Drift", "Skin", 0.0f, 1.0f, 0.1f, percent),
    CONT (human, "Human", "Skin", 0.0f, 1.0f, 0.3f, percent),
    CONT (fmIndex, "FM Index", "Osc", 0.0f, 1.0f, 0.2f, percent),
    CHOICE (fmRatio, "FM Ratio", "Osc", ratioChoices, 10.0f),
    CONT (feedback, "Feedback", "Osc", 0.0f, 1.0f, 0.0f, percent),
    CONT (timbre, "Timbre", "Osc", 0.0f, 1.0f, 0.12f, percent),
    CONT (symmetry, "Symmetry", "Osc", -1.0f, 1.0f, 0.0f, bipolar),
    CONT (bloom, "Bloom", "Osc", 0.0f, 1.0f, 0.3f, percent),
    CONT (gateMode, "Gate Mode", "Gate", 0.0f, 1.0f, 0.5f, gateMode),
    CONT (resonance, "Resonance", "Gate", 0.0f, 1.0f, 0.0f, percent),
    CONT (strike, "Strike", "Gate", 0.0f, 1.0f, 0.4f, strikeTime),
    CONT (decay, "Decay", "Gate", 0.0f, 1.0f, 0.33f, decayTime),
    CONT (open, "Open", "Gate", 0.0f, 1.0f, 0.0f, percent),
    CONT (hold, "Hold", "Gate", 0.0f, 1.0f, 0.0f, percent),
    CONT (memory, "Memory", "Gate", 0.0f, 1.0f, 0.25f, percent),
    CONT (accent, "Accent", "Gate", 0.0f, 1.0f, 0.5f, percent),
    CONT (glide, "Glide", "Gate", 0.0f, 1.0f, 0.35f, glideTime),
    CONT (drive, "Drive", "FX", 0.0f, 1.0f, 0.0f, percent),
    CONT (crush, "Crush", "FX", 0.0f, 1.0f, 0.0f, percent),
    CONT (echo, "Echo", "FX", 0.0f, 1.0f, 0.12f, percent),
    CHOICE (echoTime, "Echo Time", "FX", echoTimeChoices, 7.0f),
    CONT (repeats, "Repeats", "FX", 0.0f, 1.0f, 0.4f, percent),
    CONT (absorb, "Absorb", "FX", 0.0f, 1.0f, 0.45f, percent),
    CONT (space, "Space", "FX", 0.0f, 1.0f, 0.18f, percent),
    CONT (spaceDecay, "Space Decay", "FX", 0.0f, 1.0f, 0.5f, percent),
    CONT (volume, "Volume", "FX", 0.0f, 1.0f, 0.7f, percent),
    CHOICE (seqOn, "Sequencer", "Sequencer", offOn, 1.0f),
    CHOICE (seqMode, "Seq Mode", "Sequencer", seqModeChoices, 0.0f),
    CHOICE (rate, "Rate", "Sequencer", rateChoices, 5.0f),
    INT (length, "Length", "Sequencer", 1.0f, 16.0f, 16.0f, plainInt),
    CHOICE (direction, "Direction", "Sequencer", directionChoices, 0.0f),
    CONT (swing, "Swing", "Sequencer", 0.5f, 0.75f, 0.54f, percent),
    CONT (gate, "Gate", "Sequencer", 0.05f, 1.0f, 0.5f, percent),
    CHOICE (scale, "Scale", "Sequencer", scaleChoices, 5.0f),
    CONT (density, "Density", "Sequencer", 0.0f, 1.0f, 0.5f, percent),
    INT (rotate, "Rotate", "Sequencer", 0.0f, 15.0f, 0.0f, plainInt),
    CONT (mutate, "Mutate", "Sequencer", 0.0f, 1.0f, 0.0f, percent),
    CONT (range, "Range", "Sequencer", 0.0f, 1.0f, 0.4f, octaves),
    CONT (accents, "Accents", "Sequencer", 0.0f, 1.0f, 0.3f, percent),
    INT (rule, "Rule", "Sequencer", 0.0f, 255.0f, 90.0f, plainInt),
    CHOICE (rhythm, "Rhythm", "Sequencer", rhythmChoices, 6.0f),
    CONT (curve, "Curve", "Sequencer", 0.0f, 1.0f, 0.5f, ratioCurve),
    INT (seed, "Seed", "Sequencer", 0.0f, 9999.0f, 1.0f, plainInt),
    BUTTON (regenerate, "Regenerate", "Sequencer"),
    CHOICE (notes, "Notes", "Sequencer", notesChoices, 0.0f),
    STEP (pitch1, 0, "Pitch 1", "Steps/Pitch", -21, 21, defaultPitch[0], integer, degree, nullptr),
    STEP (pitch1, 1, "Pitch 2", "Steps/Pitch", -21, 21, defaultPitch[1], integer, degree, nullptr),
    STEP (pitch1, 2, "Pitch 3", "Steps/Pitch", -21, 21, defaultPitch[2], integer, degree, nullptr),
    STEP (pitch1, 3, "Pitch 4", "Steps/Pitch", -21, 21, defaultPitch[3], integer, degree, nullptr),
    STEP (pitch1, 4, "Pitch 5", "Steps/Pitch", -21, 21, defaultPitch[4], integer, degree, nullptr),
    STEP (pitch1, 5, "Pitch 6", "Steps/Pitch", -21, 21, defaultPitch[5], integer, degree, nullptr),
    STEP (pitch1, 6, "Pitch 7", "Steps/Pitch", -21, 21, defaultPitch[6], integer, degree, nullptr),
    STEP (pitch1, 7, "Pitch 8", "Steps/Pitch", -21, 21, defaultPitch[7], integer, degree, nullptr),
    STEP (pitch1, 8, "Pitch 9", "Steps/Pitch", -21, 21, defaultPitch[8], integer, degree, nullptr),
    STEP (pitch1, 9, "Pitch 10", "Steps/Pitch", -21, 21, defaultPitch[9], integer, degree, nullptr),
    STEP (pitch1, 10, "Pitch 11", "Steps/Pitch", -21, 21, defaultPitch[10], integer, degree, nullptr),
    STEP (pitch1, 11, "Pitch 12", "Steps/Pitch", -21, 21, defaultPitch[11], integer, degree, nullptr),
    STEP (pitch1, 12, "Pitch 13", "Steps/Pitch", -21, 21, defaultPitch[12], integer, degree, nullptr),
    STEP (pitch1, 13, "Pitch 14", "Steps/Pitch", -21, 21, defaultPitch[13], integer, degree, nullptr),
    STEP (pitch1, 14, "Pitch 15", "Steps/Pitch", -21, 21, defaultPitch[14], integer, degree, nullptr),
    STEP (pitch1, 15, "Pitch 16", "Steps/Pitch", -21, 21, defaultPitch[15], integer, degree, nullptr),
    STEP (trig1, 0, "Trig 1", "Steps/Trig", 0, 4, defaultTrig[0], choice, choice, trigChoices),
    STEP (trig1, 1, "Trig 2", "Steps/Trig", 0, 4, defaultTrig[1], choice, choice, trigChoices),
    STEP (trig1, 2, "Trig 3", "Steps/Trig", 0, 4, defaultTrig[2], choice, choice, trigChoices),
    STEP (trig1, 3, "Trig 4", "Steps/Trig", 0, 4, defaultTrig[3], choice, choice, trigChoices),
    STEP (trig1, 4, "Trig 5", "Steps/Trig", 0, 4, defaultTrig[4], choice, choice, trigChoices),
    STEP (trig1, 5, "Trig 6", "Steps/Trig", 0, 4, defaultTrig[5], choice, choice, trigChoices),
    STEP (trig1, 6, "Trig 7", "Steps/Trig", 0, 4, defaultTrig[6], choice, choice, trigChoices),
    STEP (trig1, 7, "Trig 8", "Steps/Trig", 0, 4, defaultTrig[7], choice, choice, trigChoices),
    STEP (trig1, 8, "Trig 9", "Steps/Trig", 0, 4, defaultTrig[8], choice, choice, trigChoices),
    STEP (trig1, 9, "Trig 10", "Steps/Trig", 0, 4, defaultTrig[9], choice, choice, trigChoices),
    STEP (trig1, 10, "Trig 11", "Steps/Trig", 0, 4, defaultTrig[10], choice, choice, trigChoices),
    STEP (trig1, 11, "Trig 12", "Steps/Trig", 0, 4, defaultTrig[11], choice, choice, trigChoices),
    STEP (trig1, 12, "Trig 13", "Steps/Trig", 0, 4, defaultTrig[12], choice, choice, trigChoices),
    STEP (trig1, 13, "Trig 14", "Steps/Trig", 0, 4, defaultTrig[13], choice, choice, trigChoices),
    STEP (trig1, 14, "Trig 15", "Steps/Trig", 0, 4, defaultTrig[14], choice, choice, trigChoices),
    STEP (trig1, 15, "Trig 16", "Steps/Trig", 0, 4, defaultTrig[15], choice, choice, trigChoices),
    STEP (stroke1, 0, "Stroke 1", "Steps/Stroke", 0, 7, defaultStroke[0], choice, choice, strokeChoices),
    STEP (stroke1, 1, "Stroke 2", "Steps/Stroke", 0, 7, defaultStroke[1], choice, choice, strokeChoices),
    STEP (stroke1, 2, "Stroke 3", "Steps/Stroke", 0, 7, defaultStroke[2], choice, choice, strokeChoices),
    STEP (stroke1, 3, "Stroke 4", "Steps/Stroke", 0, 7, defaultStroke[3], choice, choice, strokeChoices),
    STEP (stroke1, 4, "Stroke 5", "Steps/Stroke", 0, 7, defaultStroke[4], choice, choice, strokeChoices),
    STEP (stroke1, 5, "Stroke 6", "Steps/Stroke", 0, 7, defaultStroke[5], choice, choice, strokeChoices),
    STEP (stroke1, 6, "Stroke 7", "Steps/Stroke", 0, 7, defaultStroke[6], choice, choice, strokeChoices),
    STEP (stroke1, 7, "Stroke 8", "Steps/Stroke", 0, 7, defaultStroke[7], choice, choice, strokeChoices),
    STEP (stroke1, 8, "Stroke 9", "Steps/Stroke", 0, 7, defaultStroke[8], choice, choice, strokeChoices),
    STEP (stroke1, 9, "Stroke 10", "Steps/Stroke", 0, 7, defaultStroke[9], choice, choice, strokeChoices),
    STEP (stroke1, 10, "Stroke 11", "Steps/Stroke", 0, 7, defaultStroke[10], choice, choice, strokeChoices),
    STEP (stroke1, 11, "Stroke 12", "Steps/Stroke", 0, 7, defaultStroke[11], choice, choice, strokeChoices),
    STEP (stroke1, 12, "Stroke 13", "Steps/Stroke", 0, 7, defaultStroke[12], choice, choice, strokeChoices),
    STEP (stroke1, 13, "Stroke 14", "Steps/Stroke", 0, 7, defaultStroke[13], choice, choice, strokeChoices),
    STEP (stroke1, 14, "Stroke 15", "Steps/Stroke", 0, 7, defaultStroke[14], choice, choice, strokeChoices),
    STEP (stroke1, 15, "Stroke 16", "Steps/Stroke", 0, 7, defaultStroke[15], choice, choice, strokeChoices),
    STEP (chance1, 0, "Chance 1", "Steps/Chance", 0, 1, defaultChance[0], continuous, percent, nullptr),
    STEP (chance1, 1, "Chance 2", "Steps/Chance", 0, 1, defaultChance[1], continuous, percent, nullptr),
    STEP (chance1, 2, "Chance 3", "Steps/Chance", 0, 1, defaultChance[2], continuous, percent, nullptr),
    STEP (chance1, 3, "Chance 4", "Steps/Chance", 0, 1, defaultChance[3], continuous, percent, nullptr),
    STEP (chance1, 4, "Chance 5", "Steps/Chance", 0, 1, defaultChance[4], continuous, percent, nullptr),
    STEP (chance1, 5, "Chance 6", "Steps/Chance", 0, 1, defaultChance[5], continuous, percent, nullptr),
    STEP (chance1, 6, "Chance 7", "Steps/Chance", 0, 1, defaultChance[6], continuous, percent, nullptr),
    STEP (chance1, 7, "Chance 8", "Steps/Chance", 0, 1, defaultChance[7], continuous, percent, nullptr),
    STEP (chance1, 8, "Chance 9", "Steps/Chance", 0, 1, defaultChance[8], continuous, percent, nullptr),
    STEP (chance1, 9, "Chance 10", "Steps/Chance", 0, 1, defaultChance[9], continuous, percent, nullptr),
    STEP (chance1, 10, "Chance 11", "Steps/Chance", 0, 1, defaultChance[10], continuous, percent, nullptr),
    STEP (chance1, 11, "Chance 12", "Steps/Chance", 0, 1, defaultChance[11], continuous, percent, nullptr),
    STEP (chance1, 12, "Chance 13", "Steps/Chance", 0, 1, defaultChance[12], continuous, percent, nullptr),
    STEP (chance1, 13, "Chance 14", "Steps/Chance", 0, 1, defaultChance[13], continuous, percent, nullptr),
    STEP (chance1, 14, "Chance 15", "Steps/Chance", 0, 1, defaultChance[14], continuous, percent, nullptr),
    STEP (chance1, 15, "Chance 16", "Steps/Chance", 0, 1, defaultChance[15], continuous, percent, nullptr),
};
#undef CONT
#undef INT
#undef CHOICE
#undef BUTTON
#undef STEP

constexpr uint32_t paramCount = sizeof (paramTable) / sizeof (paramTable[0]);
static_assert (paramCount == ParamId::count, "every id has one row");

// The table's row for an id.
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

// A note name for `semis` from Tune's root (C2), with cents when it falls between keys (Pelog, Slendro, a detuned Tune).
inline void noteName (float semis, char* text, uint32_t size)
{
    static const char* const names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    const float note = (float) tuneRootNote + semis;
    const int key = (int) std::lround (note);
    const int cents = (int) std::lround ((note - (float) key) * 100.0f);
    const int octave = (key >= 0 ? key / 12 : -((-key + 11) / 12)) - 2; // MIDI 60 = C3
    const int name = ((key % 12) + 12) % 12;
    if (cents != 0)
        std::snprintf (text, size, "%s%d%+dc", names[name], octave, cents);
    else
        std::snprintf (text, size, "%s%d", names[name], octave);
}

inline void formatParam (const Param& p, float v, char* text, uint32_t size, const TextContext& context = {})
{
    v = clampParam (p, v);
    auto time = [&] (float s) {
        if (s < 0.0995f) std::snprintf (text, size, "%.1f ms", s * 1000.0f);
        else if (s < 1.0f) std::snprintf (text, size, "%.0f ms", s * 1000.0f);
        else std::snprintf (text, size, "%.2f s", s);
    };
    switch (p.unit)
    {
        case Unit::note: noteName (v, text, size); break;
        case Unit::percent: std::snprintf (text, size, "%.0f %%", v * 100.0f); break;
        case Unit::bipolar: std::snprintf (text, size, "%+.0f %%", v * 100.0f); break;
        case Unit::ringTime: time (ringSeconds (v)); break;
        case Unit::strikeTime: time (strikeSeconds (v)); break;
        case Unit::decayTime: time (decaySeconds (v)); break;
        case Unit::glideTime: time (glideSeconds (v)); break;
        case Unit::gateMode:
            if (v == 0.5f) std::snprintf (text, size, "Combo");
            else if (v < 0.5f) std::snprintf (text, size, "VCA %.0f%%", (0.5f - v) * 200.0f);
            else std::snprintf (text, size, "LP %.0f%%", (v - 0.5f) * 200.0f);
            break;
        case Unit::plainInt: std::snprintf (text, size, "%d", (int) v); break;
        case Unit::choice: std::snprintf (text, size, "%s", p.choices[(int) v]); break;
        case Unit::button: std::snprintf (text, size, "%s", v >= 0.5f ? "Pressed" : "Press"); break;
        case Unit::degree: noteName (context.tune + degreeSemis ((int) v, (int) context.scale), text, size); break;
        case Unit::octaves: std::snprintf (text, size, "%.2f oct", rangeOctaves (v)); break;
        case Unit::ratioCurve: std::snprintf (text, size, "x%.2f", bounceRatio (v)); break;
    }
}

// Reads text as formatParam writes it (units optional); a choice by its name or its number. Text that is no number
// (nan, inf) is refused.
inline bool parseParam (const Param& p, const char* text, float& v)
{
    while (*text == ' ')
        ++text;
    if (p.kind == Kind::choice)
    {
        const int n = (int) (p.max - p.min) + 1;
        size_t len = std::strlen (text);
        while (len > 0 && text[len - 1] == ' ')
            --len;
        for (int i = 0; i < n; ++i)
            if (std::strlen (p.choices[i]) == len && strncasecmp (text, p.choices[i], len) == 0)
                return v = (float) i, true;
    }
    if (p.kind == Kind::button)
    {
        v = (strncasecmp (text, "Pressed", 7) == 0 || std::atof (text) >= 0.5) ? 1.0f : 0.0f;
        return true;
    }
    if (p.unit == Unit::gateMode)
    {
        if (strncasecmp (text, "Combo", 5) == 0) return v = 0.5f, true;
        const bool vca = strncasecmp (text, "VCA", 3) == 0, lp = strncasecmp (text, "LP", 2) == 0;
        if (vca || lp)
        {
            const double share = std::strtod (text + (vca ? 3 : 2), nullptr);
            if (! std::isfinite (share)) return false;
            v = clampParam (p, (float) (vca ? 0.5 - share / 200.0 : 0.5 + share / 200.0));
            return true;
        }
    }
    if (p.unit == Unit::note || p.unit == Unit::degree)
    {
        // A note name (C2, F#3, D#3+40c) or a number (semitones for Tune, a degree for a step).
        static const char* const letters = "C D EF G A B";
        const char* at = std::strchr (letters, (text[0] >= 'a' && text[0] <= 'g') ? text[0] - 32 : text[0]);
        if (text[0] != 0 && at != nullptr && *at != ' ')
        {
            int key = (int) (at - letters);
            const char* rest = text + 1;
            if (*rest == '#') ++key, ++rest;
            else if (*rest == 'b') --key, ++rest;
            char* end = nullptr;
            const long octave = std::strtol (rest, &end, 10);
            if (end == rest) return false;
            float semis = (float) (key + 12 * (octave + 2) - tuneRootNote);
            if (*end == '+' || *end == '-') semis += (float) std::strtod (end, nullptr) / 100.0f;
            if (p.unit == Unit::note) { v = clampParam (p, semis); return true; }
            return false; // a step's note by name: not offered (degrees depend on the scale)
        }
    }
    if (p.unit == Unit::ratioCurve && (*text == 'x' || *text == 'X'))
        ++text;
    char* end = nullptr;
    const double x = std::strtod (text, &end);
    if (end == text || ! std::isfinite (x))
        return false;
    while (*end == ' ')
        ++end;
    const bool milli = (end[0] == 'm' && end[1] == 's');
    auto fromTime = [&] (double seconds, double low, double ratio) { return std::log (std::max (seconds, 1e-9) / low) / std::log (ratio); };
    switch (p.unit)
    {
        case Unit::percent: v = (float) (x / 100.0); break;
        case Unit::bipolar: v = (float) (x / 100.0); break;
        case Unit::ringTime: v = (float) fromTime (milli ? x / 1000.0 : x, 0.03, 4.0 / 0.03); break;
        case Unit::strikeTime: v = (float) fromTime (milli || x > 0.5 ? x / 1000.0 : x, 0.0005, 60.0); break;
        case Unit::decayTime: v = (float) fromTime (milli ? x / 1000.0 : x, 0.025, 100.0); break;
        case Unit::glideTime: v = (float) fromTime (milli ? x / 1000.0 : x, 0.005, 100.0); break;
        case Unit::octaves: v = (float) ((x - 0.25) / 2.75); break;
        case Unit::ratioCurve: v = (float) ((x - 0.5) / 0.42); break;
        default: v = (float) x; break;
    }
    if (! std::isfinite (v))
        return false;
    v = clampParam (p, v);
    return true;
}
} // namespace mystik
