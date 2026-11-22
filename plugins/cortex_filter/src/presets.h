// Cortex Filter's factory presets: the settings the demo renders play (tests/host.cpp checks each one's values). A preset
// sets every parameter but the edit filter's (which show the filter it picks): the ones listed here, the rest to their
// defaults.
#pragma once

#include "params.h"

#include <cstring>

namespace cortex
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
// Output evens each preset's loudness with the dry input (K-weighted, on tools/balance.sh's loop: each within about
// 2 dB of it). Choices by their place in the list. Filter: the factory's (filters.h), F03 Vowels A E O U, F05 Phase Drift,
// F06 Harmonic Comb, F07 Wah Talk, F08 Bell Tower, F09 Radio Tuner, F10 Acid Twins, F12 Swirl, F16 Chord Bank. LFO Sync:
// Free 0, 4 Bars 2, 1 Bar 4, 1/2 5. LFO Shape: Sine 0, Triangle 1, Drift 6. Sequence: Resonators 3, Acid Steps 5,
// Vowel Talk 6, Bar Gate 7, Up And Down 8. Clocks: 1 Bar 4, 1/4 7, 1/4T 8, 1/8 9, 1/16 11; Reset: 1 Bar 1, 2 Bars 2. Env
// Trigger: Inc 1. A preset leaves the bank and the sequences as they are: it picks from them.
constexpr PresetValue talkingLoop[] = {
    { filter, 3.0f }, { morph, 0.5f }, { freq, 0.5f }, { transform, 0.5f }, { lfoSync, 4.0f }, { lfoPhase, 0.25f },
    { lfoMorph, 0.9f }, { lfoTransform, 0.9f }, { output, 5.5f },
};
constexpr PresetValue phaseDrift[] = {
    { filter, 5.0f }, { morph, 0.3f }, { freq, 0.35f }, { transform, 0.4f }, { lfoRate, 0.2917f }, { lfoFreq, 0.8f },
};
constexpr PresetValue envelopeWah[] = {
    { filter, 7.0f }, { morph, 0.2f }, { freq, 0.1f }, { transform, 0.5f }, { envGain, 6.0f }, { envAttack, 0.3f },
    { envRelease, 0.45f }, { envFreq, 0.9f }, { output, 7.5f },
};
constexpr PresetValue harmonicSwirl[] = {
    { filter, 6.0f }, { morph, 0.0f }, { freq, 0.4f }, { transform, 0.0f }, { lfoSync, 5.0f }, { lfoShape, 1.0f },
    { lfoMorph, 1.0f }, { spread, 0.6f }, { mix, 0.8f }, { output, 2.0f },
};
constexpr PresetValue bellRinger[] = {
    { filter, 8.0f }, { morph, 0.6f }, { freq, 0.45f }, { transform, 0.6f }, { drive, 6.0f }, { mix, 0.7f },
};
constexpr PresetValue acidEnvelope[] = {
    { filter, 10.0f }, { morph, 0.1f }, { freq, 0.35f }, { transform, 0.3f }, { emphasis, 0.3f }, { drive, 9.0f },
    { envAttack, 0.2f }, { envRelease, 0.5f }, { envFreq, 0.5f }, { output, -2.0f },
};
constexpr PresetValue radioDrift[] = {
    { filter, 9.0f }, { morph, 0.8f }, { freq, 0.5f }, { transform, 0.4f }, { lfoShape, 6.0f }, { lfoRate, 0.35f },
    { lfoFreq, 0.5f }, { lfoTransform, 0.6f }, { output, 4.5f },
};
constexpr PresetValue wideSpace[] = {
    { filter, 12.0f }, { morph, 0.5f }, { freq, 0.5f }, { transform, 0.2f }, { lfoSync, 2.0f }, { lfoPhase, 0.333f },
    { lfoMorph, 0.8f }, { lfoFreq, 0.4f }, { lfoTransform, 0.4f }, { spread, 1.0f }, { mix, 0.85f }, { output, 2.5f },
};
constexpr PresetValue acidSequence[] = {
    { filter, 10.0f }, { sequence, 5.0f }, { incClock, 11.0f }, { resetClock, 1.0f }, { morph, 0.15f }, { freq, 0.45f }, { transform, 0.35f },
    { emphasis, 0.2f }, { glide, 0.25f }, { envAttack, 0.2f }, { envRelease, 0.45f }, { envFreq, 0.3f }, { output, 3.5f },
};
constexpr PresetValue vowelWalk[] = {
    { filter, 3.0f }, { sequence, 6.0f }, { incClock, 9.0f }, { resetClock, 2.0f }, { morph, 0.5f }, { freq, 0.5f }, { transform, 0.5f },
    { lfoSync, 4.0f }, { lfoMorph, 0.4f }, { output, 7.0f },
};
constexpr PresetValue hitSteps[] = {
    { filter, 8.0f }, { sequence, 3.0f }, { incClock, 0.0f }, { envTrigger, 1.0f }, { trigLevel, -20.0f }, { envAttack, 0.15f },
    { envRelease, 0.4f }, { morph, 0.5f }, { freq, 0.45f }, { transform, 0.5f }, { mix, 0.7f }, { output, 3.0f },
};
constexpr PresetValue barGate[] = {
    { filter, 27.0f }, { sequence, 7.0f }, { incClock, 9.0f }, { resetClock, 1.0f }, { morph, 0.5f }, { freq, 0.5f }, { transform, 0.4f },
    { output, 1.5f },
};
constexpr PresetValue wander[] = {
    { filter, 1.0f }, { sequence, 8.0f }, { incClock, 7.0f }, { decClock, 8.0f }, { morph, 0.3f }, { freq, 0.5f }, { transform, 0.3f },
    { lfoSync, 3.0f }, { lfoFreq, 0.3f }, { output, 3.0f },
};
constexpr PresetValue chordGlow[] = {
    { filter, 16.0f }, { morph, 0.0f }, { freq, 0.3f }, { transform, 0.5f }, { lfoSync, 2.0f }, { lfoShape, 1.0f },
    { lfoMorph, 1.0f }, { mix, 0.6f }, { output, 1.0f },
};
} // namespace presetValues

#define PRESET(name, key, list) Preset { name, key, presetValues::list, (uint32_t) (sizeof (presetValues::list) / sizeof (presetValues::list[0])) }
inline constexpr Preset presetTable[] = {
    Preset { "Init", "init", nullptr, 0 },
    PRESET ("Talking Loop", "talking-loop", talkingLoop),
    PRESET ("Phase Drift", "phase-drift", phaseDrift),
    PRESET ("Envelope Wah", "envelope-wah", envelopeWah),
    PRESET ("Harmonic Swirl", "harmonic-swirl", harmonicSwirl),
    PRESET ("Bell Ringer", "bell-ringer", bellRinger),
    PRESET ("Acid Envelope", "acid-envelope", acidEnvelope),
    PRESET ("Radio Drift", "radio-drift", radioDrift),
    PRESET ("Wide Space", "wide-space", wideSpace),
    PRESET ("Chord Glow", "chord-glow", chordGlow),
    PRESET ("Acid Sequence", "acid-sequence", acidSequence),
    PRESET ("Vowel Walk", "vowel-walk", vowelWalk),
    PRESET ("Hit Steps", "hit-steps", hitSteps),
    PRESET ("Bar Gate", "bar-gate", barGate),
    PRESET ("Wander", "wander", wander),
};
#undef PRESET
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

// Every parameter's value under a preset, in the table's order.
inline void presetValuesOf (const Preset& preset, float* values)
{
    for (uint32_t i = 0; i < paramCount; ++i)
        values[i] = paramTable[i].def;
    for (uint32_t i = 0; i < preset.count; ++i)
    {
        const int index = paramIndex (preset.values[i].id);
        if (index >= 0)
            values[index] = clampParam (paramTable[index], preset.values[i].value);
    }
}
} // namespace cortex
