// Psychic's factory presets: the patches the demo renders play (tests/host.cpp checks they are the same values and the
// same sound). A preset sets every parameter, the sequences too: the ones listed here, the rest to their defaults.
#pragma once

#include "params.h"

#include <cstring>

namespace psychic
{
struct PresetValue
{
    uint32_t id;
    float value;
};

struct Preset
{
    const char* name; // as a host lists it
    const char* key;  // its load key (CLAP preset-load), never changed once released
    const PresetValue* values;
    uint32_t count;
};

namespace presetValues
{
using namespace ParamId;
// Choice values by their place in the list: Clock Div 1/8 is 3, 1/16 is 5; Clock 2 "1/8T" is 5; Order 1 is VCW > VCF,
// 2 VCF > VCW; Quantize 2 Major, 3 Pentatonic, 12 Minor 7th, 13 Minor 11th; Bits as numbers (step 1 is 1, step 8 is 128).
constexpr uint32_t cv1 = cv1First, cv2 = cv2First;
constexpr PresetValue psychicBells[] = {
    { quantize, 3.0f }, { clockDiv, 5.0f }, { vcoFreq, 0.5f }, { fmAmt, 0.5f }, { modFreq, 0.8f }, { modSeq2Amt, 1.0f },
    { vcoLevel, 0.6f }, { modLevel, 0.0f }, { fold, 0.05f }, { vcwEg1Amt, 0.35f }, { blend, 0.25f }, { cutoff, 0.62f },
    { resonance, 0.2f }, { vcfEg1Amt, 0.3f }, { eg1Decay, 0.33f }, { eg2Decay, 0.5f }, { cvRange1, 0.3f }, { cvRange2, 0.25f },
    { corrupt1, 0.12f }, { bits1, 1 + 4 + 16 + 32 + 128 }, { bits2, 1 + 16 + 64 }, { egTrigMix, 0.35f },
    { cv1 + 0, 0.0f }, { cv1 + 2, 0.45f }, { cv1 + 4, -0.3f }, { cv1 + 5, 0.7f }, { cv1 + 7, 0.25f },
    { cv2 + 0, 0.2f }, { cv2 + 4, -0.5f }, { cv2 + 6, 0.65f },
};
constexpr PresetValue foldBass[] = {
    { quantize, 12.0f }, { clockDiv, 5.0f }, { vcoFreq, 0.2f }, { vcoEg1Amt, 0.08f }, { fmAmt, 0.0f }, { vcoLevel, 0.7f },
    { modLevel, 0.0f }, { fold, 0.3f }, { bias, 0.2f }, { vcwEg1Amt, 0.45f }, { order, 1.0f }, { blend, 1.0f },
    { cutoff, 0.42f }, { resonance, 0.55f }, { vcfEg1Amt, 0.4f }, { vcfSeq2Amt, 0.0f }, { eg1Decay, 0.3f }, { eg2Decay, 0.4f },
    { cvRange1, 0.12f }, { egTrigMix, 0.0f }, { bits1, 1 + 4 + 8 + 32 + 64 + 128 }, { length1, 7.0f },
    { cv1 + 0, 0.0f }, { cv1 + 2, 0.5f }, { cv1 + 3, -0.4f }, { cv1 + 5, 0.9f }, { cv1 + 6, 0.2f }, { cv1 + 7, -0.8f },
};
constexpr PresetValue chainMelody[] = {
    { chain, 1.0f }, { quantize, 2.0f }, { clockDiv, 5.0f }, { length1, 6.0f }, { length2, 5.0f }, { vcoFreq, 0.48f },
    { fmAmt, 0.25f }, { modFreq, 0.77f }, { vcoLevel, 0.6f }, { modLevel, 0.15f }, { fold, 0.12f }, { vcwEg1Amt, 0.2f },
    { blend, 0.6f }, { cutoff, 0.55f }, { resonance, 0.4f }, { vcfEg1Amt, 0.35f }, { vcfSeq2Amt, 0.3f }, { eg1Decay, 0.3f },
    { eg2Decay, 0.42f }, { cvRange1, 0.25f }, { cvRange2, 0.25f }, { egTrigMix, 0.5f }, { bits1, 1 + 2 + 8 + 16 + 32 },
    { bits2, 1 + 4 + 8 + 16 },
    { cv1 + 1, 0.4f }, { cv1 + 3, -0.2f }, { cv1 + 4, 0.6f }, { cv2 + 0, 0.8f }, { cv2 + 2, -0.5f }, { cv2 + 3, 0.3f }, { cv2 + 4, 0.1f },
};
constexpr PresetValue polyrhythm[] = {
    { clock2, 5.0f }, { clockDiv, 5.0f }, { length1, 5.0f }, { length2, 3.0f }, { quantize, 13.0f }, { vcoFreq, 0.42f },
    { modFreq, 0.7f }, { modSeq2Amt, 1.0f }, { fmAmt, 0.3f }, { vcoLevel, 0.5f }, { ringLevel, 0.35f }, { modLevel, 0.1f },
    { fold, 0.2f }, { vcwEg1Amt, 0.3f }, { blend, 0.5f }, { cutoff, 0.5f }, { resonance, 0.45f }, { vcfEg1Amt, 0.4f },
    { eg1Decay, 0.28f }, { eg2Decay, 0.36f }, { cvRange1, 0.25f }, { cvRange2, 0.3f }, { egTrigMix, 0.5f },
    { bits1, 1 + 2 + 8 }, { bits2, 1 + 4 },
};
constexpr PresetValue corruptDrone[] = {
    { drone, 1.0f }, { clockDiv, 1.0f }, { quantize, 13.0f }, { vcoFreq, 0.3f }, { modFreq, 0.22f }, { modSeq2Amt, 0.0f },
    { modFold, 0.35f }, { modCutoff, 0.25f }, { fmAmt, 0.0f }, { vcoLevel, 0.6f }, { modLevel, 0.0f }, { noiseLevel, 0.12f },
    { noiseTone, 0.3f }, { fold, 0.3f }, { vcwEg1Amt, 0.2f }, { vcwSeq1Amt, 0.3f }, { filterMode, 0.7f }, { cutoff, 0.5f },
    { resonance, 0.7f }, { vcfEg1Amt, 0.2f }, { vcfSeq2Amt, 0.5f }, { blend, 0.55f }, { volume, 0.55f }, { eg1Decay, 0.6f },
    { eg2Decay, 0.7f }, { corrupt1, 0.7f }, { corrupt2, 0.35f }, { cvRange1, 0.2f }, { cvRange2, 0.35f },
};
constexpr PresetValue noisePerc[] = {
    { clockDiv, 5.0f }, { vcoLevel, 0.0f }, { modLevel, 0.0f }, { noiseLevel, 0.8f }, { noiseTone, 0.6f }, { vcoSeq1Amt, 0.0f },
    { filterMode, 1.0f }, { cutoff, 0.5f }, { resonance, 0.78f }, { vcfEg1Amt, 0.3f }, { vcfSeq2Amt, 1.0f }, { blend, 1.0f },
    { volume, 0.85f }, { eg1Decay, 0.2f }, { eg2Decay, 0.3f }, { cvRange2, 0.3f }, { quantize, 3.0f }, { egTrigMix, 0.5f },
    { bits1, 1 + 4 + 16 + 64 }, { bits2, 1 + 2 + 8 + 32 + 128 }, { corrupt2, 0.2f },
};
} // namespace presetValues

#define PSYCHIC_PRESET(name, key, list) Preset { name, key, list, (uint32_t) (sizeof (list) / sizeof (list[0])) }
inline const Preset presetTable[] = {
    Preset { "Default", "default", nullptr, 0 },
    PSYCHIC_PRESET ("Psychic Bells", "psychic-bells", presetValues::psychicBells),
    PSYCHIC_PRESET ("Fold Bass", "fold-bass", presetValues::foldBass),
    PSYCHIC_PRESET ("Chain Melody", "chain-melody", presetValues::chainMelody),
    PSYCHIC_PRESET ("Polyrhythm", "polyrhythm", presetValues::polyrhythm),
    PSYCHIC_PRESET ("Corrupt Drone", "corrupt-drone", presetValues::corruptDrone),
    PSYCHIC_PRESET ("Noise Perc", "noise-perc", presetValues::noisePerc),
};
#undef PSYCHIC_PRESET
constexpr uint32_t presetCount = sizeof (presetTable) / sizeof (presetTable[0]);

inline const Preset* findPreset (const char* key)
{
    if (key == nullptr)
        return nullptr;
    for (const auto& p : presetTable)
        if (std::strcmp (p.key, key) == 0)
            return &p;
    return nullptr;
}

// Every parameter's value under `preset`, in the table's order (buttons at 0).
inline void presetValuesOf (const Preset& preset, float* out)
{
    for (uint32_t i = 0; i < paramCount; ++i)
        out[i] = paramTable[i].def;
    for (uint32_t n = 0; n < preset.count; ++n)
    {
        const int index = paramIndex (preset.values[n].id);
        if (index >= 0 && paramTable[index].kind != Kind::button)
            out[index] = clampParam (paramTable[index], preset.values[n].value);
    }
}
} // namespace psychic
