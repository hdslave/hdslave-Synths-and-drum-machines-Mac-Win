// Nexus's factory presets: values that differ from the defaults (params.h), by parameter id. Times and rates are
// the knobs' positions (params.h's curves). Keys are like ids: never renamed once released (a host may store them).
#pragma once

#include "params.h"

#include <cstring>

namespace nexus
{
struct PresetValue { uint16_t id; float value; };
struct Preset { const char* key; const char* name; const PresetValue* values; int count; };

inline constexpr PresetValue p_init[] = { { 0, 8.0f } }; // the defaults
inline constexpr PresetValue p_filter_bass[] = { { 0, 0.0f }, { 1, -12.0f }, { 2, 0.35f }, { 3, 0.35f }, { 4, 0.2f }, { 5, 0.35f }, { 9, 2.0f }, { 11, 0.3f }, { 13, 2.0f }, { 18, 0.271442f }, { 27, 0.0736806f }, { 28, 0.382586f }, { 29, 0.55f }, { 30, 0.262074f } };
inline constexpr PresetValue p_saw_lead[] = { { 2, 0.55f }, { 4, 0.85f }, { 9, 2.0f }, { 13, 2.0f }, { 15, 2.0f }, { 16, 0.12f }, { 17, 0.6f }, { 18, 0.327107f }, { 27, 0.0928318f }, { 28, 0.430887f }, { 29, 0.8f }, { 30, 0.341995f }, { 38, 0.51614f }, { 62, 0.08f } };
inline constexpr PresetValue p_square_bass[] = { { 1, -12.0f }, { 2, 0.0f }, { 3, 0.25f }, { 4, 0.0f }, { 9, 2.0f }, { 13, 0.0f }, { 27, 0.0584804f }, { 28, 0.353035f }, { 29, 0.4f }, { 30, 0.228943f } };
inline constexpr PresetValue p_pd_keys[] = { { 0, 1.0f }, { 2, 0.3f }, { 3, 0.45f }, { 4, 0.3f }, { 5, 0.5f }, { 10, 0.7f }, { 11, 0.5f } };
inline constexpr PresetValue p_fm_piano[] = { { 0, 2.0f }, { 2, 0.0f }, { 4, 0.4f }, { 11, 0.75f } };
inline constexpr PresetValue p_fm_bells[] = { { 0, 3.0f }, { 2, 0.2f }, { 4, 0.35f }, { 11, 0.85f } };
inline constexpr PresetValue p_terrain_lead[] = { { 0, 5.0f }, { 2, 0.4f }, { 3, 0.6f }, { 9, 2.0f }, { 13, 2.0f }, { 18, 0.292402f }, { 27, 0.125992f }, { 28, 0.391487f }, { 29, 0.75f }, { 30, 0.368403f }, { 38, 0.246621f }, { 66, 0.25f } };
inline constexpr PresetValue p_string_ensemble[] = { { 0, 6.0f }, { 3, 0.55f }, { 4, 0.6f }, { 9, 2.0f }, { 14, 6.0f }, { 27, 0.44814f }, { 28, 0.493242f }, { 29, 0.85f }, { 30, 0.654213f } };
inline constexpr PresetValue p_chip_arp[] = { { 0, 7.0f }, { 3, 0.4f }, { 4, 0.3f }, { 12, 5.0f } };
inline constexpr PresetValue p_fold_bass[] = { { 0, 9.0f }, { 1, -12.0f }, { 2, 0.3f }, { 3, 0.25f }, { 4, 0.35f }, { 5, 0.6f }, { 11, 0.4f }, { 13, 0.0f } };
inline constexpr PresetValue p_fm_pluck[] = { { 0, 10.0f }, { 2, 0.45f }, { 3, 0.55f }, { 4, 0.3f }, { 5, 0.5f }, { 9, 1.0f }, { 11, 0.45f } };
inline constexpr PresetValue p_formant_voice[] = { { 0, 11.0f }, { 2, 0.4f }, { 4, 0.4f }, { 9, 2.0f }, { 27, 0.228943f }, { 28, 0.430887f }, { 29, 0.8f }, { 30, 0.430887f }, { 38, 0.215443f }, { 66, 0.2f } };
inline constexpr PresetValue p_drawbar_organ[] = { { 0, 12.0f }, { 2, 0.3f }, { 3, 0.4f }, { 4, 0.3f }, { 9, 2.0f }, { 27, 0.0843433f }, { 28, 0.271442f }, { 29, 1.0f }, { 30, 0.228943f } };
inline constexpr PresetValue p_wavetable_pad[] = { { 0, 13.0f }, { 2, 0.25f }, { 3, 0.4f }, { 9, 2.0f }, { 14, 6.0f }, { 27, 0.519249f }, { 28, 0.542884f }, { 29, 0.8f }, { 30, 0.711379f }, { 37, 1.0f }, { 38, 0.170998f }, { 68, 0.3f } };
inline constexpr PresetValue p_chord_stab[] = { { 0, 14.0f }, { 2, 0.35f }, { 4, 0.4f }, { 9, 1.0f }, { 11, 0.4f }, { 13, 0.0f } };
inline constexpr PresetValue p_speech[] = { { 0, 15.0f }, { 2, 0.6f }, { 9, 2.0f }, { 28, 0.493242f }, { 29, 0.9f }, { 30, 0.341995f } };
inline constexpr PresetValue p_swarm_pad[] = { { 0, 16.0f }, { 2, 0.4f }, { 3, 0.4f }, { 9, 2.0f }, { 27, 0.564622f }, { 28, 0.542884f }, { 29, 0.85f }, { 30, 0.736806f } };
inline constexpr PresetValue p_noise_sweep[] = { { 0, 17.0f }, { 2, 0.6f }, { 4, 0.6f }, { 9, 2.0f }, { 27, 0.391487f }, { 28, 0.493242f }, { 30, 0.564622f }, { 38, 0.184202f }, { 66, 0.4f } };
inline constexpr PresetValue p_particle_drops[] = { { 0, 18.0f }, { 2, 0.4f }, { 3, 0.4f }, { 4, 0.7f }, { 9, 1.0f }, { 11, 0.7f } };
inline constexpr PresetValue p_plucked_string[] = { { 0, 19.0f }, { 2, 0.2f }, { 4, 0.45f } };
inline constexpr PresetValue p_modal_bell[] = { { 0, 20.0f }, { 2, 0.55f }, { 3, 0.6f }, { 4, 0.65f } };
inline constexpr PresetValue p_kick[] = { { 0, 21.0f }, { 2, 0.45f }, { 3, 0.45f }, { 4, 0.55f } };
inline constexpr PresetValue p_snare[] = { { 0, 22.0f }, { 4, 0.45f } };
inline constexpr PresetValue p_hi_hat[] = { { 0, 23.0f }, { 3, 0.6f }, { 4, 0.3f } };

inline constexpr Preset presetTable[] = {
    { "init", "Init", p_init, 1 },
    { "filter-bass", "Filter Bass", p_filter_bass, 14 },
    { "saw-lead", "Saw Lead", p_saw_lead, 14 },
    { "square-bass", "Square Bass", p_square_bass, 10 },
    { "pd-keys", "PD Keys", p_pd_keys, 7 },
    { "fm-piano", "FM Piano", p_fm_piano, 4 },
    { "fm-bells", "FM Bells", p_fm_bells, 4 },
    { "terrain-lead", "Terrain Lead", p_terrain_lead, 12 },
    { "string-ensemble", "String Ensemble", p_string_ensemble, 9 },
    { "chip-arp", "Chip Arp", p_chip_arp, 4 },
    { "fold-bass", "Fold Bass", p_fold_bass, 8 },
    { "fm-pluck", "FM Pluck", p_fm_pluck, 7 },
    { "formant-voice", "Formant Voice", p_formant_voice, 10 },
    { "drawbar-organ", "Drawbar Organ", p_drawbar_organ, 9 },
    { "wavetable-pad", "Wavetable Pad", p_wavetable_pad, 12 },
    { "chord-stab", "Chord Stab", p_chord_stab, 6 },
    { "speech", "Speech", p_speech, 6 },
    { "swarm-pad", "Swarm Pad", p_swarm_pad, 8 },
    { "noise-sweep", "Noise Sweep", p_noise_sweep, 9 },
    { "particle-drops", "Particle Drops", p_particle_drops, 6 },
    { "plucked-string", "Plucked String", p_plucked_string, 3 },
    { "modal-bell", "Modal Bell", p_modal_bell, 4 },
    { "kick", "Kick", p_kick, 4 },
    { "snare", "Snare", p_snare, 2 },
    { "hi-hat", "Hi-Hat", p_hi_hat, 3 },
};
constexpr int presetCount = (int) (sizeof (presetTable) / sizeof (presetTable[0]));

inline const Preset* findPreset (const char* key)
{
    if (key == nullptr) return nullptr;
    for (const auto& p : presetTable)
        if (std::strcmp (p.key, key) == 0) return &p;
    return nullptr;
}

// A preset's whole set of values: the defaults, then its own.
inline void presetValuesOf (const Preset& preset, float* values)
{
    for (int i = 0; i < paramCount; ++i)
        values[i] = paramTable[i].def;
    for (int i = 0; i < preset.count; ++i)
        values[preset.values[i].id] = clampParam (paramTable[preset.values[i].id], preset.values[i].value);
}
} // namespace nexus
