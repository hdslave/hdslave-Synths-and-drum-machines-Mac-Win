// Necronomicon's parameters: one table for the CLAP side (names, ranges, text) and the engine (what a knob means).
// Ids are fixed forever: a saved project keeps its values by id. New parameters take new ids.
#pragma once

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <strings.h>

#ifndef NECRONOMICON_VERSION
#define NECRONOMICON_VERSION "0.1.5"
#endif

namespace necronomicon
{
namespace ParamId
{
enum : uint32_t
{
    vco1Freq = 0, vco1Wave, vco1Sub1, vco1Sub2, vco2Freq, vco2Wave, vco2Sub1, vco2Sub2,
    vco1Level, vco1Sub1Level, vco1Sub2Level, vco2Level, vco2Sub1Level, vco2Sub2Level,
    cutoff, resonance, vcfEgAmt, vcfAttack, vcfDecay, vcaAttack, vcaDecay, egMode, volume,
    quantize, seqOct,
    seq1Step1, seq1Step2, seq1Step3, seq1Step4, seq2Step1, seq2Step2, seq2Step3, seq2Step4,
    seq1Osc, seq1Sub1, seq1Sub2, seq2Osc, seq2Sub1, seq2Sub2,
    rhythm1, rhythm2, rhythm3, rhythm4,
    rhythm1Seq1, rhythm2Seq1, rhythm3Seq1, rhythm4Seq1,
    rhythm1Seq2, rhythm2Seq2, rhythm3Seq2, rhythm4Seq2,
    rhythmLogic, clockDiv, run, reset, next, trigger, fineTune,
    count
};
}

enum class Kind : uint8_t { continuous, integer, choice, button };
// How a value reads: what the knob does, for its text.
enum class Unit : uint8_t { vcoHz, cutoffHz, attack, decay, percent, bipolar, divisor, plainInt, step, cents, choice, button };

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
// Knobs run 0..1 (amounts and steps -1..1); these turn them into the engine's units. Each range is the panel's
// where the manual gives it, else chosen to cover what the panel does (README: "Where values were inferred").

// VCO frequency: four octaves, middle C (261.6 Hz) fully left to the top C (4,186 Hz) fully right. The sequencers
// and notes reach below and above that; nothing goes past 10 kHz.
constexpr float vcoLowHz = 261.625565f, vcoOctaves = 4.0f, vcoHighestHz = 10000.0f;
inline float vcoHz (float k) { return vcoLowHz * std::exp2 (k * vcoOctaves); }
inline float vcoKnob (float hz) { return std::log2 (hz / vcoLowHz) / vcoOctaves; }

// Cutoff: exponential, 20 Hz to about 20 kHz.
constexpr float cutoffLowHz = 20.0f, cutoffOctaves = 10.0f;
inline float cutoffHz (float k) { return cutoffLowHz * std::exp2 (k * cutoffOctaves); }
inline float cutoffKnob (float hz) { return std::log2 (hz / cutoffLowHz) / cutoffOctaves; }

// Attack: the rise to the top, 1 ms to 10 s. Decay: the fall of 60 dB, 5 ms to 10 s. Both exponential over the knob.
constexpr float attackShortest = 0.001f, attackLongest = 10.0f;
constexpr float decayShortest = 0.005f, decayLongest = 10.0f;
inline float attackSeconds (float k) { return attackShortest * std::pow (attackLongest / attackShortest, k); }
inline float attackKnob (float s) { return std::log (s / attackShortest) / std::log (attackLongest / attackShortest); }
inline float decaySeconds (float k) { return decayShortest * std::pow (decayLongest / decayShortest, k); }
inline float decayKnob (float s) { return std::log (s / decayShortest) / std::log (decayLongest / decayShortest); }

// VCF EG Amt at +-100 %: the envelope moves the cutoff 8 octaves (an 8 V envelope into 1 V/octave).
constexpr float vcfEgOctaves = 8.0f;
// Resonance: the ladder's feedback; it self-oscillates from about 90 % of the turn.
constexpr float maxFeedback = 4.4f;
// The middle WAVE position: SUB 1's sawtooth sweeps the VCO's pulse width between 5 % and 95 %.
constexpr float pwmDepth = 0.45f;
// A sequencer assigned to a subharmonic moves its divisor by 3 for each octave the step is worth (the 16 divisors
// over about five volts of the knob's travel); up is a smaller divisor, a higher note.
constexpr float divisorsPerOctave = 3.0f;
// Fine Tune at +-100 %: +-50 cents on both VCOs.
constexpr float fineTuneCents = 50.0f;
// A MIDI note moves both VCOs from middle C (note 60): it is the panel's 1 V/octave input, normalled to both.
constexpr int middleC = 60;

inline const char* const offOn[] = { "Off", "On" };
// UP: the VCO and its two subs square; MIDDLE: the VCO a pulse whose width SUB 1's sawtooth moves, the subs
// sawtooth; DOWN: all three sawtooth.
inline const char* const waveChoices[] = { "Square", "Pulse/Saw", "Saw" };
inline const char* const egModeChoices[] = { "Off", "On", "Hold" };
inline const char* const quantizeChoices[] = { "Off", "12-ET", "8-ET", "12-JI", "8-JI" };
inline const char* const seqOctChoices[] = { "+/-1", "+/-2", "+/-5" };
constexpr float seqOctaves[] = { 1.0f, 2.0f, 5.0f };
inline const char* const logicChoices[] = { "OR", "XOR" };
inline const char* const runChoices[] = { "Stop", "Run" };
// The clock replaces the panel's Tempo: the rhythm generators divide a clock of this much of a beat at the host's
// tempo (1/16 is the panel's own clock under MIDI clock: four a quarter note).
inline const char* const clockDivChoices[] = { "1/2", "1/4", "1/4T", "1/8", "1/8T", "1/16", "1/16T", "1/32", "1/32T", "1/64" };
constexpr double clockDivBeats[] = { 2.0, 1.0, 2.0 / 3.0, 0.5, 1.0 / 3.0, 0.25, 1.0 / 6.0, 0.125, 1.0 / 12.0, 0.0625 };
static_assert (sizeof (clockDivBeats) / sizeof (clockDivBeats[0]) == sizeof (clockDivChoices) / sizeof (clockDivChoices[0]), "a length a choice");

using P = Param;
#define CONT(id, name, module, lo, hi, def, unit) P { ParamId::id, name, module, lo, hi, def, Kind::continuous, Unit::unit, nullptr }
#define INT(id, name, module, lo, hi, def, unit) P { ParamId::id, name, module, lo, hi, def, Kind::integer, Unit::unit, nullptr }
#define CHOICE(id, name, module, list, def) P { ParamId::id, name, module, 0.0f, (float) (sizeof (list) / sizeof (list[0]) - 1), def, Kind::choice, Unit::choice, list }
#define BUTTON(id, name, module) P { ParamId::id, name, module, 0.0f, 1.0f, 0.0f, Kind::button, Unit::button, nullptr }

// In the order a host lists them, three to a page as Melodee's Edit page shows them: each VCO and its subs, their
// levels, the switches, the filter, the envelopes, the sequencers, the rhythm generators, the transport.
// Default steps: Seq 1 a minor-seventh arpeggio (0, +7, +3, +10 semitones at +/-2 octaves), Seq 2 under it.
inline constexpr Param paramTable[] = {
    CONT (vco1Freq, "VCO 1 Freq", "VCO 1", 0.0f, 1.0f, 0.0f, vcoHz),
    INT (vco1Sub1, "VCO 1 Sub 1", "VCO 1", 1.0f, 16.0f, 2.0f, divisor),
    INT (vco1Sub2, "VCO 1 Sub 2", "VCO 1", 1.0f, 16.0f, 3.0f, divisor),
    CONT (vco1Level, "VCO 1 Level", "Mixer", 0.0f, 1.0f, 0.7f, percent),
    CONT (vco1Sub1Level, "VCO 1 Sub 1 Level", "Mixer", 0.0f, 1.0f, 0.5f, percent),
    CONT (vco1Sub2Level, "VCO 1 Sub 2 Level", "Mixer", 0.0f, 1.0f, 0.0f, percent),
    CONT (vco2Freq, "VCO 2 Freq", "VCO 2", 0.0f, 1.0f, 7.0f / 48.0f, vcoHz),
    INT (vco2Sub1, "VCO 2 Sub 1", "VCO 2", 1.0f, 16.0f, 2.0f, divisor),
    INT (vco2Sub2, "VCO 2 Sub 2", "VCO 2", 1.0f, 16.0f, 4.0f, divisor),
    CONT (vco2Level, "VCO 2 Level", "Mixer", 0.0f, 1.0f, 0.5f, percent),
    CONT (vco2Sub1Level, "VCO 2 Sub 1 Level", "Mixer", 0.0f, 1.0f, 0.0f, percent),
    CONT (vco2Sub2Level, "VCO 2 Sub 2 Level", "Mixer", 0.0f, 1.0f, 0.4f, percent),
    CHOICE (vco1Wave, "VCO 1 Wave", "VCO 1", waveChoices, 2.0f),
    CHOICE (vco2Wave, "VCO 2 Wave", "VCO 2", waveChoices, 2.0f),
    CHOICE (quantize, "Quantize", "Sequencer", quantizeChoices, 1.0f),
    CONT (cutoff, "Cutoff", "VCF", 0.0f, 1.0f, 0.55f, cutoffHz),
    CONT (resonance, "Resonance", "VCF", 0.0f, 1.0f, 0.2f, percent),
    CONT (vcfEgAmt, "VCF EG Amt", "VCF", -1.0f, 1.0f, 0.3f, bipolar),
    CONT (vcfAttack, "VCF Attack", "VCF EG", 0.0f, 1.0f, 0.1f, attack),
    CONT (vcfDecay, "VCF Decay", "VCF EG", 0.0f, 1.0f, 0.4f, decay),
    CHOICE (egMode, "EG", "EG", egModeChoices, 1.0f),
    CONT (vcaAttack, "VCA Attack", "VCA EG", 0.0f, 1.0f, 0.05f, attack),
    CONT (vcaDecay, "VCA Decay", "VCA EG", 0.0f, 1.0f, 0.55f, decay),
    CONT (volume, "Volume", "VCA", 0.0f, 1.0f, 0.7f, percent),
    CONT (seq1Step1, "Seq 1 Step 1", "Sequencer 1", -1.0f, 1.0f, 0.0f, step),
    CONT (seq1Step2, "Seq 1 Step 2", "Sequencer 1", -1.0f, 1.0f, 7.0f / 24.0f, step),
    CONT (seq1Step3, "Seq 1 Step 3", "Sequencer 1", -1.0f, 1.0f, 3.0f / 24.0f, step),
    CONT (seq1Step4, "Seq 1 Step 4", "Sequencer 1", -1.0f, 1.0f, 10.0f / 24.0f, step),
    CONT (seq2Step1, "Seq 2 Step 1", "Sequencer 2", -1.0f, 1.0f, 0.0f, step),
    CONT (seq2Step2, "Seq 2 Step 2", "Sequencer 2", -1.0f, 1.0f, -5.0f / 24.0f, step),
    CONT (seq2Step3, "Seq 2 Step 3", "Sequencer 2", -1.0f, 1.0f, 2.0f / 24.0f, step),
    CONT (seq2Step4, "Seq 2 Step 4", "Sequencer 2", -1.0f, 1.0f, -2.0f / 24.0f, step),
    CHOICE (seqOct, "Seq Oct", "Sequencer", seqOctChoices, 1.0f),
    CHOICE (seq1Osc, "Seq 1 > Osc 1", "Sequencer 1", offOn, 1.0f),
    CHOICE (seq1Sub1, "Seq 1 > Sub 1", "Sequencer 1", offOn, 0.0f),
    CHOICE (seq1Sub2, "Seq 1 > Sub 2", "Sequencer 1", offOn, 0.0f),
    CHOICE (seq2Osc, "Seq 2 > Osc 2", "Sequencer 2", offOn, 1.0f),
    CHOICE (seq2Sub1, "Seq 2 > Sub 1", "Sequencer 2", offOn, 0.0f),
    CHOICE (seq2Sub2, "Seq 2 > Sub 2", "Sequencer 2", offOn, 0.0f),
    INT (rhythm1, "Rhythm 1", "Rhythm", 1.0f, 16.0f, 4.0f, plainInt),
    CHOICE (rhythm1Seq1, "Rhythm 1 > Seq 1", "Rhythm", offOn, 1.0f),
    CHOICE (rhythm1Seq2, "Rhythm 1 > Seq 2", "Rhythm", offOn, 0.0f),
    INT (rhythm2, "Rhythm 2", "Rhythm", 1.0f, 16.0f, 3.0f, plainInt),
    CHOICE (rhythm2Seq1, "Rhythm 2 > Seq 1", "Rhythm", offOn, 0.0f),
    CHOICE (rhythm2Seq2, "Rhythm 2 > Seq 2", "Rhythm", offOn, 1.0f),
    INT (rhythm3, "Rhythm 3", "Rhythm", 1.0f, 16.0f, 5.0f, plainInt),
    CHOICE (rhythm3Seq1, "Rhythm 3 > Seq 1", "Rhythm", offOn, 0.0f),
    CHOICE (rhythm3Seq2, "Rhythm 3 > Seq 2", "Rhythm", offOn, 0.0f),
    INT (rhythm4, "Rhythm 4", "Rhythm", 1.0f, 16.0f, 7.0f, plainInt),
    CHOICE (rhythm4Seq1, "Rhythm 4 > Seq 1", "Rhythm", offOn, 0.0f),
    CHOICE (rhythm4Seq2, "Rhythm 4 > Seq 2", "Rhythm", offOn, 0.0f),
    CHOICE (rhythmLogic, "Rhythm Logic", "Rhythm", logicChoices, 0.0f),
    CHOICE (clockDiv, "Clock Div", "Transport", clockDivChoices, 5.0f),
    CHOICE (run, "Run", "Transport", runChoices, 1.0f),
    BUTTON (reset, "Reset", "Transport"),
    BUTTON (next, "Next", "Transport"),
    BUTTON (trigger, "Trigger", "Transport"),
    CONT (fineTune, "Fine Tune", "VCO", -1.0f, 1.0f, 0.0f, cents),
};
#undef CONT
#undef INT
#undef CHOICE
#undef BUTTON

constexpr uint32_t paramCount = sizeof (paramTable) / sizeof (paramTable[0]);
static_assert (paramCount == ParamId::count, "every id has one row");

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

inline float clampParam (const Param& p, float v)
{
    if (! (v >= p.min)) v = p.min; // NaN too
    if (v > p.max) v = p.max;
    return p.kind == Kind::continuous ? v : std::round (v);
}

// The octaves a step knob at full turn is worth, from Seq Oct's value.
inline float stepOctaves (float seqOctValue)
{
    const int i = (int) std::lround (seqOctValue);
    return seqOctaves[i < 0 ? 0 : (i > 2 ? 2 : i)];
}

// A value's text. `octaves` is what a step knob at full turn is worth now (Seq Oct's setting): steps read in
// semitones at it.
inline void formatParam (const Param& p, float v, char* text, uint32_t size, float octaves = 2.0f)
{
    v = clampParam (p, v);
    auto time = [&] (float s) {
        if (s < 0.00995f) std::snprintf (text, size, "%.1f ms", s * 1000.0f);
        else if (s < 1.0f) std::snprintf (text, size, "%.0f ms", s * 1000.0f);
        else std::snprintf (text, size, "%.2f s", s);
    };
    auto hz = [&] (float f) {
        if (f < 1000.0f) std::snprintf (text, size, "%.1f Hz", f);
        else std::snprintf (text, size, "%.2f kHz", f / 1000.0f);
    };
    switch (p.unit)
    {
        // A VCO's pitch to the hertz above 1 kHz (two cents at the most), where kilohertz to two places lose 9 cents.
        case Unit::vcoHz: if (vcoHz (v) < 1000.0f) hz (vcoHz (v)); else std::snprintf (text, size, "%.0f Hz", vcoHz (v)); break;
        case Unit::cutoffHz: hz (cutoffHz (v)); break;
        case Unit::attack: time (attackSeconds (v)); break;
        case Unit::decay: time (decaySeconds (v)); break;
        case Unit::percent: std::snprintf (text, size, "%.0f %%", v * 100.0f); break;
        case Unit::bipolar: std::snprintf (text, size, "%+.0f %%", v * 100.0f); break;
        case Unit::divisor: std::snprintf (text, size, "1/%d", (int) v); break;
        case Unit::plainInt: std::snprintf (text, size, "%d", (int) v); break;
        case Unit::step: std::snprintf (text, size, "%+.1f st", v * octaves * 12.0f); break;
        case Unit::cents: std::snprintf (text, size, "%+.0f ct", v * fineTuneCents); break;
        case Unit::choice: std::snprintf (text, size, "%s", p.choices[(int) v]); break;
        case Unit::button: std::snprintf (text, size, "%s", v >= 0.5f ? "Pressed" : "Press"); break;
    }
}

// Reads text as formatParam writes it (units optional); a choice by its name or its number; a divisor as "1/5" or 5.
inline bool parseParam (const Param& p, const char* text, float& v, float octaves = 2.0f)
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
    if (p.unit == Unit::divisor)
        if (const char* slash = std::strchr (text, '/'))
            text = slash + 1;
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
        case Unit::attack: v = attackKnob ((float) (milli ? x / 1000.0 : x)); break;
        case Unit::decay: v = decayKnob ((float) (milli ? x / 1000.0 : x)); break;
        case Unit::percent:
        case Unit::bipolar: v = (float) (x / 100.0); break;
        case Unit::step: v = (float) (x / (octaves * 12.0)); break;
        case Unit::cents: v = (float) (x / fineTuneCents); break;
        default: v = (float) x; break;
    }
    v = clampParam (p, v);
    return true;
}
} // namespace necronomicon
