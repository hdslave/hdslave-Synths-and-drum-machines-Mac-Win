// Anvil's factory presets: the patches the demo renders play (tests/host.cpp checks they are the same values and
// the same sound). A preset sets every parameter: the ones listed here, the rest to their defaults.
#pragma once

#include "params.h"

#include <cstring>

namespace anvil
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
// Choice values by their place in the list: VCA EG Fast 0, Slow 1; Clock Div 1/8 is 3, 1/16 is 5; VCO 1 Wave 1 is
// Triangle; VCF Mode 1 is High Pass; Hard Sync 1 is On; Direction Ping-Pong 2, Random 3; Notes Steps 1; Quantize
// Minor 3, Minor Pent 4, Fifths 6.
constexpr PresetValue kick[] = {
    { vco1Freq, 0.18f }, { vco1EgAmt, 0.55f }, { vcoDecay, 0.25f }, { vco1Wave, 1.0f }, { vco2Level, 0.0f }, { noiseLevel, 0.05f },
    { cutoff, 0.45f }, { vcfEgAmt, 0.3f }, { vcfDecay, 0.3f }, { vcaEg, 0.0f }, { vcaDecay, 0.45f }, { clockDiv, 3.0f },
};
constexpr PresetValue fmPerc[] = {
    { fmAmt, 0.25f }, { egFmAmt, 0.7f }, { vcoDecay, 0.3f }, { vco2Level, 1.0f }, { vco1Level, 0.0f }, { cutoff, 0.7f },
    { resonance, 0.4f }, { vcaDecay, 0.35f }, { clockDiv, 5.0f }, { pitch2, 0.4f }, { pitch4, -0.3f }, { pitch6, 0.8f },
    { velocity3, 0.3f }, { velocity7, 0.5f },
};
constexpr PresetValue syncBass[] = {
    { hardSync, 1.0f }, { vco2EgAmt, 0.6f }, { vco1Freq, 0.3f }, { vco2Freq, 0.45f }, { vco1Level, 0.3f }, { vco2Level, 0.9f },
    { cutoff, 0.4f }, { resonance, 0.55f }, { vcfEgAmt, 0.5f }, { vcaEg, 1.0f }, { vcaDecay, 0.5f }, { clockDiv, 5.0f },
    { pitch1, 0.0f }, { pitch3, 0.5f }, { pitch5, -0.4f }, { pitch8, 0.25f },
};
constexpr PresetValue noiseHat[] = {
    { vco1Level, 0.0f }, { vco2Level, 0.0f }, { noiseLevel, 1.0f }, { vcfMode, 1.0f }, { cutoff, 0.75f }, { resonance, 0.3f },
    { vcaDecay, 0.15f }, { clockDiv, 5.0f }, { velocity2, 0.4f }, { velocity4, 0.4f }, { velocity6, 0.4f }, { velocity8, 0.4f },
};
// 0.5: patches for what 0.3 to 0.5 added.
// A bassline in a key: Minor Pent from the VCOs' pitch, 7 steps swung, the cutoff following each step's pitch and
// velocity.
constexpr PresetValue acidSteps[] = {
    { vco1Freq, 0.34f }, { vco2Freq, 0.342f }, { vco1Level, 0.7f }, { vco2Level, 0.6f }, { vco1EgAmt, 0.0f }, { vco2EgAmt, 0.0f },
    { vcoDecay, 0.3f }, { cutoff, 0.42f }, { resonance, 0.6f }, { vcfEgAmt, 0.45f }, { vcfDecay, 0.33f }, { vcaDecay, 0.42f },
    { clockDiv, 5.0f }, { length, 7.0f }, { swing, 0.58f }, { quantize, 4.0f }, { pitchCutoff, 0.5f }, { velCutoff, 0.4f },
    { pitch3, 0.2f }, { pitch4, 0.05f }, { pitch6, 0.117f }, { pitch7, 0.083f },
    { velocity2, 0.5f }, { velocity3, 0.8f }, { velocity4, 0.6f }, { velocity5, 1.0f }, { velocity6, 0.4f }, { velocity7, 0.7f },
};
// Every eighth hit twice (Ratchet 2), the backbeat loud, Chance leaving out a few ghost notes.
constexpr PresetValue ratchetSnare[] = {
    { vco1Freq, 0.565f }, { vco1Wave, 1.0f }, { vco1EgAmt, 0.15f }, { vcoDecay, 0.2f }, { vco1Level, 0.5f }, { vco2Level, 0.0f },
    { noiseLevel, 0.85f }, { cutoff, 0.72f }, { resonance, 0.25f }, { vcfEgAmt, 0.2f }, { vcfDecay, 0.25f }, { vcaDecay, 0.3f },
    { clockDiv, 3.0f }, { ratchet, 2.0f }, { chance, 0.85f },
    { velocity1, 0.35f }, { velocity2, 0.25f }, { velocity3, 1.0f }, { velocity4, 0.3f }, { velocity5, 0.35f }, { velocity6, 0.25f },
    { velocity7, 1.0f }, { velocity8, 0.45f },
};
// Laser zaps in a random order: VCO 2 swept down by its EG, FM riding the VCO EG, the VCF EG bending VCO 2 too, steps
// a fifth or an octave apart, some left out.
constexpr PresetValue randomZap[] = {
    { vco2Freq, 0.75f }, { vco2EgAmt, 0.7f }, { vcoDecay, 0.28f }, { fmAmt, 0.3f }, { egFmAmt, 0.5f }, { vco1Freq, 0.45f },
    { vco1Level, 0.0f }, { vco2Level, 1.0f }, { cutoff, 0.8f }, { resonance, 0.35f }, { vcfEgAmt, 0.0f }, { vcfEgVco2, 0.25f },
    { vcfDecay, 0.35f }, { vcaDecay, 0.32f }, { clockDiv, 5.0f }, { direction, 3.0f }, { chance, 0.7f }, { quantize, 6.0f },
    { pitch2, 0.2f }, { pitch3, -0.2f }, { pitch4, 0.117f }, { pitch5, 0.3f }, { pitch6, -0.1f }, { pitch7, 0.4f }, { pitch8, 0.083f },
    { velocity2, 0.7f }, { velocity3, 0.9f }, { velocity4, 0.6f }, { velocity6, 0.8f }, { velocity7, 0.65f }, { velocity8, 0.75f },
};
// Six toms down a minor scale and back up (Ping-Pong over Length 6), swung.
constexpr PresetValue pingPongToms[] = {
    { vco1Freq, 0.42f }, { vco1Wave, 1.0f }, { vco1EgAmt, 0.25f }, { vcoDecay, 0.3f }, { vco2Freq, 0.425f }, { vco2Wave, 1.0f },
    { vco2Level, 0.4f }, { vco2EgAmt, 0.25f }, { noiseLevel, 0.05f }, { cutoff, 0.6f }, { resonance, 0.2f }, { vcfEgAmt, 0.25f },
    { vcfDecay, 0.35f }, { vcaDecay, 0.45f }, { clockDiv, 5.0f }, { length, 6.0f }, { direction, 2.0f }, { swing, 0.62f },
    { quantize, 3.0f },
    { pitch1, 0.15f }, { pitch2, 0.1f }, { pitch3, 0.05f }, { pitch5, -0.05f }, { pitch6, -0.1f },
    { velocity2, 0.7f }, { velocity3, 0.85f }, { velocity4, 0.6f }, { velocity5, 0.9f }, { velocity6, 0.65f },
};
// Eight drums on eight pads (Notes: Steps, MIDI notes 36 to 43), low to high, brighter the higher (Pitch > Cutoff);
// with the transport running the sequence plays them as a pattern too (Run Stop: the pads only).
constexpr PresetValue padKit[] = {
    { noteMode, 1.0f }, { vco1Wave, 1.0f }, { vco1Freq, 0.3f }, { vco1EgAmt, 0.5f }, { vcoDecay, 0.22f }, { vco1Level, 1.0f },
    { vco2Level, 0.0f }, { noiseLevel, 0.35f }, { cutoff, 0.5f }, { resonance, 0.15f }, { vcfEgAmt, 0.35f }, { vcfDecay, 0.25f },
    { vcaDecay, 0.35f }, { pitchCutoff, 0.8f }, { velCutoff, 0.3f }, { clockDiv, 5.0f },
    { pitch2, 0.25f }, { pitch3, 0.4f }, { pitch4, 0.55f }, { pitch5, 0.7f }, { pitch6, 0.85f }, { pitch7, 1.0f }, { pitch8, 0.6f },
    { velocity2, 0.5f }, { velocity3, 0.8f }, { velocity4, 0.5f }, { velocity5, 0.9f }, { velocity6, 0.45f }, { velocity7, 0.7f },
};
} // namespace presetValues

#define ANVIL_PRESET(name, key, list) Preset { name, key, list, (uint32_t) (sizeof (list) / sizeof (list[0])) }
inline const Preset presetTable[] = {
    Preset { "Default", "default", nullptr, 0 },
    ANVIL_PRESET ("Kick", "kick", presetValues::kick),
    ANVIL_PRESET ("FM Perc", "fm-perc", presetValues::fmPerc),
    ANVIL_PRESET ("Sync Bass", "sync-bass", presetValues::syncBass),
    ANVIL_PRESET ("Noise Hat", "noise-hat", presetValues::noiseHat),
    ANVIL_PRESET ("Acid Steps", "acid-steps", presetValues::acidSteps),
    ANVIL_PRESET ("Ratchet Snare", "ratchet-snare", presetValues::ratchetSnare),
    ANVIL_PRESET ("Random Zap", "random-zap", presetValues::randomZap),
    ANVIL_PRESET ("Ping-Pong Toms", "ping-pong-toms", presetValues::pingPongToms),
    ANVIL_PRESET ("Pad Kit", "pad-kit", presetValues::padKit),
};
#undef ANVIL_PRESET
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
} // namespace anvil
