// Necronomicon's factory presets: the patches the demo renders play (tests/host.cpp checks they are the same values and
// the same sound). A preset sets every parameter: the ones listed here, the rest to their defaults.
#pragma once

#include "params.h"

#include <cstring>

namespace necronomicon
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
// Choice values by their place in the list: Wave 0 Square, 1 Pulse/Saw, 2 Saw; Quantize 0 Off, 1 12-ET, 2 8-ET,
// 3 12-JI, 4 8-JI; EG 0 Off, 1 On, 2 Hold; Seq Oct 0 +/-1, 1 +/-2, 2 +/-5; Clock Div 3 is 1/8, 5 is 1/16; Rhythm
// Logic 1 is XOR; the routings and assignments 1 On. Steps at +/-2 octaves are semitones / 24, at +/-1 / 12.

// Just-intonation chords: each VCO's subs a few subharmonics below it, both sequencers walking slow chord tones.
constexpr PresetValue subharmonicChords[] = {
    { quantize, 3.0f }, { vco1Sub1, 3.0f }, { vco1Sub2, 5.0f }, { vco2Freq, 7.0f / 48.0f }, { vco2Sub1, 4.0f }, { vco2Sub2, 6.0f },
    { vco1Level, 0.5f }, { vco1Sub1Level, 0.6f }, { vco1Sub2Level, 0.5f }, { vco2Level, 0.45f }, { vco2Sub1Level, 0.5f }, { vco2Sub2Level, 0.4f },
    { cutoff, 0.5f }, { resonance, 0.25f }, { vcfEgAmt, 0.35f }, { vcfAttack, 0.35f }, { vcfDecay, 0.6f }, { vcaAttack, 0.3f }, { vcaDecay, 0.7f },
    { seq1Step2, 5.0f / 24.0f }, { seq1Step3, 3.0f / 24.0f }, { seq1Step4, 7.0f / 24.0f },
    { seq2Step2, -2.0f / 24.0f }, { seq2Step3, 3.0f / 24.0f }, { seq2Step4, -4.0f / 24.0f },
    { rhythm1, 8.0f }, { rhythm2, 6.0f },
};
// A low pattern in 3 against 4: VCO 1's subs an octave and two below, VCO 2's third sub moved by its sequencer.
constexpr PresetValue polyrhythmBass[] = {
    { vco1Sub1, 2.0f }, { vco1Sub2, 4.0f }, { vco1Level, 0.35f }, { vco1Sub1Level, 0.8f }, { vco1Sub2Level, 0.6f },
    { vco2Freq, 0.0f }, { vco2Sub1, 3.0f }, { vco2Sub2, 4.0f }, { vco2Level, 0.0f }, { vco2Sub1Level, 0.5f }, { vco2Sub2Level, 0.0f },
    { seq2Osc, 0.0f }, { seq2Sub1, 1.0f },
    { cutoff, 0.38f }, { resonance, 0.45f }, { vcfEgAmt, 0.5f }, { vcfAttack, 0.0f }, { vcfDecay, 0.35f }, { vcaAttack, 0.0f }, { vcaDecay, 0.45f },
    { seqOct, 0.0f }, { seq1Step2, 3.0f / 12.0f }, { seq1Step3, 7.0f / 12.0f }, { seq1Step4, -5.0f / 12.0f },
    { seq2Step2, -0.34f }, { seq2Step3, 0.34f }, { seq2Step4, 0.0f },
    { rhythm1, 3.0f }, { rhythm2, 4.0f }, { rhythm3, 7.0f }, { rhythm3Seq1, 1.0f }, { rhythm3Seq2, 1.0f },
};
// The middle Wave position on both VCOs: each pulse's width swept by its SUB 1, under slow envelopes.
constexpr PresetValue pwmPad[] = {
    { vco1Wave, 1.0f }, { vco2Wave, 1.0f }, { vco1Sub1, 5.0f }, { vco1Sub2, 2.0f }, { vco2Freq, 4.0f / 48.0f }, { vco2Sub1, 3.0f },
    { vco1Level, 0.6f }, { vco1Sub1Level, 0.3f }, { vco1Sub2Level, 0.4f }, { vco2Level, 0.55f }, { vco2Sub1Level, 0.3f }, { vco2Sub2Level, 0.0f },
    { cutoff, 0.5f }, { resonance, 0.3f }, { vcfEgAmt, 0.3f }, { vcfAttack, 0.6f }, { vcfDecay, 0.8f }, { vcaAttack, 0.55f }, { vcaDecay, 0.85f },
    { seq1Step2, 5.0f / 24.0f }, { seq1Step3, -2.0f / 24.0f }, { seq1Step4, 3.0f / 24.0f },
    { seq2Step2, 3.0f / 24.0f }, { seq2Step3, -5.0f / 24.0f }, { seq2Step4, 0.0f },
    { rhythm1, 16.0f }, { rhythm2, 12.0f },
};
// The sequencers move the subs' divisors, not the VCOs: a subharmonic arpeggio, three generators under XOR.
constexpr PresetValue subArp[] = {
    { vco1Wave, 0.0f }, { vco2Wave, 0.0f }, { vco1Sub1, 4.0f }, { vco1Sub2, 6.0f }, { vco2Freq, 0.25f }, { vco2Sub1, 5.0f },
    { vco1Level, 0.2f }, { vco1Sub1Level, 0.7f }, { vco1Sub2Level, 0.5f }, { vco2Level, 0.0f }, { vco2Sub1Level, 0.6f }, { vco2Sub2Level, 0.0f },
    { seq1Osc, 0.0f }, { seq1Sub1, 1.0f }, { seq1Sub2, 1.0f }, { seq2Osc, 0.0f }, { seq2Sub1, 1.0f },
    { seq1Step2, 0.33f }, { seq1Step3, -0.33f }, { seq1Step4, 0.5f }, { seq2Step2, 0.5f }, { seq2Step3, -0.5f }, { seq2Step4, 0.17f },
    { cutoff, 0.45f }, { resonance, 0.35f }, { vcfEgAmt, 0.45f }, { vcfAttack, 0.0f }, { vcfDecay, 0.3f }, { vcaAttack, 0.0f }, { vcaDecay, 0.35f },
    { rhythm1, 2.0f }, { rhythm2, 3.0f }, { rhythm3, 5.0f }, { rhythm3Seq1, 1.0f }, { rhythmLogic, 1.0f },
};
// EG: Hold: a drone the sequencers retune on slow rhythms, a resonant filter, just intonation.
constexpr PresetValue holdDrone[] = {
    { egMode, 2.0f }, { quantize, 3.0f }, { vco1Sub1, 2.0f }, { vco1Sub2, 3.0f }, { vco2Freq, 7.0f / 48.0f }, { vco2Sub1, 3.0f }, { vco2Sub2, 4.0f },
    { vco1Level, 0.45f }, { vco1Sub1Level, 0.5f }, { vco1Sub2Level, 0.4f }, { vco2Level, 0.4f }, { vco2Sub1Level, 0.4f }, { vco2Sub2Level, 0.35f },
    { cutoff, 0.42f }, { resonance, 0.6f }, { vcfEgAmt, 0.0f }, { volume, 0.6f },
    { seq1Step2, 5.0f / 24.0f }, { seq1Step3, -3.0f / 24.0f }, { seq1Step4, 2.0f / 24.0f },
    { seq2Step2, -5.0f / 24.0f }, { seq2Step3, 4.0f / 24.0f }, { seq2Step4, 0.0f },
    { rhythm1, 16.0f }, { rhythm2, 12.0f }, { clockDiv, 3.0f },
};
} // namespace presetValues

#define NECRONOMICON_PRESET(name, key, list) Preset { name, key, list, (uint32_t) (sizeof (list) / sizeof (list[0])) }
inline const Preset presetTable[] = {
    Preset { "Default", "default", nullptr, 0 },
    NECRONOMICON_PRESET ("Subharmonic Chords", "subharmonic-chords", presetValues::subharmonicChords),
    NECRONOMICON_PRESET ("Polyrhythm Bass", "polyrhythm-bass", presetValues::polyrhythmBass),
    NECRONOMICON_PRESET ("PWM Pad", "pwm-pad", presetValues::pwmPad),
    NECRONOMICON_PRESET ("Sub Arp", "sub-arp", presetValues::subArp),
    NECRONOMICON_PRESET ("Hold Drone", "hold-drone", presetValues::holdDrone),
};
#undef NECRONOMICON_PRESET
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
} // namespace necronomicon
