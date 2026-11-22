// MoonBass's factory presets: the patches the demo renders play (tests/host.cpp checks they are the same values and
// the same sound). A preset sets every parameter: the ones listed here, the rest to their defaults.
#pragma once

#include "params.h"

#include <cstring>

namespace moonbass
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
// Choices by their place in the list: LFO Wave Saw Down 1, Sine 2; LFO Mode Free 0, One Shot 2; LFO Clock 1/16 is
// 8; Split Mode C1-C2 is 2, C1-F3 is 6; Pitch Mode Linear 0, Semitones 1. Knobs as 0..1 (params.h has the curves:
// Pitch 0.576 is 52 Hz, 0.414 is 32.7 Hz, the note C1).
constexpr PresetValue gutPunch[] = {
    { tune, 0.7f }, { pitch, 0.576f }, { decay, 0.62f }, { harmonics, 0.2f }, { pulse, 0.7f }, { noise, 0.25f },
    { attack, 0.7f }, { eq, 0.25f }, { gateTime, 0.25f }, { compression, 0.1f },
};
constexpr PresetValue softBoom[] = {
    { tune, 0.15f }, { pitch, 0.5255f }, { decay, 0.85f }, { harmonics, 0.0f }, { pulse, 0.2f }, { noise, 0.05f },
    { attack, 0.3f }, { eq, 0.8f }, { gateTime, 0.2f },
};
constexpr PresetValue subDrop[] = {
    { tune, 0.4f }, { pitch, 0.3838f }, { decay, 0.95f }, { harmonics, 0.05f }, { pulse, 0.3f }, { noise, 0.0f },
    { attack, 0.2f }, { eq, 0.6f },
};
constexpr PresetValue clapKick[] = {
    { tune, 0.45f }, { decay, 0.5f }, { pulse, 0.5f }, { noise, 0.8f }, { attack, 0.85f }, { eq, 0.05f }, { gateTime, 0.95f },
};
constexpr PresetValue metalKick[] = {
    { tune, 0.6f }, { decay, 0.55f }, { harmonics, 0.4f }, { noise, 0.7f }, { attack, 0.7f }, { eq, 0.1f }, { gateTime, 0.6f },
    { metalNoise1, 337.0f }, { metalNoise2, 77.0f }, { noise2Mask, 12.0f },
};
constexpr PresetValue wobbleTail[] = {
    { tune, 0.35f }, { pitch, 0.5f }, { decay, 0.9f }, { lfo1Wave, 2.0f }, { lfo1Int, 0.25f }, { lfo1Mode, 0.0f }, { lfo1Clock, 8.0f },
};
constexpr PresetValue zap[] = {
    { tune, 0.3f }, { decay, 0.55f }, { harmonics, 0.3f }, { lfo1Wave, 1.0f }, { lfo1Speed, 0.75f }, { lfo1Int, 0.6f }, { lfo1Mode, 2.0f },
};
constexpr PresetValue bassLine[] = {
    { splitMode, 6.0f }, { pitchMode, 1.0f }, { pitch, 0.4139f }, { tune, 0.1f }, { decay, 0.8f }, { harmonics, 0.5f },
    { pulse, 0.15f }, { noise, 0.0f }, { attack, 0.2f }, { eq, 0.35f },
};
constexpr PresetValue padTunings[] = {
    { splitMode, 2.0f }, { pitchMode, 0.0f }, { pitch, 0.45f }, { tune, 0.55f }, { decay, 0.6f }, { pulse, 0.6f }, { attack, 0.6f },
};
} // namespace presetValues

#define MOONBASS_PRESET(name, key, list) Preset { name, key, list, (uint32_t) (sizeof (list) / sizeof (list[0])) }
inline const Preset presetTable[] = {
    Preset { "Default", "default", nullptr, 0 },
    MOONBASS_PRESET ("Gut Punch", "gut-punch", presetValues::gutPunch),
    MOONBASS_PRESET ("Soft Boom", "soft-boom", presetValues::softBoom),
    MOONBASS_PRESET ("Sub Drop", "sub-drop", presetValues::subDrop),
    MOONBASS_PRESET ("Clap Kick", "clap-kick", presetValues::clapKick),
    MOONBASS_PRESET ("Metal Kick", "metal-kick", presetValues::metalKick),
    MOONBASS_PRESET ("Wobble Tail", "wobble-tail", presetValues::wobbleTail),
    MOONBASS_PRESET ("Zap", "zap", presetValues::zap),
    MOONBASS_PRESET ("Bass Line", "bass-line", presetValues::bassLine),
    MOONBASS_PRESET ("Pad Tunings", "pad-tunings", presetValues::padTunings),
};
#undef MOONBASS_PRESET

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
} // namespace moonbass
