// Cortex Filter's parameters: one table for the CLAP side (names, ranges, text) and the engine (what a knob means).
// Ids are fixed forever: a saved project keeps its values by id. New parameters take new ids.
//
// Since 0.2.0 the sound is a filter from a bank (filters.h): a cube and the settings that go with it (where the knobs'
// zero sits on each axis and how far they reach, fades in and out, a gain and a distortion for each side, Transform
// turned over to distortion). Filter picks one; the sequencer can step through a list of them. The "Edit Filter"
// parameters are the settings of the filter Filter picks, as a host sees them: turning one changes that filter in the
// bank, and choosing another filter shows that one's. Id 0, 0.1.0's Cube, is retired: an older project's cube loads
// as the factory filter for that cube (cortex.cpp).
#pragma once

#include "cubes.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <strings.h>

#ifndef CORTEX_VERSION
#define CORTEX_VERSION "0.2.1"
#endif

namespace cortex
{
namespace ParamId
{
enum : uint32_t
{
    retiredCube = 0, morph, freq, transform, emphasis, glide,
    drive, mix, output, spread,
    lfoSync, lfoRate, lfoShape, lfoPhase, lfoMorph, lfoFreq, lfoTransform,
    envGain, envAttack, envRelease, envMorph, envFreq, envTransform,
    // 0.2.0: the filter and the sequencer.
    filter, sequence, incClock, decClock, resetClock, envTrigger, trigLevel, trigInc, trigDec, trigReset,
    // 0.2.0: the edit filter's settings, in filters.h's field order (editCube + field).
    editCube, morphOffset, freqOffset, transformOffset, morphGain, freqGain, transformGain, enterFade, exitFade,
    gainLeft, gainRight, distLeft, distRight, transformDist,
    count
};
}

enum class Kind : uint8_t { continuous, integer, choice };
// How a value reads.
enum class Unit : uint8_t { percent, bipolar, decibels, lfoHz, glide, attack, release, degrees, choice, slot, offset, times, wholeDb, fade };

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

// Emphasis: every band's Q up to four times as sharp (+100 %) or half as sharp (-100 %), exponentially; widened
// boosts are evened out in morph.h's design().
constexpr float emphasisOctaves = 2.0f, emphasisWideOctaves = 1.0f; // log2 of the Q's factor at each end
constexpr float minQ = 0.1f, maxQ = 80.0f;

// Glide: how fast Morph, Freq and Transform follow their knobs (the time constant), 1 ms to 2 s.
inline float glideSeconds (float k) { return 0.001f * std::pow (2000.0f, k); }
inline float glideKnob (float s) { return std::log (s / 0.001f) / std::log (2000.0f); }

// The LFO's free rate, 0.02 Hz to 20 Hz.
inline float lfoHz (float k) { return 0.02f * std::pow (1000.0f, k); }
inline float lfoKnob (float hz) { return std::log (hz / 0.02f) / std::log (1000.0f); }

// The envelope follower: attack 0.1 ms to 200 ms, release 5 ms to 2 s. It reads the input from -48 dB (0) to 0 dB
// (full), after Env Gain.
inline float attackSeconds (float k) { return 0.0001f * std::pow (2000.0f, k); }
inline float attackKnob (float s) { return std::log (s / 0.0001f) / std::log (2000.0f); }
inline float releaseSeconds (float k) { return 0.005f * std::pow (400.0f, k); }
inline float releaseKnob (float s) { return std::log (s / 0.005f) / std::log (400.0f); }
constexpr float envFloorDb = -48.0f;

// A filter's fade in and out, 1 ms to 4 s. The knob's default is 3 ms exactly (the dip 0.1.0 gave every cube change),
// so the curve is pinned there: 3 ms times 4000 to the power of the knob's distance from it.
constexpr float fadeDefault = 0.13245323f; // log(3) / log(4000): 1 ms at 0, 4 s at 1
inline float fadeSeconds (float k) { return 0.003f * std::pow (4000.0f, k - fadeDefault); }
inline float fadeKnob (float s) { return fadeDefault + std::log (s / 0.003f) / std::log (4000.0f); }

// Depths at full turn: an LFO swings an axis by half its length each way (from the middle, end to end); the
// envelope moves it by its whole length.
constexpr float lfoSwing = 0.5f, envSwing = 1.0f;
// Spread: the left and right channels sit this far apart on the Morph axis at full turn.
constexpr float spreadWidth = 0.5f;
// Transform turned over to distortion: its whole swing (a knob end to end, or the LFO and envelope as far) is 100 dB
// of distortion, the distortion's whole range, at a gain of 1.0x.
constexpr float transformDistDb = 50.0f; // per half axis
constexpr float distLowest = -30.0f, distHighest = 70.0f;

// LFO Sync: Free runs at LFO Rate; the rest are a cycle's length in the host's beats, in step with the song.
inline const char* const lfoSyncChoices[] = { "Free", "8 Bars", "4 Bars", "2 Bars", "1 Bar", "1/2", "1/2T", "1/4", "1/4T",
                                              "1/8", "1/8T", "1/16", "1/16T", "1/32" };
constexpr double lfoSyncBeats[] = { 0.0, 32.0, 16.0, 8.0, 4.0, 2.0, 4.0 / 3.0, 1.0, 2.0 / 3.0, 0.5, 1.0 / 3.0, 0.25, 1.0 / 6.0, 0.125 };
static_assert (sizeof (lfoSyncBeats) / sizeof (lfoSyncBeats[0]) == sizeof (lfoSyncChoices) / sizeof (lfoSyncChoices[0]), "a length a choice");
inline const char* const lfoShapeChoices[] = { "Sine", "Triangle", "Saw Up", "Saw Down", "Square", "S&H", "Drift" };
enum class Shape : uint8_t { sine, triangle, sawUp, sawDown, square, sampleHold, drift };

// The sequencer's clocks: a trigger each time the song crosses a multiple of the length (beats), while it plays.
inline const char* const clockChoices[] = { "Off", "8 Bars", "4 Bars", "2 Bars", "1 Bar", "1/2", "1/2T", "1/4", "1/4T",
                                            "1/8", "1/8T", "1/16", "1/16T", "1/32" };
constexpr double clockBeats[] = { 0.0, 32.0, 16.0, 8.0, 4.0, 2.0, 4.0 / 3.0, 1.0, 2.0 / 3.0, 0.5, 1.0 / 3.0, 0.25, 1.0 / 6.0, 0.125 };
static_assert (sizeof (clockBeats) / sizeof (clockBeats[0]) == sizeof (clockChoices) / sizeof (clockChoices[0]), "a length a choice");
inline const char* const resetChoices[] = { "Off", "1 Bar", "2 Bars", "4 Bars", "8 Bars", "16 Bars" };
constexpr double resetBeats[] = { 0.0, 4.0, 8.0, 16.0, 32.0, 64.0 };
static_assert (sizeof (resetBeats) / sizeof (resetBeats[0]) == sizeof (resetChoices) / sizeof (resetChoices[0]), "a length a choice");
// The envelope follower as a trigger: each time it rises through Trig Level it fires one of the three inputs. It
// fires again once it has fallen 6 dB under the level.
inline const char* const envTriggerChoices[] = { "Off", "Inc", "Dec", "Reset" };
constexpr float trigHysteresisDb = 6.0f;
// Inc, Dec and Reset as parameters: a trigger each time one goes from Low to High (a gate's rising edge).
inline const char* const gateChoices[] = { "Low", "High" };
inline const char* const offOnChoices[] = { "Off", "On" };

// The sequences' names, as Sequence lists them (filters.h holds what they are).
inline const char* const sequenceChoices[] = { "Off", "Lowpasses", "Vowels", "Resonators", "Tone Shapers", "Acid Steps", "Vowel Talk",
                                               "Bar Gate", "Up And Down", "Seq 9", "Seq 10", "Seq 11", "Seq 12", "Seq 13", "Seq 14",
                                               "Seq 15", "Seq 16" };
// A filter's cube: Null (no filtering at all) or one of cubes.h's, numbered from 1.
struct CubeChoices
{
    const char* names[cubeCount + 1];
    constexpr CubeChoices() : names()
    {
        names[0] = "Null";
        for (int i = 0; i < cubeCount; ++i)
            names[i + 1] = cubeNames[i];
    }
};
inline constexpr CubeChoices cubeChoices {};

constexpr int slotCount = 64; // filters F01..F64

using P = Param;
#define CONT(id, name, module, lo, hi, def, unit) P { ParamId::id, name, module, lo, hi, def, Kind::continuous, Unit::unit, nullptr }
#define WHOLE(id, name, module, lo, hi, def, unit) P { ParamId::id, name, module, lo, hi, def, Kind::integer, Unit::unit, nullptr }
#define CHOICE(id, name, module, list, def) P { ParamId::id, name, module, 0.0f, (float) (sizeof (list) / sizeof (list[0]) - 1), def, Kind::choice, Unit::choice, list }

// In the order a host lists them, three to a page as Melodee's Edit page shows them: the knobs; the tone around them;
// the LFO; the envelope follower; the filter and the sequencer; the edit filter.
inline constexpr Param paramTable[] = {
    CONT (morph, "Morph", "Knobs", 0.0f, 1.0f, 0.0f, percent),
    CONT (freq, "Freq", "Knobs", 0.0f, 1.0f, 0.6f, percent),
    CONT (transform, "Transform", "Knobs", 0.0f, 1.0f, 0.3f, percent),
    CONT (emphasis, "Emphasis", "Knobs", -1.0f, 1.0f, 0.0f, bipolar),
    CONT (glide, "Glide", "Knobs", 0.0f, 1.0f, 0.4f, glide),
    CONT (drive, "Drive", "Output", 0.0f, 24.0f, 0.0f, decibels),
    CONT (mix, "Mix", "Output", 0.0f, 1.0f, 1.0f, percent),
    CONT (output, "Output", "Output", -24.0f, 12.0f, 0.0f, decibels),
    CONT (spread, "Spread", "Output", 0.0f, 1.0f, 0.0f, percent),
    CHOICE (lfoSync, "LFO Sync", "LFO", lfoSyncChoices, 0.0f),
    CONT (lfoRate, "LFO Rate", "LFO", 0.0f, 1.0f, 0.4f, lfoHz),
    CHOICE (lfoShape, "LFO Shape", "LFO", lfoShapeChoices, 0.0f),
    CONT (lfoPhase, "LFO Phase", "LFO", 0.0f, 1.0f, 0.25f, degrees),
    CONT (lfoMorph, "LFO > Morph", "LFO", -1.0f, 1.0f, 0.0f, bipolar),
    CONT (lfoFreq, "LFO > Freq", "LFO", -1.0f, 1.0f, 0.0f, bipolar),
    CONT (lfoTransform, "LFO > Transform", "LFO", -1.0f, 1.0f, 0.0f, bipolar),
    CONT (envGain, "Env Gain", "Envelope", 0.0f, 24.0f, 0.0f, decibels),
    CONT (envAttack, "Env Attack", "Envelope", 0.0f, 1.0f, 0.5f, attack),
    CONT (envRelease, "Env Release", "Envelope", 0.0f, 1.0f, 0.5f, release),
    CONT (envMorph, "Env > Morph", "Envelope", -1.0f, 1.0f, 0.0f, bipolar),
    CONT (envFreq, "Env > Freq", "Envelope", -1.0f, 1.0f, 0.0f, bipolar),
    CONT (envTransform, "Env > Transform", "Envelope", -1.0f, 1.0f, 0.0f, bipolar),
    WHOLE (filter, "Filter", "Sequencer", 1.0f, (float) slotCount, 1.0f, slot),
    CHOICE (sequence, "Sequence", "Sequencer", sequenceChoices, 0.0f),
    CHOICE (incClock, "Inc Clock", "Sequencer", clockChoices, 7.0f),
    CHOICE (decClock, "Dec Clock", "Sequencer", clockChoices, 0.0f),
    CHOICE (resetClock, "Reset Clock", "Sequencer", resetChoices, 0.0f),
    CHOICE (envTrigger, "Env Trigger", "Sequencer", envTriggerChoices, 0.0f),
    CONT (trigLevel, "Trig Level", "Sequencer", -48.0f, 0.0f, -18.0f, decibels),
    CHOICE (trigInc, "Inc", "Sequencer", gateChoices, 0.0f),
    CHOICE (trigDec, "Dec", "Sequencer", gateChoices, 0.0f),
    CHOICE (trigReset, "Reset", "Sequencer", gateChoices, 0.0f),
    P { ParamId::editCube, "Cube", "Edit Filter", 0.0f, (float) cubeCount, 0.0f, Kind::choice, Unit::choice, cubeChoices.names },
    WHOLE (morphOffset, "Morph Offset", "Edit Filter", -100.0f, 100.0f, 0.0f, offset),
    WHOLE (freqOffset, "Freq Offset", "Edit Filter", -100.0f, 100.0f, 0.0f, offset),
    WHOLE (transformOffset, "Transform Offset", "Edit Filter", -100.0f, 100.0f, 0.0f, offset),
    WHOLE (morphGain, "Morph Gain", "Edit Filter", -99.0f, 99.0f, 10.0f, times),
    WHOLE (freqGain, "Freq Gain", "Edit Filter", -99.0f, 99.0f, 10.0f, times),
    WHOLE (transformGain, "Transform Gain", "Edit Filter", -99.0f, 99.0f, 10.0f, times),
    CONT (enterFade, "Enter Fade", "Edit Filter", 0.0f, 1.0f, fadeDefault, fade),
    CONT (exitFade, "Exit Fade", "Edit Filter", 0.0f, 1.0f, fadeDefault, fade),
    WHOLE (gainLeft, "Left Gain", "Edit Filter", -40.0f, 40.0f, 0.0f, wholeDb),
    WHOLE (gainRight, "Right Gain", "Edit Filter", -40.0f, 40.0f, 0.0f, wholeDb),
    WHOLE (distLeft, "Left Dist", "Edit Filter", distLowest, distHighest, distLowest, wholeDb),
    WHOLE (distRight, "Right Dist", "Edit Filter", distLowest, distHighest, distLowest, wholeDb),
    CHOICE (transformDist, "Xform > Dist", "Edit Filter", offOnChoices, 0.0f),
};
#undef CONT
#undef WHOLE
#undef CHOICE

constexpr uint32_t paramCount = sizeof (paramTable) / sizeof (paramTable[0]);
static_assert (paramCount == ParamId::count - 1, "every id but the retired one has one row");
static_assert (paramCount <= 64, "the screen keeps a bit a row");
static_assert (ParamId::transformDist - ParamId::editCube == 13, "the edit filter's fields, in order");

// The table's row for an id (a host gives ids, so they are looked up).
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

inline bool isEditField (uint32_t id) { return id >= ParamId::editCube && id <= ParamId::transformDist; }

inline float clampParam (const Param& p, float v)
{
    if (! (v >= p.min)) v = p.min; // NaN too
    if (v > p.max) v = p.max;
    return p.kind == Kind::continuous ? v : std::round (v);
}

inline void formatParam (const Param& p, float v, char* text, uint32_t size)
{
    v = clampParam (p, v);
    // Each unit chosen by the value as it will print (99.96 ms prints as 100 ms, not 100.0 ms), so the text reads
    // back to the same text.
    auto time = [&] (float s) {
        const float ms = s * 1000.0f;
        if (ms < 9.995f) std::snprintf (text, size, "%.2f ms", ms);
        else if (ms < 99.95f) std::snprintf (text, size, "%.1f ms", ms);
        else if (ms < 999.5f) std::snprintf (text, size, "%.0f ms", ms);
        else std::snprintf (text, size, "%.2f s", s);
    };
    switch (p.unit)
    {
        case Unit::percent: std::snprintf (text, size, "%.0f %%", v * 100.0f); break;
        case Unit::bipolar: std::snprintf (text, size, "%+.0f %%", v * 100.0f); break;
        case Unit::decibels: std::snprintf (text, size, "%+.1f dB", v); break;
        case Unit::lfoHz: std::snprintf (text, size, lfoHz (v) < 0.9995f ? "%.3f Hz" : "%.2f Hz", lfoHz (v)); break;
        case Unit::glide: time (glideSeconds (v)); break;
        case Unit::attack: time (attackSeconds (v)); break;
        case Unit::release: time (releaseSeconds (v)); break;
        case Unit::fade: time (fadeSeconds (v)); break;
        case Unit::degrees: std::snprintf (text, size, "%.0f deg", v * 360.0f); break;
        case Unit::choice: std::snprintf (text, size, "%s", p.choices[(int) v]); break;
        case Unit::slot: std::snprintf (text, size, "F%02d", (int) v); break;
        case Unit::offset: std::snprintf (text, size, "%+d %%", (int) v); break;
        case Unit::times: std::snprintf (text, size, "%+.1fx", v / 10.0f); break;
        case Unit::wholeDb: std::snprintf (text, size, "%+d dB", (int) v); break;
    }
}

// Reads text as formatParam writes it (units optional); a choice by its name (any case, spaces around it ignored)
// or its number alone ("4 Bar" is neither: refused rather than read as choice 4); a filter by its number, with or
// without the F and its name after it ("F07 Bell Tower", "f7", "7").
inline bool parseParam (const Param& p, const char* text, float& v)
{
    if (p.kind == Kind::choice)
    {
        while (*text == ' ')
            ++text;
        size_t length = std::strlen (text);
        while (length > 0 && text[length - 1] == ' ')
            --length;
        const int n = (int) (p.max - p.min) + 1;
        for (int i = 0; i < n; ++i)
            if (std::strlen (p.choices[i]) == length && strncasecmp (text, p.choices[i], length) == 0)
                return v = (float) i, true;
        char* end = nullptr;
        const long number = std::strtol (text, &end, 10);
        if (end == text || end != text + length)
            return false;
        v = clampParam (p, (float) number);
        return true;
    }
    if (p.unit == Unit::slot)
    {
        while (*text == ' ')
            ++text;
        if (*text == 'F' || *text == 'f')
            ++text;
        char* end = nullptr;
        const long number = std::strtol (text, &end, 10);
        if (end == text || (*end != 0 && *end != ' '))
            return false;
        v = clampParam (p, (float) number);
        return true;
    }
    char* end = nullptr;
    double x = std::strtod (text, &end);
    if (end == text)
        return false;
    while (*end == ' ')
        ++end;
    const bool milli = (end[0] == 'm' && end[1] == 's');
    switch (p.unit)
    {
        case Unit::percent:
        case Unit::bipolar: v = (float) (x / 100.0); break;
        case Unit::lfoHz: v = lfoKnob ((float) x); break;
        case Unit::glide: v = glideKnob ((float) (milli ? x / 1000.0 : x)); break;
        case Unit::attack: v = attackKnob ((float) (milli ? x / 1000.0 : x)); break;
        case Unit::release: v = releaseKnob ((float) (milli ? x / 1000.0 : x)); break;
        case Unit::fade: v = fadeKnob ((float) (milli ? x / 1000.0 : x)); break;
        case Unit::degrees: v = (float) (x / 360.0); break;
        case Unit::times: v = (float) std::round (x * 10.0); break;
        default: v = (float) x; break;
    }
    v = clampParam (p, v); // a time or rate of 0 or less (a log of it NaN or -inf) is the knob's lowest
    return true;
}
} // namespace cortex
