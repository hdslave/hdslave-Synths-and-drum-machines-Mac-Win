// The factory presets: the patches the demo renders play (tests/host.cpp checks they are the same values and the same
// sound). A preset sets every parameter: the ones listed here, the rest to their defaults. All but Default lock their
// seed, so they play the same pattern every time; Note Bass is played by MIDI notes instead.
#pragma once

#include "params.h"

#include <cstring>

namespace tbmpc
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
// Choices by their place in the list: Waveform Saw 0, Square 1; Slide TB-3PO 0, 303 1; Seed Mode Random 0, Locked 1;
// Scale by its row in scales.h (Semitone 0, Dorian 2, Phrygian 3, Aeolian 6); Density as shown (-7..+7).
constexpr PresetValue acidLine[] = {
    { seedMode, 1.0f }, { seed, 0x5a17 }, { scale, 6.0f }, { density, 5.0f }, { cutoff, 0.465f }, { resonance, 0.78f },
    { envMod, 0.62f }, { decay, 0.3f }, { accent, 0.8f },
};
constexpr PresetValue rootPulse[] = {
    { seedMode, 1.0f }, { seed, 0x0303 }, { scale, 0.0f }, { density, -7.0f }, { cutoff, 0.42f }, { resonance, 0.85f },
    { envMod, 0.7f }, { decay, 0.25f }, { accent, 1.0f },
};
constexpr PresetValue squareLead[] = {
    { seedMode, 1.0f }, { seed, 0xbeef }, { scale, 3.0f }, { density, 6.0f }, { octave, 1.0f }, { waveform, 1.0f },
    { glide, 1.0f }, { cutoff, 0.571f }, { resonance, 0.7f }, { envMod, 0.45f }, { decay, 0.45f }, { accent, 0.5f },
};
constexpr PresetValue dubSteps[] = {
    { seedMode, 1.0f }, { seed, 0x1d0b }, { scale, 2.0f }, { density, 3.0f }, { steps, 12.0f }, { octave, -1.0f },
    { cutoff, 0.496f }, { resonance, 0.5f }, { envMod, 0.35f }, { decay, 0.75f }, { accent, 0.4f }, { volume, 0.8f },
};
// Mode: Notes: played by MIDI notes (held over another: a glide; velocity 100 and up: an accent).
constexpr PresetValue noteBass[] = {
    { mode, 1.0f }, { cutoff, 0.45f }, { resonance, 0.74f }, { envMod, 0.62f }, { decay, 0.38f }, { accent, 0.8f },
};
} // namespace presetValues

#define TBMPC_PRESET(name, key, list) Preset { name, key, list, (uint32_t) (sizeof (list) / sizeof (list[0])) }
inline const Preset presetTable[] = {
    Preset { "Default", "default", nullptr, 0 },
    TBMPC_PRESET ("Acid Line", "acid-line", presetValues::acidLine),
    TBMPC_PRESET ("Root Pulse", "root-pulse", presetValues::rootPulse),
    TBMPC_PRESET ("Square Lead", "square-lead", presetValues::squareLead),
    TBMPC_PRESET ("Dub Steps", "dub-steps", presetValues::dubSteps),
    TBMPC_PRESET ("Note Bass", "note-bass", presetValues::noteBass),
};
#undef TBMPC_PRESET
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
} // namespace tbmpc
