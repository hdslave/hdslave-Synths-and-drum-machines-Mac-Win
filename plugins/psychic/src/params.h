// Psychic's parameters: one table for the CLAP side (names, ranges, text) and the engine (what a knob means).
// Ids are fixed forever: a saved project keeps its values by id. New parameters take new ids.
#pragma once

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <strings.h>

#ifndef PSYCHIC_VERSION
#define PSYCHIC_VERSION "0.1.3"
#endif

namespace psychic
{
namespace ParamId
{
enum : uint32_t
{
    vcoFreq = 0, vcoEg1Amt, vcoSeq1Amt, modFreq, modEg1Amt, modSeq2Amt, fmAmt,
    vcoLevel, ringLevel, modLevel, noiseLevel, noiseTone,
    fold, bias, vcwEg1Amt, vcwSeq1Amt,
    cutoff, resonance, filterMode, vcfEg1Amt, vcfSeq2Amt,
    order, blend, volume,
    eg1Decay, eg2Decay, egTrigMix,
    clockDiv, swing, run, trigger, advance, reset, chain, quantize,
    corrupt1, cvRange1, length1, bitFlip1, bitShift1, bits1, writeHead1,
    corrupt2, cvRange2, length2, bitFlip2, bitShift2, bits2, writeHead2,
    save, recall,
    cv1First, cv1Last = cv1First + 7, // Seq 1 CV 1..8
    cv2First, cv2Last = cv2First + 7, // Seq 2 CV 1..8
    clock2, modFold, modCutoff, drone,
    count
};
}

enum class Kind : uint8_t { continuous, integer, choice, button };
// How a value reads: what the knob does, for its text.
enum class Unit : uint8_t { vcoHz, modHz, cutoffHz, decay, percent, bipolar, qtzAmount, filterMode, blend, trigMix, cvRange,
                            volts, bits, ahead, plainInt, choice, button };

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
// Knobs run 0..1 (amounts -1..1); these turn them into the engine's units. Each range is the panel's where its manual
// gives one, else chosen to cover what the panel does (the README lists which).

// VCO (sine): about 20 Hz to about 5 kHz, exponential: 8 octaves.
constexpr float vcoLowHz = 20.0f, vcoOctaves = 8.0f;
inline float vcoHz (float k) { return vcoLowHz * std::exp2 (k * vcoOctaves); }
inline float vcoKnob (float hz) { return std::log2 (hz / vcoLowHz) / vcoOctaves; }

// MOD VCO (triangle): from a slow LFO (a cycle every 6 s) to about 1.3 kHz: 13 octaves.
constexpr float modLowHz = 0.16f, modOctaves = 13.0f;
inline float modHz (float k) { return modLowHz * std::exp2 (k * modOctaves); }
inline float modKnob (float hz) { return std::log2 (hz / modLowHz) / modOctaves; }

// Cutoff: about 20 Hz to about 20 kHz.
constexpr float cutoffLowHz = 20.0f, cutoffOctaves = 10.0f;
inline float cutoffHz (float k) { return cutoffLowHz * std::exp2 (k * cutoffOctaves); }
inline float cutoffKnob (float hz) { return std::log2 (hz / cutoffLowHz) / cutoffOctaves; }

// Decay (both EGs): the time to fall 60 dB, exponential over the knob, 10 ms to 8 s ("greater than 5 s").
constexpr float decayShortest = 0.010f, decayLongest = 8.0f;
inline float decaySeconds (float k) { return decayShortest * std::pow (decayLongest / decayShortest, k); }
inline float decayKnob (float s) { return std::log (s / decayShortest) / std::log (decayLongest / decayShortest); }

// Depths at full turn. The EGs peak at the trigger's level (EG Trig Mix), so each depth is reached at a full trigger.
constexpr float egPitchOctaves = 8.0f; // VCO / Mod VCO EG1 Amt at +-100 %: EG1's 8 V into 1 V/octave
constexpr float egCutoffOctaves = 8.0f; // VCF EG1 Amt at +-100 %
constexpr float seqOctaves = 5.0f;     // a sequencer's CV: +-5 V, at 1 V/octave +-5 octaves with CV Range full
constexpr float fmIndex = 4.0f;        // FM Amt at 100 %: through zero, the VCO swings by 4 times its frequency
constexpr float foldMax = 4.0f * 3.14159265f; // the folder's gain at full Fold: about four folds each way
constexpr float modFoldSpan = 1.0f;    // Mod VCO > Fold at 100 %: the triangle moves Fold by a whole turn each way
constexpr float modCutoffOctaves = 5.0f; // Mod VCO > Cutoff at 100 %: +-5 octaves (its +-5 V at 1 V/octave)
// Resonance: the filter's damping, from none to a Q of 10 (it rings but stays below self-oscillation).
constexpr float dampingMin = 0.1f;
// The VCO's and Mod VCO's Seq Amt: the top of the turn is QTZ, the sequencer's CV at 1 V/octave (in tune when the
// quantizer is on); below it the CV comes in scaled (out of tune: a glide of intervals).
constexpr float qtzFrom = 0.98f;
inline float seqAmtScale (float k) { return k >= qtzFrom ? 1.0f : k / qtzFrom; }
// EG attacks: decay-only on the panel, but a capacitor still takes a moment to charge: 1 ms.
constexpr float attackSeconds = 0.001f;
// MIDI notes transpose the quantized sequences: this note leaves them where they are (middle C).
constexpr int transposeNote = 60;

inline const char* const offOn[] = { "Off", "On" };
inline const char* const runChoices[] = { "Stop", "Run" };
inline const char* const orderChoices[] = { "Parallel", "VCW > VCF", "VCF > VCW" };
// The quantizer's 16 modes, in the panel's order (SEQ 1 bits 1-8, then SEQ 2 bits 1-8), as pitch classes above
// the root (bit n set: n semitones). Unquantized has none: the CV passes as it is.
inline const char* const quantizeChoices[] = { "Unquantized", "Chromatic", "Major", "Pentatonic", "Melodic Minor", "Harmonic Minor",
                                               "Diminished 6th", "Whole Tone", "Hirajoshi", "7 Sus 4", "Major 7th", "Major 13th",
                                               "Minor 7th", "Minor 11th", "Hang Drum", "Quads (Minor 3rds)" };
constexpr uint16_t pc (int a) { return (uint16_t) (1u << a); }
constexpr uint16_t scaleMask[] = {
    0,                                                                                     // Unquantized
    0xfff,                                                                                 // Chromatic
    pc (0) | pc (2) | pc (4) | pc (5) | pc (7) | pc (9) | pc (11),                         // Major
    pc (0) | pc (2) | pc (4) | pc (7) | pc (9),                                            // Pentatonic (major)
    pc (0) | pc (2) | pc (3) | pc (5) | pc (7) | pc (9) | pc (11),                         // Melodic Minor
    pc (0) | pc (2) | pc (3) | pc (5) | pc (7) | pc (8) | pc (11),                         // Harmonic Minor
    pc (0) | pc (2) | pc (4) | pc (5) | pc (7) | pc (8) | pc (9) | pc (11),                // Diminished 6th
    pc (0) | pc (2) | pc (4) | pc (6) | pc (8) | pc (10),                                  // Whole Tone
    pc (0) | pc (2) | pc (3) | pc (7) | pc (8),                                            // Hirajoshi
    pc (0) | pc (5) | pc (7) | pc (10),                                                    // 7 Sus 4: 1 4 5 b7
    pc (0) | pc (4) | pc (7) | pc (11),                                                    // Major 7th: 1 3 5 7
    pc (0) | pc (2) | pc (4) | pc (7) | pc (9) | pc (11),                                  // Major 13th: 1 3 5 6 7 9
    pc (0) | pc (3) | pc (7) | pc (10),                                                    // Minor 7th: 1 b3 5 b7
    pc (0) | pc (2) | pc (3) | pc (5) | pc (7) | pc (10),                                  // Minor 11th: 1 b3 4 5 b7 9
    pc (0) | pc (2) | pc (3) | pc (7) | pc (8) | pc (10),                                  // Hang Drum (an integral-style minor)
    pc (0) | pc (3) | pc (6) | pc (9),                                                     // Quads: minor thirds
};
static_assert (sizeof (scaleMask) / sizeof (scaleMask[0]) == sizeof (quantizeChoices) / sizeof (quantizeChoices[0]), "a mask a mode");

// The clock replaces the panel's Tempo: a step is this much of a bar's quarter notes at the host's tempo.
inline const char* const clockDivChoices[] = { "1/2", "1/4", "1/4T", "1/8", "1/8T", "1/16", "1/16T", "1/32", "1/32T", "1/64" };
constexpr double clockDivBeats[] = { 2.0, 1.0, 2.0 / 3.0, 0.5, 1.0 / 3.0, 0.25, 1.0 / 6.0, 0.125, 1.0 / 12.0, 0.0625 };
static_assert (sizeof (clockDivBeats) / sizeof (clockDivBeats[0]) == sizeof (clockDivChoices) / sizeof (clockDivChoices[0]), "a length a choice");
// Clock 2 (the patch bay's CLOCK 2 input): normalled to Clock 1, or a grid of its own for polyrhythms.
inline const char* const clock2Choices[] = { "Clock 1", "1/2", "1/4", "1/4T", "1/8", "1/8T", "1/16", "1/16T", "1/32", "1/32T", "1/64" };

// The sequence's own state: the bits, their CVs, lengths, write heads, Chain Seq and the quantizer, which the engine
// changes itself (Bit Flip, Bit Shift, Corrupt, Recall) and the host must hear. Also what the buffer (Save, Recall)
// keeps.
constexpr uint32_t seqStateIds[] = {
    ParamId::bits1, ParamId::bits2, ParamId::length1, ParamId::length2, ParamId::writeHead1, ParamId::writeHead2,
    ParamId::chain, ParamId::quantize,
    ParamId::cv1First + 0, ParamId::cv1First + 1, ParamId::cv1First + 2, ParamId::cv1First + 3,
    ParamId::cv1First + 4, ParamId::cv1First + 5, ParamId::cv1First + 6, ParamId::cv1First + 7,
    ParamId::cv2First + 0, ParamId::cv2First + 1, ParamId::cv2First + 2, ParamId::cv2First + 3,
    ParamId::cv2First + 4, ParamId::cv2First + 5, ParamId::cv2First + 6, ParamId::cv2First + 7,
};
constexpr uint32_t seqStateCount = sizeof (seqStateIds) / sizeof (seqStateIds[0]);
static_assert (seqStateCount <= 32, "one bit each in a mask");
struct SeqStateIndex
{
    int8_t of[ParamId::count];
    constexpr SeqStateIndex() : of()
    {
        for (uint32_t i = 0; i < ParamId::count; ++i)
            of[i] = -1;
        for (uint32_t i = 0; i < seqStateCount; ++i)
            of[seqStateIds[i]] = (int8_t) i;
    }
};
inline constexpr SeqStateIndex seqStateIndexOf {};
inline int seqStateIndex (uint32_t id) { return id < ParamId::count ? seqStateIndexOf.of[id] : -1; }

using P = Param;
#define CONT(id, name, module, lo, hi, def, unit) P { ParamId::id, name, module, lo, hi, def, Kind::continuous, Unit::unit, nullptr }
#define INT(id, name, module, lo, hi, def, unit) P { ParamId::id, name, module, lo, hi, def, Kind::integer, Unit::unit, nullptr }
#define CHOICE(id, name, module, list, def) P { ParamId::id, name, module, 0.0f, (float) (sizeof (list) / sizeof (list[0]) - 1), def, Kind::choice, Unit::choice, list }
#define BUTTON(id, name, module) P { ParamId::id, name, module, 0.0f, 1.0f, 0.0f, Kind::button, Unit::button, nullptr }
#define CV(id, name, module, def) P { id, name, module, -1.0f, 1.0f, def, Kind::continuous, Unit::volts, nullptr }

// The two sequences as they open: SEQ 1 a five-hit line, SEQ 2 three hits on it (EG Trig Mix in the middle: where
// both play the envelopes peak, elsewhere half), and fixed "random" CVs that make a tune in the default scale.
constexpr float defaultBits1 = 1 + 4 + 8 + 32 + 64; // X.XX.XX.
constexpr float defaultBits2 = 1 + 8 + 64;          // X..X..X.
constexpr float defaultCv1[8] = { 0.0f, 0.42f, -0.31f, 0.66f, 0.18f, -0.52f, 0.37f, -0.12f };
constexpr float defaultCv2[8] = { 0.0f, -0.45f, 0.28f, 0.6f, -0.22f, 0.11f, -0.63f, 0.48f };

// In the order a host lists them: three to a page, as Melodee's Edit page shows them (the VCO; the Mod VCO; FM and
// the mixer; the wavefolder; the filter; the outputs; the envelopes; then the sequencers, the patch routings and the
// sequences' CVs).
inline constexpr Param paramTable[] = {
    CONT (vcoFreq, "VCO Freq", "VCO", 0.0f, 1.0f, 0.375f, vcoHz),
    CONT (vcoEg1Amt, "VCO EG1 Amt", "VCO", -1.0f, 1.0f, 0.0f, bipolar),
    CONT (vcoSeq1Amt, "VCO Seq 1 Amt", "VCO", 0.0f, 1.0f, 1.0f, qtzAmount),
    CONT (modFreq, "Mod VCO Freq", "Mod VCO", 0.0f, 1.0f, 0.74f, modHz),
    CONT (modEg1Amt, "Mod VCO EG1 Amt", "Mod VCO", -1.0f, 1.0f, 0.0f, bipolar),
    CONT (modSeq2Amt, "Mod VCO Seq 2 Amt", "Mod VCO", 0.0f, 1.0f, 1.0f, qtzAmount),
    CONT (fmAmt, "FM Amt", "Mod VCO", 0.0f, 1.0f, 0.2f, percent),
    CONT (vcoLevel, "VCO Level", "Mixer", 0.0f, 1.0f, 0.55f, percent),
    CONT (modLevel, "Mod VCO Level", "Mixer", 0.0f, 1.0f, 0.2f, percent),
    CONT (ringLevel, "Ring Mod Level", "Mixer", 0.0f, 1.0f, 0.0f, percent),
    CONT (noiseLevel, "Noise Level", "Mixer", 0.0f, 1.0f, 0.0f, percent),
    CONT (noiseTone, "Noise Tone", "Mixer", 0.0f, 1.0f, 0.5f, percent),
    CONT (fold, "Fold", "VCW", 0.0f, 1.0f, 0.15f, percent),
    CONT (bias, "Bias", "VCW", -1.0f, 1.0f, 0.0f, bipolar),
    CONT (vcwEg1Amt, "VCW EG1 Amt", "VCW", -1.0f, 1.0f, 0.25f, bipolar),
    CONT (vcwSeq1Amt, "VCW Seq 1 Amt", "VCW", 0.0f, 1.0f, 0.0f, percent),
    CONT (cutoff, "Cutoff", "VCF", 0.0f, 1.0f, 0.5f, cutoffHz),
    CONT (resonance, "Resonance", "VCF", 0.0f, 1.0f, 0.35f, percent),
    CONT (filterMode, "Filter Mode", "VCF", 0.0f, 1.0f, 0.0f, filterMode),
    CONT (vcfEg1Amt, "VCF EG1 Amt", "VCF", -1.0f, 1.0f, 0.35f, bipolar),
    CONT (vcfSeq2Amt, "VCF Seq 2 Amt", "VCF", 0.0f, 1.0f, 0.2f, percent),
    CHOICE (order, "Order", "Output", orderChoices, 0.0f),
    CONT (blend, "Blend", "Output", 0.0f, 1.0f, 0.5f, blend),
    CONT (volume, "Volume", "Output", 0.0f, 1.0f, 0.7f, percent),
    CONT (eg1Decay, "EG1 Decay", "EG", 0.0f, 1.0f, 0.35f, decay),
    CONT (eg2Decay, "EG2 Decay", "EG", 0.0f, 1.0f, 0.45f, decay),
    CONT (egTrigMix, "EG Trig Mix", "EG", 0.0f, 1.0f, 0.5f, trigMix),
    CHOICE (clockDiv, "Clock Div", "Sequencer", clockDivChoices, 3.0f),
    CHOICE (run, "Run", "Sequencer", runChoices, 1.0f),
    CHOICE (quantize, "Quantize", "Sequencer", quantizeChoices, 2.0f),
    BUTTON (trigger, "Trigger", "Sequencer"),
    BUTTON (advance, "Advance", "Sequencer"),
    BUTTON (reset, "Reset", "Sequencer"),
    CONT (corrupt1, "Corrupt 1", "Seq 1", 0.0f, 1.0f, 0.0f, percent),
    CONT (cvRange1, "CV Range 1", "Seq 1", 0.0f, 1.0f, 0.2f, cvRange),
    INT (length1, "Length 1", "Seq 1", 1.0f, 8.0f, 8.0f, plainInt),
    BUTTON (bitFlip1, "Bit Flip 1", "Seq 1"),
    BUTTON (bitShift1, "Bit Shift 1", "Seq 1"),
    INT (bits1, "Bits 1", "Seq 1", 0.0f, 255.0f, defaultBits1, bits),
    CONT (corrupt2, "Corrupt 2", "Seq 2", 0.0f, 1.0f, 0.0f, percent),
    CONT (cvRange2, "CV Range 2", "Seq 2", 0.0f, 1.0f, 0.2f, cvRange),
    INT (length2, "Length 2", "Seq 2", 1.0f, 8.0f, 8.0f, plainInt),
    BUTTON (bitFlip2, "Bit Flip 2", "Seq 2"),
    BUTTON (bitShift2, "Bit Shift 2", "Seq 2"),
    INT (bits2, "Bits 2", "Seq 2", 0.0f, 255.0f, defaultBits2, bits),
    CHOICE (chain, "Chain Seq", "Sequencer", offOn, 0.0f),
    INT (writeHead1, "Write Head 1", "Seq 1", 0.0f, 7.0f, 0.0f, ahead),
    INT (writeHead2, "Write Head 2", "Seq 2", 0.0f, 7.0f, 0.0f, ahead),
    CONT (swing, "Swing", "Sequencer", 0.5f, 0.75f, 0.5f, percent),
    BUTTON (save, "Save", "Sequencer"),
    BUTTON (recall, "Recall", "Sequencer"),
    CHOICE (clock2, "Clock 2", "Patch", clock2Choices, 0.0f),
    CONT (modFold, "Mod VCO > Fold", "Patch", -1.0f, 1.0f, 0.0f, bipolar),
    CONT (modCutoff, "Mod VCO > Cutoff", "Patch", -1.0f, 1.0f, 0.0f, bipolar),
    CHOICE (drone, "Drone", "Patch", offOn, 0.0f),
    CV (ParamId::cv1First + 0, "Seq 1 CV 1", "Seq 1/CV", defaultCv1[0]),
    CV (ParamId::cv1First + 1, "Seq 1 CV 2", "Seq 1/CV", defaultCv1[1]),
    CV (ParamId::cv1First + 2, "Seq 1 CV 3", "Seq 1/CV", defaultCv1[2]),
    CV (ParamId::cv1First + 3, "Seq 1 CV 4", "Seq 1/CV", defaultCv1[3]),
    CV (ParamId::cv1First + 4, "Seq 1 CV 5", "Seq 1/CV", defaultCv1[4]),
    CV (ParamId::cv1First + 5, "Seq 1 CV 6", "Seq 1/CV", defaultCv1[5]),
    CV (ParamId::cv1First + 6, "Seq 1 CV 7", "Seq 1/CV", defaultCv1[6]),
    CV (ParamId::cv1First + 7, "Seq 1 CV 8", "Seq 1/CV", defaultCv1[7]),
    CV (ParamId::cv2First + 0, "Seq 2 CV 1", "Seq 2/CV", defaultCv2[0]),
    CV (ParamId::cv2First + 1, "Seq 2 CV 2", "Seq 2/CV", defaultCv2[1]),
    CV (ParamId::cv2First + 2, "Seq 2 CV 3", "Seq 2/CV", defaultCv2[2]),
    CV (ParamId::cv2First + 3, "Seq 2 CV 4", "Seq 2/CV", defaultCv2[3]),
    CV (ParamId::cv2First + 4, "Seq 2 CV 5", "Seq 2/CV", defaultCv2[4]),
    CV (ParamId::cv2First + 5, "Seq 2 CV 6", "Seq 2/CV", defaultCv2[5]),
    CV (ParamId::cv2First + 6, "Seq 2 CV 7", "Seq 2/CV", defaultCv2[6]),
    CV (ParamId::cv2First + 7, "Seq 2 CV 8", "Seq 2/CV", defaultCv2[7]),
};
#undef CONT
#undef INT
#undef CHOICE
#undef BUTTON
#undef CV

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

// A bit pattern as the panel's LEDs: step 1 first, X on, . off.
inline void formatBits (int bits, char* text, uint32_t size)
{
    char s[9];
    for (int i = 0; i < 8; ++i)
        s[i] = (bits >> i) & 1 ? 'X' : '.';
    s[8] = 0;
    std::snprintf (text, size, "%s", s);
}

inline void formatParam (const Param& p, float v, char* text, uint32_t size)
{
    v = clampParam (p, v);
    // (Each unit up to where its rounding would read as the next: 999.96 Hz is "1.00 kHz", not "1000.0 Hz".)
    auto time = [&] (float s) {
        if (s < 0.9995f) std::snprintf (text, size, "%.0f ms", s * 1000.0f);
        else std::snprintf (text, size, "%.2f s", s);
    };
    auto hz = [&] (float f) {
        if (f < 9.995f) std::snprintf (text, size, "%.2f Hz", f);
        else if (f < 999.95f) std::snprintf (text, size, "%.1f Hz", f);
        else std::snprintf (text, size, "%.2f kHz", f / 1000.0f);
    };
    // A two-way knob: its ends by name, between them the two shares (adding up to 100).
    auto twoWay = [&] (const char* left, const char* right, const char* format) {
        const int share = (int) std::lround (v * 100.0f);
        if (share <= 0) std::snprintf (text, size, "%s", left);
        else if (share >= 100) std::snprintf (text, size, "%s", right);
        else std::snprintf (text, size, format, 100 - share, share);
    };
    switch (p.unit)
    {
        case Unit::vcoHz: hz (vcoHz (v)); break;
        case Unit::modHz: hz (modHz (v)); break;
        case Unit::cutoffHz: hz (cutoffHz (v)); break;
        case Unit::decay: time (decaySeconds (v)); break;
        case Unit::percent: std::snprintf (text, size, "%.0f %%", v * 100.0f); break;
        case Unit::bipolar: std::snprintf (text, size, "%+.0f %%", v * 100.0f); break;
        case Unit::qtzAmount: // below QTZ never "100 %" (which reads back as QTZ)
            if (v >= qtzFrom) std::snprintf (text, size, "QTZ");
            else std::snprintf (text, size, "%.0f %%", std::floor (seqAmtScale (v) * 100.0f));
            break;
        case Unit::filterMode: twoWay ("LP", "BP", "LP %d / BP %d"); break;
        case Unit::blend: twoWay ("VCW", "VCF", "VCW %d / VCF %d"); break;
        case Unit::trigMix: twoWay ("Seq 1", "Seq 2", "%d / %d"); break;
        case Unit::cvRange: std::snprintf (text, size, "+-%.1f oct", v * seqOctaves); break;
        case Unit::volts: std::snprintf (text, size, "%+.2f V", v * 5.0f); break;
        case Unit::bits: formatBits ((int) v, text, size); break;
        case Unit::ahead: std::snprintf (text, size, "+%d", (int) v); break;
        case Unit::plainInt: std::snprintf (text, size, "%d", (int) v); break;
        case Unit::choice: std::snprintf (text, size, "%s", p.choices[(int) v]); break;
        case Unit::button: std::snprintf (text, size, "%s", v >= 0.5f ? "Pressed" : "Press"); break;
    }
}

// Reads text as formatParam writes it (units optional, spaces around it ignored); a choice by its name or its number;
// a bit pattern as its eight LEDs or as a number. Anything else (and "nan", "inf") is refused.
inline bool parseParam (const Param& p, const char* raw, float& v)
{
    char trimmed[64];
    while (*raw == ' ' || *raw == '\t')
        ++raw;
    std::snprintf (trimmed, sizeof (trimmed), "%s", raw);
    for (size_t n = std::strlen (trimmed); n > 0 && (trimmed[n - 1] == ' ' || trimmed[n - 1] == '\t'); --n)
        trimmed[n - 1] = 0;
    const char* text = trimmed;
    if (p.kind == Kind::choice)
    {
        const int n = (int) (p.max - p.min) + 1;
        for (int i = 0; i < n; ++i)
            if (strcasecmp (text, p.choices[i]) == 0)
                return v = (float) i, true;
        // Or its number, and nothing more.
        char* end = nullptr;
        const long i = std::strtol (text, &end, 10);
        if (end == text || *end != 0 || i < 0 || i >= n)
            return false;
        return v = (float) i, true;
    }
    if (p.kind == Kind::button)
    {
        v = (strcasecmp (text, "Pressed") == 0 || std::atof (text) >= 0.5) ? 1.0f : 0.0f;
        return true;
    }
    if (p.unit == Unit::bits && std::strlen (text) == 8 && std::strspn (text, "Xx.-01oO*") == 8)
    {
        int bits = 0;
        for (int i = 0; i < 8; ++i)
            bits |= (text[i] == 'X' || text[i] == 'x' || text[i] == '1' || text[i] == 'o' || text[i] == 'O' || text[i] == '*') ? 1 << i : 0;
        return v = (float) bits, true;
    }
    // The named ends of two-way knobs, and QTZ.
    struct Named { Unit unit; const char* name; float value; };
    static const Named named[] = { { Unit::filterMode, "LP", 0.0f }, { Unit::filterMode, "BP", 1.0f }, { Unit::blend, "VCW", 0.0f },
                                   { Unit::blend, "VCF", 1.0f }, { Unit::trigMix, "Seq 1", 0.0f }, { Unit::trigMix, "Seq 2", 1.0f },
                                   { Unit::qtzAmount, "QTZ", 1.0f } };
    for (const auto& n : named)
        if (p.unit == n.unit && strcasecmp (text, n.name) == 0)
            return v = n.value, true;
    // "A / B" (two-way knobs): B is the right end's share.
    if (p.unit == Unit::filterMode || p.unit == Unit::blend || p.unit == Unit::trigMix)
        if (const char* slash = std::strchr (text, '/'))
        {
            const char* at = slash + 1;
            while (*at != 0 && (*at < '0' || *at > '9') && *at != '.' && *at != '-')
                ++at;
            char* end = nullptr;
            const double share = std::strtod (at, &end);
            if (end == at || ! std::isfinite (share))
                return false;
            return v = clampParam (p, (float) (share / 100.0)), true;
        }
    const char* at = text;
    while (*at == '+' && at[1] == '-') // "+-1.0 oct"
        at += 2;
    char* end = nullptr;
    double x = std::strtod (at, &end);
    if (end == at || ! std::isfinite (x))
        return false;
    while (*end == ' ')
        ++end;
    const bool kilo = (*end == 'k' || *end == 'K');
    const bool milli = (end[0] == 'm' && end[1] == 's');
    switch (p.unit)
    {
        case Unit::vcoHz: v = vcoKnob ((float) (kilo ? x * 1000.0 : x)); break;
        case Unit::modHz: v = modKnob ((float) (kilo ? x * 1000.0 : x)); break;
        case Unit::cutoffHz: v = cutoffKnob ((float) (kilo ? x * 1000.0 : x)); break;
        case Unit::decay: v = decayKnob ((float) (milli ? x / 1000.0 : x)); break;
        case Unit::percent:
        case Unit::bipolar:
        case Unit::filterMode:
        case Unit::blend:
        case Unit::trigMix: v = (float) (x / 100.0); break;
        case Unit::qtzAmount: v = (float) (x / 100.0) * qtzFrom; break;
        case Unit::cvRange: v = (float) (x / seqOctaves); break;
        case Unit::volts: v = (float) (x / 5.0); break;
        default: v = (float) x; break;
    }
    v = clampParam (p, v);
    return true;
}
} // namespace psychic
