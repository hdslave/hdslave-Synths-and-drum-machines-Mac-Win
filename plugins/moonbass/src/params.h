// MoonBass's parameters: one table for the CLAP side (names, ranges, text) and the engine (what a knob means).
// Ids are fixed forever: a saved project keeps its values by id. New parameters take new ids.
//
// The table's order is the Edit pages' order, three to a page as Melodee shows them on K1-K3: the eight panel knobs
// and Volume, then every menu parameter, so nothing is behind a second function.
#pragma once

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <strings.h>

#ifndef MOONBASS_VERSION
#define MOONBASS_VERSION "0.1.3"
#endif

namespace moonbass
{
namespace ParamId
{
enum : uint32_t
{
    tune = 0, pitch, decay, harmonics, pulse, noise, attack, eq, volume, gateTime, compression, play,
    metalNoise1, metalNoise2, noise2Mask,
    lfo1Wave, lfo1Speed, lfo1Int, lfo1Dest, lfo1Mode, lfo1Clock,
    lfo2Wave, lfo2Speed, lfo2Int, lfo2Dest, lfo2Mode, lfo2Clock,
    splitMode, pitchMode, dynamics,
    cv1Dest, cv1Amt, cv1In, cv2Dest, cv2Amt, cv2In, cv3Dest, cv3Amt, cv3In, cv4Dest, cv4Amt, cv4In,
    count
};
}

enum class Kind : uint8_t { continuous, integer, choice, button };
// How a value reads: what the knob does, for its text.
enum class Unit : uint8_t { percent, tuneOct, pitchHz, decay, eqHz, gateMs, lfoHz, volts, signedInt, metal1, metal2, mask, choice, button };

struct Param
{
    uint32_t id;
    const char* name;
    const char* module; // the page's name: the CLAP module path
    float min, max, def;
    Kind kind;
    Unit unit;
    const char* const* choices; // for Kind::choice: max - min + 1 names
};

// --- What the knobs mean ---------------------------------------------------------------------------------------
// Knobs run 0..1; these turn them into the engine's units. Ranges are the hardware's where the manuals give them
// (Pitch 10-175 Hz, Decay up to 2 s, Gate Time 0.1-16 ms), else chosen to cover what the panel does (README:
// "Where values were inferred").

// Pitch: the frequency the VCO settles on once the Tune envelope has gone, exponential, 10 Hz to 175 Hz.
constexpr float pitchLowHz = 10.0f, pitchHighHz = 175.0f;
constexpr float pitchSpanOctaves = 4.12928302f; // log2 (pitchHighHz / pitchLowHz): the knob's whole turn
inline float pitchHz (float k) { return pitchLowHz * std::exp2 (k * pitchSpanOctaves); }
inline float pitchKnob (float hz) { return std::log2 (hz / pitchLowHz) / pitchSpanOctaves; }

// Tune: the pitch envelope's depth (attack 0, a fixed decay), up to 4 octaves above Pitch at the hit.
constexpr float tuneOctavesMax = 4.0f;
constexpr float tuneDecaySeconds = 0.020f; // the pitch envelope's time constant (fixed on the hardware)

// Decay: the VCA's time to fall 60 dB, exponential, 30 ms to 2 s.
constexpr float decayShortest = 0.030f, decayLongest = 2.0f;
inline float decaySeconds (float k) { return decayShortest * std::pow (decayLongest / decayShortest, k); }
inline float decayKnob (float s) { return std::log (s / decayShortest) / std::log (decayLongest / decayShortest); }

// EQ: a one-pole (6 dB an octave, "flat slope") low pass over everything: 0 open (20 kHz), 100 % closed (150 Hz).
constexpr float eqOpenHz = 20000.0f, eqClosedHz = 150.0f;
constexpr float eqSpanOctaves = -7.05889369f; // log2 (eqClosedHz / eqOpenHz)
inline float eqHz (float k) { return eqOpenHz * std::pow (eqClosedHz / eqOpenHz, k); }
inline float eqKnob (float hz) { return std::log (hz / eqOpenHz) / std::log (eqClosedHz / eqOpenHz); }

// Gate Time: the trigger pulse the analog circuit gets, exponential, 0.1 ms to 16 ms. Pulse and noise sound for it.
constexpr float gateShortestMs = 0.1f, gateLongestMs = 16.0f;
inline float gateMs (float k) { return gateShortestMs * std::pow (gateLongestMs / gateShortestMs, k); }
inline float gateKnob (float ms) { return std::log (ms / gateShortestMs) / std::log (gateLongestMs / gateShortestMs); }

// LFO Speed (Clock: Free): exponential, 0.1 Hz to 40 Hz.
constexpr float lfoSlowestHz = 0.1f, lfoFastestHz = 40.0f;
inline float lfoHz (float k) { return lfoSlowestHz * std::pow (lfoFastestHz / lfoSlowestHz, k); }
inline float lfoKnob (float hz) { return std::log (hz / lfoSlowestHz) / std::log (lfoFastestHz / lfoSlowestHz); }
constexpr float lfoPitchOctaves = 2.0f;   // LFO Int at 100 % on Pitch: +-2 octaves
constexpr float lfoMetalPatterns = 128.0f; // LFO Int at 100 % on Metal Noise 1: +-128 patterns

// Metal noise: pattern numbers as the ModBase 09 counts them.
constexpr int metalPatterns = 499;

// CV inputs: 0-5 V; CV n Amt -128..127, so 5 V at 127 adds nearly the whole of the destination's range.
constexpr float cvVoltsMax = 5.0f;

inline const char* const waveChoices[] = { "Saw Up", "Saw Down", "Sine", "Sine -", "Triangle", "Triangle -", "Square", "Square -",
                                           "Pulse 60", "Pulse 70", "Pulse 80", "Pulse 88", "Pulse 40", "Pulse 30", "Pulse 20", "Pulse 12" };
inline const char* const lfoDestChoices[] = { "Pitch", "Metal Noise 1" };
// LFO Sync and LFO One Shot (MBase 11) as one control: Free (FrE, One Shot off), Retrigger (SYn), One Shot (SYn, onE).
inline const char* const lfoModeChoices[] = { "Free", "Retrigger", "One Shot" };
// Clock: Free runs at Speed; the others are a cycle a note value at the host's tempo, in step with its beats (the
// MBase 11's "Mid" is 1/4: a cycle a beat).
inline const char* const lfoClockChoices[] = { "Free", "2/1", "1/1", "1/2", "1/4", "1/4T", "1/8", "1/8T", "1/16", "1/16T", "1/32" };
constexpr double lfoClockBeats[] = { 0.0, 8.0, 4.0, 2.0, 1.0, 2.0 / 3.0, 0.5, 1.0 / 3.0, 0.25, 1.0 / 6.0, 0.125 };
static_assert (sizeof (lfoClockBeats) / sizeof (lfoClockBeats[0]) == sizeof (lfoClockChoices) / sizeof (lfoClockChoices[0]), "a length a choice");
// Split Mode: which keys play and from which the pitch counts. Any Note (new: any pad plays the kick as set), then
// the ModBase 09's SM1-SM5 and the MBase 11's SM2.
inline const char* const splitChoices[] = { "Any Note", "C1", "C1-C2", "C2-C3", "C3-C4", "C1-C6", "C1-F3" };
struct KeyRange { int16_t low, high; };
inline constexpr KeyRange splitRanges[] = { { 0, 127 }, { 36, 36 }, { 36, 48 }, { 48, 60 }, { 60, 72 }, { 36, 96 }, { 36, 65 } };
inline const char* const pitchModeChoices[] = { "Linear", "Semitones" };
constexpr float linearKeyStep = 0.0594630944f; // Pitch Mode Linear: each key adds this much of the range's lowest pitch (2^(1/12) - 1)
inline const char* const cvDestChoices[] = { "Off", "Tune", "Pitch", "Decay", "Harmonics", "Pulse", "Noise", "Attack", "EQ", "Compression",
                                             "Metal Noise 1", "Metal Noise 2", "LFO 1 Rate", "LFO 1 Int", "LFO 2 Rate", "LFO 2 Int", "Accent" };
namespace CvDest
{
enum : uint8_t { off, tune, pitch, decay, harmonics, pulse, noise, attack, eq, compression, metal1, metal2, lfo1Rate, lfo1Int, lfo2Rate, lfo2Int, accent, count };
}
static_assert (sizeof (cvDestChoices) / sizeof (cvDestChoices[0]) == CvDest::count, "a name a destination");

using P = Param;
#define CONT(id, name, module, lo, hi, def, unit) P { ParamId::id, name, module, lo, hi, def, Kind::continuous, Unit::unit, nullptr }
#define INT(id, name, module, lo, hi, def, unit) P { ParamId::id, name, module, lo, hi, def, Kind::integer, Unit::unit, nullptr }
#define CHOICE(id, name, module, list, def) P { ParamId::id, name, module, 0.0f, (float) (sizeof (list) / sizeof (list[0]) - 1), def, Kind::choice, Unit::choice, list }
#define BUTTON(id, name, module) P { ParamId::id, name, module, 0.0f, 1.0f, 0.0f, Kind::button, Unit::button, nullptr }

// In the order a host lists them, three to a page (see the README's page table).
inline constexpr Param paramTable[] = {
    // 1 2 3: the panel's knobs
    CONT (tune, "Tune", "Kick", 0.0f, 1.0f, 0.5f, tuneOct),
    CONT (pitch, "Pitch", "Kick", 0.0f, 1.0f, 0.5623f, pitchHz),
    CONT (decay, "Decay", "Kick", 0.0f, 1.0f, 0.6f, decay),
    CONT (harmonics, "Harmonics", "Kick", 0.0f, 1.0f, 0.1f, percent),
    CONT (pulse, "Pulse", "Kick", 0.0f, 1.0f, 0.5f, percent),
    CONT (noise, "Noise", "Kick", 0.0f, 1.0f, 0.3f, percent),
    CONT (attack, "Attack", "Kick", 0.0f, 1.0f, 0.5f, percent),
    CONT (eq, "EQ", "Kick", 0.0f, 1.0f, 0.3f, eqHz),
    CONT (volume, "Volume", "Kick", 0.0f, 1.0f, 0.85f, percent),
    // 4: the menu's sound parameters
    CONT (gateTime, "Gate Time", "Kick", 0.0f, 1.0f, 0.35f, gateMs),
    CONT (compression, "Compression", "Kick", 0.0f, 1.0f, 0.0f, percent),
    BUTTON (play, "Play", "Kick"),
    // 5: the noise generators
    INT (metalNoise1, "Metal Noise 1", "Noise", 0.0f, (float) metalPatterns, 1.0f, metal1),
    INT (metalNoise2, "Metal Noise 2", "Noise", 0.0f, (float) metalPatterns, 0.0f, metal2),
    INT (noise2Mask, "Noise 2 Mask", "Noise", 1.0f, 16.0f, 8.0f, mask),
    // 6 7: LFO 1 (the lower knobs' second function on the ModBase 09, then its menu)
    CHOICE (lfo1Wave, "LFO 1 Wave", "LFO 1", waveChoices, 2.0f),
    CONT (lfo1Speed, "LFO 1 Speed", "LFO 1", 0.0f, 1.0f, 0.5f, lfoHz),
    CONT (lfo1Int, "LFO 1 Int", "LFO 1", 0.0f, 1.0f, 0.0f, percent),
    CHOICE (lfo1Dest, "LFO 1 Dest", "LFO 1", lfoDestChoices, 0.0f),
    CHOICE (lfo1Mode, "LFO 1 Mode", "LFO 1", lfoModeChoices, 1.0f),
    CHOICE (lfo1Clock, "LFO 1 Clock", "LFO 1", lfoClockChoices, 0.0f),
    // 8 9: LFO 2
    CHOICE (lfo2Wave, "LFO 2 Wave", "LFO 2", waveChoices, 4.0f),
    CONT (lfo2Speed, "LFO 2 Speed", "LFO 2", 0.0f, 1.0f, 0.5f, lfoHz),
    CONT (lfo2Int, "LFO 2 Int", "LFO 2", 0.0f, 1.0f, 0.0f, percent),
    CHOICE (lfo2Dest, "LFO 2 Dest", "LFO 2", lfoDestChoices, 0.0f),
    CHOICE (lfo2Mode, "LFO 2 Mode", "LFO 2", lfoModeChoices, 1.0f),
    CHOICE (lfo2Clock, "LFO 2 Clock", "LFO 2", lfoClockChoices, 0.0f),
    // 10: how notes play it
    CHOICE (splitMode, "Split Mode", "Play", splitChoices, 0.0f),
    CHOICE (pitchMode, "Pitch Mode", "Play", pitchModeChoices, 1.0f),
    CONT (dynamics, "Dynamics", "Play", 0.0f, 1.0f, 1.0f, percent),
    // 11-14: the CV inputs, each with its destination and amount
    CHOICE (cv1Dest, "CV 1 Dest", "CV 1", cvDestChoices, 0.0f),
    INT (cv1Amt, "CV 1 Amt", "CV 1", -128.0f, 127.0f, 0.0f, signedInt),
    CONT (cv1In, "CV 1 In", "CV 1", 0.0f, cvVoltsMax, 0.0f, volts),
    CHOICE (cv2Dest, "CV 2 Dest", "CV 2", cvDestChoices, 0.0f),
    INT (cv2Amt, "CV 2 Amt", "CV 2", -128.0f, 127.0f, 0.0f, signedInt),
    CONT (cv2In, "CV 2 In", "CV 2", 0.0f, cvVoltsMax, 0.0f, volts),
    CHOICE (cv3Dest, "CV 3 Dest", "CV 3", cvDestChoices, 0.0f),
    INT (cv3Amt, "CV 3 Amt", "CV 3", -128.0f, 127.0f, 0.0f, signedInt),
    CONT (cv3In, "CV 3 In", "CV 3", 0.0f, cvVoltsMax, 0.0f, volts),
    CHOICE (cv4Dest, "CV 4 Dest", "CV 4", cvDestChoices, 0.0f),
    INT (cv4Amt, "CV 4 Amt", "CV 4", -128.0f, 127.0f, 0.0f, signedInt),
    CONT (cv4In, "CV 4 In", "CV 4", 0.0f, cvVoltsMax, 0.0f, volts),
};
#undef CONT
#undef INT
#undef CHOICE
#undef BUTTON

constexpr uint32_t paramCount = sizeof (paramTable) / sizeof (paramTable[0]);
static_assert (paramCount == ParamId::count, "every id has one row");
static_assert (paramCount % 3 == 0, "full pages of three");

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

inline void formatParam (const Param& p, float v, char* text, uint32_t size)
{
    v = clampParam (p, v);
    auto hz = [&] (float f) {
        if (f < 999.95f) std::snprintf (text, size, "%.1f Hz", f); // rounds to under 1000.0 Hz
        else std::snprintf (text, size, "%.2f kHz", f / 1000.0f);
    };
    switch (p.unit)
    {
        case Unit::percent: std::snprintf (text, size, "%.0f %%", v * 100.0f); break;
        case Unit::tuneOct: std::snprintf (text, size, "+%.2f oct", v * tuneOctavesMax); break;
        case Unit::pitchHz: hz (pitchHz (v)); break;
        case Unit::decay:
        {
            const float s = decaySeconds (v);
            if (s < 0.9995f) std::snprintf (text, size, "%.0f ms", s * 1000.0f); // rounds to under 1000 ms
            else std::snprintf (text, size, "%.2f s", s);
            break;
        }
        case Unit::eqHz: hz (eqHz (v)); break;
        case Unit::gateMs:
        {
            const float ms = gateMs (v);
            std::snprintf (text, size, ms < 0.995f ? "%.2f ms" : "%.1f ms", ms);
            break;
        }
        case Unit::lfoHz:
        {
            const float f = lfoHz (v);
            std::snprintf (text, size, f < 9.995f ? "%.2f Hz" : "%.1f Hz", f);
            break;
        }
        case Unit::volts: std::snprintf (text, size, "%.2f V", v); break;
        case Unit::signedInt: std::snprintf (text, size, "%+d", (int) v); break;
        case Unit::metal1:
            if (v < 0.5f) std::snprintf (text, size, "Off");
            else if (v < 1.5f) std::snprintf (text, size, "Noise");
            else std::snprintf (text, size, "%d", (int) v);
            break;
        case Unit::metal2:
            if (v < 0.5f) std::snprintf (text, size, "Off");
            else std::snprintf (text, size, "%d", (int) v);
            break;
        case Unit::mask: std::snprintf (text, size, "M%02d", (int) v); break;
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
    if ((p.unit == Unit::metal1 || p.unit == Unit::metal2) && strcasecmp (text, "Off") == 0)
        return v = 0.0f, true;
    if (p.unit == Unit::metal1 && strcasecmp (text, "Noise") == 0)
        return v = 1.0f, true;
    if (p.unit == Unit::mask && (text[0] == 'M' || text[0] == 'm'))
        ++text;
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
        case Unit::percent: v = (float) (x / 100.0); break;
        case Unit::tuneOct: v = (float) (x / tuneOctavesMax); break;
        case Unit::pitchHz: v = pitchKnob ((float) (kilo ? x * 1000.0 : x)); break;
        case Unit::decay: v = decayKnob ((float) (milli ? x / 1000.0 : x)); break;
        case Unit::eqHz: v = eqKnob ((float) (kilo ? x * 1000.0 : x)); break;
        case Unit::gateMs: v = gateKnob ((float) x); break;
        case Unit::lfoHz: v = lfoKnob ((float) x); break;
        default: v = (float) x; break;
    }
    if (! std::isfinite (v))
        return false;
    v = clampParam (p, v);
    return true;
}
} // namespace moonbass
