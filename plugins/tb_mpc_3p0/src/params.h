// The parameters: one table for the CLAP side (names, ranges, text) and the engine (what a knob means).
// Ids are fixed forever: a saved project keeps its values by id. New parameters take new ids.
#pragma once

#include "scales.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <strings.h>

#ifndef TBMPC_VERSION
#define TBMPC_VERSION "0.4.4"
#endif

namespace tbmpc
{
namespace ParamId
{
enum : uint32_t
{
    tuning = 0, cutoff, resonance, envMod, decay, accent, waveform, volume, glide,
    seedMode, seed, regen, density, scale, root, octave, steps, transpose, clockDiv, reset,
    mode, seedBack, swing, // 0.2.0
    gate, mutate, // 0.3.0
    drive, slideTime, accentDecay, filter, peakSpread, // 0.4.0
    count
};
}

enum class Kind : uint8_t { continuous, integer, choice, button };
// How a value reads.
enum class Unit : uint8_t { semitones, cutoffHz, percent, decay, seedHex, signedInt, plainInt, choice, button, slideTime, octaves };

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

// --- What the knobs mean -----------------------------------------------------------------------------------------
// 0 V from the sequencer is C2 (65.4 Hz), the 303's low C; Tuning moves it a semitone or so either way, as the
// panel's does (here up to an octave).
constexpr float voiceBaseHz = 65.406391f;

// Filter: the 303's diode ladder; Twin Peak, two 4-pole low passes, the lower subtracted from the higher (the band
// between them, a resonant peak at each edge); 18 dB, three equal stages in a loop that rings into oscillation at the
// top of Resonance.
enum Filter : int { filter303 = 0, filterTwinPeak = 1, filter18dB = 2 };
inline const char* const filterChoices[] = { "303", "Twin Peak", "18 dB" };

// Cutoff: 7 octaves, exponential. Each filter is calibrated as the 303's service notes set the 303's: with Cutoff at
// its middle and Resonance full (Env Mod at 0), the resonant peak sits at 500 Hz. That fixes each filter's nominal
// frequency at the middle (its peak at full Resonance is this far above it: 1.161 for the diode ladder, 0.981 for Twin
// Peak's lower filter, 1.774 for three stages). The diode ladder then runs from 38 Hz to 4.9 kHz nominal: the top
// where it was before the calibration, more room at the bottom.
constexpr float cutoffOctaves = 7.0f;
constexpr float filterCenterHz[3] = { 430.5f, 509.6f, 281.8f };
inline float nominalHz (int filter, float k) { return filterCenterHz[filter] * std::exp2 ((k - 0.5f) * cutoffOctaves); }
// A Cutoff saved by 0.1 - 0.3 (state version 1): 5.3 octaves from 130 Hz, the diode ladder's nominal frequency. The
// knob that gives the same frequency now.
inline float cutoffFromVersion1 (float k) { return 0.5f + (std::log2 (130.0f / filterCenterHz[0]) + 5.3f * k) / cutoffOctaves; }

// Resonance: each filter's feedback at full turn. The diode ladder would ring on its own from about 18.4 and Twin
// Peak's 4-pole ladders from 4: full turn stops short of both. Three stages ring from 8: the top of the knob (from
// 86 %) self-oscillates.
constexpr float filterFeedback[3] = { 17.0f, 3.7f, 8.6f };
inline float feedbackOf (int filter, float resonance) { return std::sqrt (resonance) * filterFeedback[filter]; }

// Peak Spread (Twin Peak): the upper peak, this many octaves over the lower (Cutoff's).
constexpr float spreadLowest = 0.5f, spreadHighest = 4.0f;

// What Cutoff reads: the frequency you hear as the filter's edge, with Resonance as it is and Env Mod at 0. For the
// low passes, the top of the passband (3 dB under the response's highest point: with Resonance at 0 the diode
// ladder's is about a ninth of its nominal frequency, from about 10 % on close to its peak); for Twin Peak, the band's
// lower edge. Worked out from the filters' analog responses (frequencies over the nominal one).
inline float filterGain2 (int filter, float k, float spreadOctaves, double w)
{
    struct C { double re, im; };
    auto mul = [] (C a, C b) { return C { a.re * b.re - a.im * b.im, a.re * b.im + a.im * b.re }; };
    auto inv2 = [] (C a) { return 1.0 / (a.re * a.re + a.im * a.im); };
    const C s { 0.0, w };
    if (filter == filter303)
    {
        // s^4 + 7 s^3 + 15 s^2 + 10 s + 1 + k
        const C s2 = mul (s, s), s3 = mul (s2, s), s4 = mul (s3, s);
        return inv2 (C { s4.re + 7.0 * s3.re + 15.0 * s2.re + 1.0 + k, s4.im + 7.0 * s3.im + 15.0 * s2.im + 10.0 * s.im });
    }
    if (filter == filter18dB)
    {
        const C a { 1.0, w }, a3 = mul (mul (a, a), a);
        return inv2 (C { a3.re + k, a3.im });
    }
    // Twin Peak: 1 / ((1 + s / r)^4 + k) - 1 / ((1 + s)^4 + k)
    auto lowPass = [&] (double scale) {
        const C a { 1.0, w / scale }, a2 = mul (a, a), d = mul (a2, a2);
        const C den { d.re + k, d.im };
        const double m = den.re * den.re + den.im * den.im;
        return C { den.re / m, -den.im / m };
    };
    const C hi = lowPass (std::exp2 ((double) spreadOctaves)), lo = lowPass (1.0);
    const C d { hi.re - lo.re, hi.im - lo.im };
    return d.re * d.re + d.im * d.im;
}

inline float cutoffEdgeRatio (int filter, float resonance, float spreadOctaves)
{
    // The last answer, kept per thread (the screen asks for Cutoff's text several times a paint).
    thread_local struct { int filter = -1; float resonance = 0.0f, spread = 0.0f, ratio = 1.0f; } last;
    if (last.filter == filter && last.resonance == resonance && (filter != filterTwinPeak || last.spread == spreadOctaves))
        return last.ratio;
    const float k = feedbackOf (filter, resonance);
    constexpr int points = 480;
    constexpr double from = -2.5, to = 1.5; // log10 of the frequency over the nominal one
    auto at = [] (int i) { return std::pow (10.0, from + (to - from) * i / (points - 1)); };
    // The highest point: for Twin Peak, the lower peak's (below the band's middle), whichever peak is taller.
    const double below = filter == filterTwinPeak ? std::exp2 (0.5 * (double) spreadOctaves) : 1.0e9;
    int top = 0;
    double best = -1.0;
    for (int i = 0; i < points && at (i) < below; ++i)
    {
        const double g = filterGain2 (filter, k, spreadOctaves, at (i));
        if (g > best)
            best = g, top = i;
    }
    auto remember = [&] (float ratio) {
        last.filter = filter, last.resonance = resonance, last.spread = spreadOctaves, last.ratio = ratio;
        return ratio;
    };
    const double half = best * 0.5;
    const int step = filter == filterTwinPeak ? -1 : 1; // the low passes' upper edge; the band's lower one
    int i = top;
    while (i + step >= 0 && i + step < points && filterGain2 (filter, k, spreadOctaves, at (i + step)) >= half)
        i += step;
    if (i + step < 0 || i + step >= points)
        return remember ((float) at (i));
    double inside = std::log (at (i)), outside = std::log (at (i + step));
    for (int n = 0; n < 40; ++n)
    {
        const double mid = 0.5 * (inside + outside);
        (filterGain2 (filter, k, spreadOctaves, std::exp (mid)) >= half ? inside : outside) = mid;
    }
    return remember ((float) std::exp (0.5 * (inside + outside)));
}

// The other values a parameter's text depends on: Cutoff reads by Filter, Resonance and Peak Spread.
struct TextContext
{
    float resonance = 0.6f, filter = 0.0f, spread = 1.5f;
};
inline float cutoffHz (float k, const TextContext& c)
{
    const int f = (int) c.filter;
    return nominalHz (f, k) * cutoffEdgeRatio (f, c.resonance, c.spread);
}
inline float cutoffKnob (float hz, const TextContext& c)
{
    const int f = (int) c.filter;
    return 0.5f + std::log2 (hz / (filterCenterHz[f] * cutoffEdgeRatio (f, c.resonance, c.spread))) / cutoffOctaves;
}

// Env Mod at full: the MEG opens the cutoff 4.5 octaves above a floor 1.3 octaves under the Cutoff knob.
constexpr float envModOctaves = 4.5f, envModFloorOctaves = 1.3f;

// Decay: the MEG's time to fall 40 dB, 200 ms to 2 s. Accent Decay is an accented step's, over the same range; its
// default, the shortest, is the 303's (whose accented steps always take it).
constexpr float decayShortest = 0.2f, decayLongest = 2.0f;
inline float decaySeconds (float k) { return decayShortest * std::pow (decayLongest / decayShortest, k); }
inline float decayKnob (float s) { return std::log (s / decayShortest) / std::log (decayLongest / decayShortest); }

// Slide Time: the 303 glide's time constant (Slide: 303, and a note's glide in Mode: Notes), 5 ms to 200 ms; it reads
// as the time to arrive (95 %, three time constants). The 303's own, 20 ms (there in 60 ms), is the default, and the
// default gives exactly that constant (the sound is the same to the bit as before the knob was added).
constexpr float glideSeconds = 0.020f;
constexpr float slideShortest = 0.005f, slideLongest = 0.200f;
constexpr float slideTimeDefault = 0.37581190f; // log (20 / 5) / log (200 / 5)
inline float slideSeconds (float k)
{
    return k == slideTimeDefault ? glideSeconds : slideShortest * std::pow (slideLongest / slideShortest, k);
}
inline float slideKnob (float s)
{
    if (std::fabs (s - glideSeconds) < 1.0e-6f) // the default, exactly (typed as "60 ms")
        return slideTimeDefault;
    return std::log (s / slideShortest) / std::log (slideLongest / slideShortest);
}

// Drive: after the VCA, a soft clipper driven up to +30 dB, its level made up so a turn changes the colour more than
// the loudness. 0 is no drive at all (the voice as before).
constexpr float driveOctaves = 5.0f;


// The module's update rate: TB-3PO's slide moves a step each tick of it.
constexpr double moduleTickRate = 1.0e6 / 60.0;

inline const char* const waveChoices[] = { "Saw", "Square" };
inline const char* const glideChoices[] = { "TB-3PO", "303" };
inline const char* const seedModeChoices[] = { "Random", "Locked" };
inline const char* const modeChoices[] = { "TB-3PO", "Notes" };
// Mode: Notes: a note at this velocity or louder is accented (as 303-style synths read MIDI).
constexpr float accentVelocity = 100.0f / 127.0f;
// 0 V is C2: in Mode: Notes, MIDI note 36 (C2) plays the pitch the sequencer's 0 V does.
constexpr int noteAtZeroVolts = 36;
// Pitch bend's range, and how far the mod wheel at its top opens the cutoff.
constexpr float bendSemitones = 2.0f, modWheelOctaves = 3.0f;
inline const char* const rootChoices[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
inline const char* const resetChoices[] = { "Play", "1 Bar", "2 Bars", "4 Bars", "8 Bars", "Off" };
constexpr int resetBars[] = { 0, 1, 2, 4, 8, 0 };
// The clock: a step is this many quarter notes at the host's tempo.
inline const char* const clockDivChoices[] = { "1/2", "1/4", "1/4T", "1/8", "1/8T", "1/16", "1/16T", "1/32", "1/32T", "1/64" };
constexpr double clockDivBeats[] = { 2.0, 1.0, 2.0 / 3.0, 0.5, 1.0 / 3.0, 0.25, 1.0 / 6.0, 0.125, 1.0 / 12.0, 0.0625 };
static_assert (sizeof (clockDivBeats) / sizeof (clockDivBeats[0]) == sizeof (clockDivChoices) / sizeof (clockDivChoices[0]), "a length a choice");

using P = Param;
#define CONT(id, name, module, lo, hi, def, unit) P { ParamId::id, name, module, lo, hi, def, Kind::continuous, Unit::unit, nullptr }
#define INT(id, name, module, lo, hi, def, unit) P { ParamId::id, name, module, lo, hi, def, Kind::integer, Unit::unit, nullptr }
#define CHOICE(id, name, module, list, def) P { ParamId::id, name, module, 0.0f, (float) (sizeof (list) / sizeof (list[0]) - 1), def, Kind::choice, Unit::choice, list }
#define BUTTON(id, name, module) P { ParamId::id, name, module, 0.0f, 1.0f, 0.0f, Kind::button, Unit::button, nullptr }

// In the order a host lists them, three to a page as Melodee's Edit page shows them (when the plug-in's own page is not
// there): the 303's panel and what 0.4 added to it (Tuning, Cutoff, Resonance; Env Mod, Decay, Accent; Waveform,
// Volume, Drive; Filter, Peak Spread, Accent Decay; the slide, its time and what plays the voice), then TB-3PO's
// settings (the seed; Seed Back, Mutate, density; scale, root, octave; length, gate, transpose) and the clock.
inline constexpr Param paramTable[] = {
    CONT (tuning, "Tuning", "Voice", -12.0f, 12.0f, 0.0f, semitones),
    CONT (cutoff, "Cutoff", "Voice", 0.0f, 1.0f, 0.518f, cutoffHz),
    CONT (resonance, "Resonance", "Voice", 0.0f, 1.0f, 0.6f, percent),
    CONT (envMod, "Env Mod", "Voice", 0.0f, 1.0f, 0.5f, percent),
    CONT (decay, "Decay", "Voice", 0.0f, 1.0f, 0.35f, decay),
    CONT (accent, "Accent", "Voice", 0.0f, 1.0f, 0.6f, percent),
    CHOICE (waveform, "Waveform", "Voice", waveChoices, 0.0f),
    CONT (volume, "Volume", "Voice", 0.0f, 1.0f, 0.75f, percent),
    CONT (drive, "Drive", "Voice", 0.0f, 1.0f, 0.0f, percent),
    CHOICE (filter, "Filter", "Voice", filterChoices, 0.0f),
    CONT (peakSpread, "Peak Spread", "Voice", spreadLowest, spreadHighest, 1.5f, octaves),
    CONT (accentDecay, "Accent Decay", "Voice", 0.0f, 1.0f, 0.0f, decay),
    CHOICE (glide, "Slide", "Voice", glideChoices, 0.0f),
    CONT (slideTime, "Slide Time", "Voice", 0.0f, 1.0f, slideTimeDefault, slideTime),
    CHOICE (mode, "Mode", "TB-3PO", modeChoices, 0.0f),
    CHOICE (seedMode, "Seed Mode", "TB-3PO", seedModeChoices, 0.0f),
    INT (seed, "Seed", "TB-3PO", 0.0f, 65535.0f, 12345.0f, seedHex),
    BUTTON (regen, "Regen", "TB-3PO"),
    BUTTON (seedBack, "Seed Back", "TB-3PO"),
    CONT (mutate, "Mutate", "TB-3PO", 0.0f, 1.0f, 0.0f, percent),
    INT (density, "Density", "TB-3PO", -7.0f, 7.0f, 5.0f, signedInt),
    P { ParamId::scale, "Scale", "TB-3PO", 0.0f, (float) (scaleCount - 1), (float) appletDefaultScale, Kind::choice, Unit::choice, scaleNames },
    CHOICE (root, "Root", "TB-3PO", rootChoices, 0.0f),
    INT (octave, "Octave", "TB-3PO", -3.0f, 3.0f, 0.0f, signedInt),
    INT (steps, "Steps", "TB-3PO", 1.0f, 32.0f, 16.0f, plainInt),
    CONT (gate, "Gate", "TB-3PO", 0.1f, 0.95f, 0.5f, percent),
    INT (transpose, "Transpose", "TB-3PO", -36.0f, 36.0f, 0.0f, signedInt),
    CHOICE (clockDiv, "Clock Div", "Clock", clockDivChoices, 5.0f),
    CONT (swing, "Swing", "Clock", 0.5f, 0.75f, 0.5f, percent),
    CHOICE (reset, "Reset", "Clock", resetChoices, 0.0f),
};
#undef CONT
#undef INT
#undef CHOICE
#undef BUTTON

constexpr uint32_t paramCount = sizeof (paramTable) / sizeof (paramTable[0]);
static_assert (paramCount == ParamId::count, "every id has one row");

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
    switch (p.unit)
    {
        case Unit::semitones: std::snprintf (text, size, "%+.2f st", v); break;
        case Unit::cutoffHz:
        {
            const float hz = cutoffHz (v, context);
            if (hz < 1000.0f) std::snprintf (text, size, "%.0f Hz", hz);
            else std::snprintf (text, size, "%.2f kHz", hz / 1000.0f);
            break;
        }
        case Unit::percent: std::snprintf (text, size, "%.0f %%", v * 100.0f); break;
        case Unit::decay:
        {
            const float s = decaySeconds (v);
            if (s < 1.0f) std::snprintf (text, size, "%.0f ms", s * 1000.0f);
            else std::snprintf (text, size, "%.2f s", s);
            break;
        }
        case Unit::octaves: std::snprintf (text, size, "%.2f oct", v); break;
        case Unit::slideTime: std::snprintf (text, size, "%.0f ms", slideSeconds (v) * 3000.0f); break; // the time to arrive
        case Unit::seedHex: std::snprintf (text, size, "%04x", (unsigned) v); break; // as the module shows it
        case Unit::signedInt: std::snprintf (text, size, "%+d", (int) v); break;
        case Unit::plainInt: std::snprintf (text, size, "%d", (int) v); break;
        case Unit::choice: std::snprintf (text, size, "%s", p.choices[(int) v]); break;
        case Unit::button: std::snprintf (text, size, "%s", v >= 0.5f ? "Pressed" : "Press"); break;
    }
}

// Reads text as formatParam writes it (units optional); a choice by its name or its number; the seed in hex.
inline bool parseParam (const Param& p, const char* text, float& v, const TextContext& context = {})
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
    if (p.unit == Unit::seedHex)
    {
        const unsigned long x = std::strtoul (text, &end, 16);
        if (end == text)
            return false;
        v = clampParam (p, (float) x);
        return true;
    }
    const double x = std::strtod (text, &end);
    if (end == text)
        return false;
    while (*end == ' ')
        ++end;
    const bool kilo = (*end == 'k' || *end == 'K');
    const bool milli = (end[0] == 'm' && end[1] == 's');
    switch (p.unit)
    {
        case Unit::cutoffHz: v = cutoffKnob ((float) (kilo ? x * 1000.0 : x), context); break;
        case Unit::decay: v = decayKnob ((float) (milli ? x / 1000.0 : x)); break;
        case Unit::slideTime: v = slideKnob ((float) (milli ? x / 1000.0 : x) / 3.0f); break;
        case Unit::percent: v = (float) (x / 100.0); break;
        default: v = (float) x; break;
    }
    v = clampParam (p, v);
    return true;
}
} // namespace tbmpc
