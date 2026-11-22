// The factory presets. A preset sets every parameter: the ones listed, the rest to their defaults, Data A-H to its
// machine's own defaults first. Keys are never changed once released (projects name the preset they loaded).
#pragma once

#include "params.h"

#include <algorithm>
#include <cstring>

namespace machine
{
struct PresetValue
{
    uint32_t id;
    float value;
};

struct Preset
{
    const char* name;
    const char* key;
    const PresetValue* values;
    uint32_t count;
};

namespace presetValues
{
using namespace ParamId;
// Times are written in seconds with a negative sign (-0.4 means 0.4 s) and become knob positions at load, so the
// table reads in time, not in knob turns.
constexpr float sec (float s) { return -s; }

constexpr PresetValue fmKeys[] = {
    { dataA, 3.0f / 7.0f }, { dataB, ratioDef (1.0f) }, { dataC, ratioDef (1.0f) }, { dataD, ratioDef (14.0f) },
    { dataF, 0.1f }, { dataH, 0.35f },
    { fmAAttack, 0.0f }, { fmADecay, sec (1.2f) }, { fmAEnd, 0.15f }, { fmALevel, 0.42f },
    { fmBAttack, 0.0f }, { fmBDecay, sec (0.15f) }, { fmBEnd, 0.0f }, { fmBLevel, 0.4f },
    { ampDecay, sec (2.5f) }, { ampRelease, sec (0.4f) },
};
constexpr PresetValue fmBell[] = {
    { dataA, 0.0f }, { dataB, ratioDef (1.0f) }, { dataC, ratioDef (3.5f) }, { dataD, ratioDef (7.0f) },
    { dataF, 0.2f },
    { fmADecay, sec (2.0f) }, { fmAEnd, 0.0f }, { fmALevel, 0.55f },
    { fmBDecay, sec (0.6f) }, { fmBEnd, 0.0f }, { fmBLevel, 0.22f },
    { ampDecay, sec (3.5f) }, { ampRelease, sec (1.5f) },
};
constexpr PresetValue fmBass[] = {
    { dataA, 1.0f / 7.0f }, { dataB, ratioDef (1.0f) }, { dataC, ratioDef (1.0f) }, { dataD, ratioDef (2.0f) },
    { dataF, 0.0f }, { dataG, 0.3f },
    { fmADecay, sec (0.25f) }, { fmAEnd, 0.1f }, { fmALevel, 0.6f },
    { fmBDecay, sec (0.1f) }, { fmBEnd, 0.0f }, { fmBLevel, 0.25f },
    { filterFreq, 0.78f }, { filterEnv, 0.3f }, { filterDecay, sec (0.2f) },
    { ampRelease, sec (0.08f) },
};
constexpr PresetValue fmPad[] = {
    { dataA, 5.0f / 7.0f }, { dataB, ratioDef (1.0f) }, { dataC, ratioDef (2.0f) }, { dataD, ratioDef (1.0f) },
    { dataF, 0.6f }, { dataH, 0.5f },
    { fmAAttack, sec (0.8f) }, { fmADecay, sec (2.0f) }, { fmAEnd, 0.6f }, { fmALevel, 0.3f },
    { fmBAttack, sec (1.0f) }, { fmBDecay, sec (2.0f) }, { fmBEnd, 0.5f }, { fmBLevel, 0.25f },
    { filterFreq, 0.72f }, { filterReso, 0.2f },
    { ampAttack, sec (0.6f) }, { ampRelease, sec (1.5f) },
    { lfoSpeed, 0.35f }, { lfoDepth, 0.12f }, { lfoDest, 1.0f },
};
constexpr PresetValue acidFm[] = {
    { dataA, 0.0f }, { dataB, ratioDef (1.0f) }, { dataC, ratioDef (1.0f) }, { dataD, ratioDef (2.0f) },
    { dataF, 0.0f }, { dataG, 0.5f },
    { fmADecay, sec (0.15f) }, { fmAEnd, 0.05f }, { fmALevel, 0.7f },
    { fmBDecay, sec (0.08f) }, { fmBEnd, 0.0f }, { fmBLevel, 0.3f },
    { filterFreq, 0.45f }, { filterReso, 0.75f }, { filterEnv, 0.55f }, { filterDecay, sec (0.18f) },
    { ampRelease, sec (0.05f) }, { ampDrive, 0.35f }, { volume, 0.6310f }, // VOLUME -3 dB
};
constexpr PresetValue kick[] = { { machine, bd } };
constexpr PresetValue longKick[] = { { machine, bd }, { dataB, 0.78f }, { dataC, 0.35f }, { dataE, 0.2f }, { dataF, 0.35f } };
constexpr PresetValue snare[] = { { machine, sd } };
constexpr PresetValue clap[] = { { machine, cp } };
constexpr PresetValue closedHat[] = { { machine, hh } };
constexpr PresetValue openHat[] = { { machine, hh }, { dataA, 1.0f }, { dataC, 0.45f } };
constexpr PresetValue cymbal[] = { { machine, cycb } };
constexpr PresetValue cowbell[] = { { machine, cycb }, { dataA, 1.0f }, { dataC, 0.4f } };
constexpr PresetValue fmTom[] = { { machine, efm } };
constexpr PresetValue groundSine[] = { { machine, gnd }, { dataD, 0.2f } };
constexpr PresetValue kitPreset[] = { { machine, kit } };

// The second barrage. Data knobs as positions (0..1), times in seconds as negatives (sec), as above.
constexpr PresetValue terrainDrone[] = {
    { machine, terrain }, { dataA, 0.8f }, { dataB, 0.55f }, { dataC, 0.6f }, { dataD, 0.8f }, { dataE, 0.7f }, { dataF, 0.648f },
    { dataG, 0.224f }, { dataH, 0.6f }, { filterFreq, 0.75f }, { filterReso, 0.2f }, { ampAttack, sec (0.8f) }, { ampRelease, sec (2.5f) },
    { lfoDest, 6.0f }, { lfoDepth, 0.3f }, { lfoSpeed, 0.3f }, { lfoWave, 4.0f },
};
constexpr PresetValue terrainBass[] = {
    { machine, terrain }, { dataA, 0.4f }, { dataB, 0.7f }, { dataC, 0.45f }, { dataD, 0.35f }, { dataE, 0.25f }, { dataG, 0.0f },
    { dataH, 0.1f }, { ampDecay, sec (0.6f) }, { ampRelease, sec (0.1f) }, { filterFreq, 0.6f }, { filterEnv, 0.3f }, { filterDecay, sec (0.25f) },
};
constexpr PresetValue phaseLead[] = {
    { machine, phase }, { dataA, 0.3f }, { dataB, 0.533f }, { dataC, 0.62f }, { dataD, sec (0.4f) }, { dataE, 0.5f }, { dataF, 0.0f },
    { ampRelease, sec (0.3f) }, { lfoDest, 0.0f }, { lfoDepth, 0.02f }, { lfoSpeed, 0.718f }, { lfoWave, 4.0f },
};
constexpr PresetValue phaseAcid[] = {
    { machine, phase }, { dataA, 0.6f }, { dataB, 0.2f }, { dataC, 0.85f }, { dataD, sec (0.15f) }, { dataE, 0.0f }, { dataF, 0.4f },
    { dataG, 0.3f }, { dataH, 0.1f }, { filterFreq, 0.5f }, { filterReso, 0.7f }, { filterEnv, 0.45f }, { filterDecay, sec (0.2f) },
    { ampRelease, sec (0.06f) }, { ampDrive, 0.3f }, { volume, 0.6495f }, // VOLUME -2.5 dB
};
constexpr PresetValue voxChoir[] = {
    { machine, vox }, { dataB, 0.75f }, { dataD, 2.0f / 7.0f }, { dataE, 0.4f }, { filterFreq, 0.8f }, { ampAttack, sec (0.35f) },
    { ampRelease, sec (1.2f) }, { lfoDest, 4.0f }, { lfoDepth, 0.25f }, { lfoSpeed, 0.3f }, { lfoWave, 4.0f },
};
constexpr PresetValue voxKnock[] = {
    { machine, vox }, { dataB, 0.2f }, { dataC, 0.792f }, { dataD, 0.0f }, { dataE, 0.6f }, { dataF, 0.9f }, { dataG, sec (0.03f) },
    { dataH, sec (0.25f) },
};
constexpr PresetValue marimba[] = {
    { machine, modal }, { dataB, 0.333f }, { dataC, 0.35f }, { dataD, 0.55f }, { dataE, 0.25f }, { dataF, 0.55f }, { dataG, 0.02f },
};
constexpr PresetValue gong[] = {
    { machine, modal }, { dataA, 0.25f }, { dataB, 0.9f }, { dataC, 0.6f }, { dataD, 0.12f }, { dataE, 0.4f }, { dataF, 0.2f },
    { dataG, 0.15f }, { dataH, 0.7f }, { volume, 0.7204f }, // VOLUME -0.7 dB
};
constexpr PresetValue pluckHarp[] = {
    { machine, pluck }, { dataB, sec (2.5f) }, { dataC, 0.7f }, { dataD, 0.5f }, { dataE, 0.15f }, { dataF, 0.05f }, { dataG, 0.2f },
    { dataH, 0.55f },
};
constexpr PresetValue pluckBass[] = {
    { machine, pluck }, { dataB, sec (1.2f) }, { dataC, 0.3f }, { dataD, 0.4f }, { dataE, 0.3f }, { dataG, 0.5f }, { dataH, 0.62f },
    { filterFreq, 0.7f },
};
constexpr PresetValue gendyInsects[] = {
    { machine, gendy }, { dataA, 9.0f / 13.0f }, { dataB, 0.6f }, { dataC, 0.45f }, { dataD, 0.5f }, { dataE, 0.3f }, { dataF, 0.2f },
    { dataG, 0.9f }, { dataH, 0.9f }, { filterType, 2.0f }, { filterFreq, 0.7f }, { filterReso, 0.3f }, { ampRelease, sec (0.4f) },
};
constexpr PresetValue gendyBuzz[] = {
    { machine, gendy }, { dataA, 3.0f / 13.0f }, { dataB, 0.25f }, { dataC, 0.1f }, { dataD, 0.15f }, { dataE, 1.0f }, { dataF, 0.3f },
    { dataG, 0.7f }, { dataH, 0.5f }, { filterFreq, 0.65f }, { filterReso, 0.35f }, { filterEnv, 0.3f },
    { volume, 0.7000f }, // VOLUME -1.2 dB
};
constexpr PresetValue pulsarRhythm[] = {
    { machine, pulsar }, { dataB, 0.646f }, { dataC, 0.85f }, { dataD, 0.2f }, { dataE, 0.5f }, { dataF, 1.0f }, { dataG, 0.1f },
};
constexpr PresetValue pulsarFormant[] = {
    { machine, pulsar }, { dataB, 0.452f }, { dataC, 0.0f }, { dataE, 0.9f }, { ampAttack, sec (0.05f) }, { ampRelease, sec (0.5f) },
    { lfoDest, 4.0f }, { lfoDepth, 0.2f }, { lfoSpeed, 0.25f }, { lfoWave, 4.0f }, { volume, 0.6880f }, // VOLUME -1.5 dB
};
constexpr PresetValue drawbarOrgan[] = {
    { machine, organ }, { ampRelease, sec (0.05f) }, { lfoDest, 0.0f }, { lfoDepth, 0.015f }, { lfoSpeed, 0.72f }, { lfoWave, 4.0f },
};
constexpr PresetValue fullOrgan[] = {
    { machine, organ }, { dataA, 1.0f }, { dataB, 0.75f }, { dataC, 1.0f }, { dataD, 1.0f }, { dataE, 0.75f }, { dataF, 1.0f },
    { dataG, 0.5f }, { dataH, 0.75f }, { ampDrive, 0.25f }, { ampRelease, sec (0.05f) }, { volume, 0.6684f }, // VOLUME -2 dB
};
constexpr PresetValue resonatorKick[] = { { machine, Kind::kick } };
constexpr PresetValue boomKick[] = {
    { machine, Kind::kick }, { dataA, 0.159f }, { dataB, 0.6f }, { dataC, sec (0.06f) }, { dataD, sec (1.1f) }, { dataF, 0.15f },
    { dataG, 0.4f }, { dataH, 0.55f }, { dataI, 0.541f }, { dataJ, 0.4f },
};
// A kit of the new machines: the resonator kick, the snare, clap and closed hat, a woodblock (MODAL), a plucked tom
// (PLUCK), a vocal knock (VOX) and a formant blip (PULSAR).
constexpr uint32_t slotMachine (uint32_t s) { return kitBase + s * kitStride + kitMachine; }
constexpr uint32_t slotData (uint32_t s, uint32_t k) { return kitBase + s * kitStride + kitData + k; }
constexpr PresetValue kitExplorer[] = {
    { machine, kit }, { slotMachine (0), 11.0f }, { slotMachine (4), 7.0f }, { slotMachine (5), 8.0f }, { slotMachine (6), 9.0f },
    { slotMachine (7), 10.0f },
    { slotData (4, 0), 0.75f }, { slotData (4, 1), 0.333f }, { slotData (4, 3), 0.8f }, { slotData (4, 5), 0.9f },
    { slotData (5, 0), 0.646f }, { slotData (5, 1), sec (0.5f) },
    { slotData (6, 3), 0.0f }, { slotData (6, 5), 0.9f }, { slotData (6, 6), sec (0.03f) }, { slotData (6, 7), sec (0.2f) },
    { slotData (7, 1), 0.75f }, { slotData (7, 4), 0.5f }, { slotData (7, 7), sec (0.15f) },
};
} // namespace presetValues

#define MACHINE_PRESET(name, key, list) Preset { name, key, list, (uint32_t) (sizeof (list) / sizeof (list[0])) }
inline const Preset presetTable[] = {
    Preset { "Default", "default", nullptr, 0 },
    MACHINE_PRESET ("FM Keys", "fm-keys", presetValues::fmKeys),
    MACHINE_PRESET ("FM Bell", "fm-bell", presetValues::fmBell),
    MACHINE_PRESET ("FM Bass", "fm-bass", presetValues::fmBass),
    MACHINE_PRESET ("FM Pad", "fm-pad", presetValues::fmPad),
    MACHINE_PRESET ("Acid FM", "acid-fm", presetValues::acidFm),
    MACHINE_PRESET ("Kick", "kick", presetValues::kick),
    MACHINE_PRESET ("Long Kick", "long-kick", presetValues::longKick),
    MACHINE_PRESET ("Snare", "snare", presetValues::snare),
    MACHINE_PRESET ("Clap", "clap", presetValues::clap),
    MACHINE_PRESET ("Closed Hat", "closed-hat", presetValues::closedHat),
    MACHINE_PRESET ("Open Hat", "open-hat", presetValues::openHat),
    MACHINE_PRESET ("Cymbal", "cymbal", presetValues::cymbal),
    MACHINE_PRESET ("Cowbell", "cowbell", presetValues::cowbell),
    MACHINE_PRESET ("FM Tom", "fm-tom", presetValues::fmTom),
    MACHINE_PRESET ("Ground Sine", "ground-sine", presetValues::groundSine),
    MACHINE_PRESET ("Kit", "kit", presetValues::kitPreset),
    MACHINE_PRESET ("Terrain Drone", "terrain-drone", presetValues::terrainDrone),
    MACHINE_PRESET ("Terrain Bass", "terrain-bass", presetValues::terrainBass),
    MACHINE_PRESET ("Phase Lead", "phase-lead", presetValues::phaseLead),
    MACHINE_PRESET ("Phase Acid", "phase-acid", presetValues::phaseAcid),
    MACHINE_PRESET ("Vox Choir", "vox-choir", presetValues::voxChoir),
    MACHINE_PRESET ("Vox Knock", "vox-knock", presetValues::voxKnock),
    MACHINE_PRESET ("Marimba", "marimba", presetValues::marimba),
    MACHINE_PRESET ("Gong", "gong", presetValues::gong),
    MACHINE_PRESET ("Pluck Harp", "pluck-harp", presetValues::pluckHarp),
    MACHINE_PRESET ("Pluck Bass", "pluck-bass", presetValues::pluckBass),
    MACHINE_PRESET ("Gendy Insects", "gendy-insects", presetValues::gendyInsects),
    MACHINE_PRESET ("Gendy Buzz", "gendy-buzz", presetValues::gendyBuzz),
    MACHINE_PRESET ("Pulsar Rhythm", "pulsar-rhythm", presetValues::pulsarRhythm),
    MACHINE_PRESET ("Pulsar Formant", "pulsar-formant", presetValues::pulsarFormant),
    MACHINE_PRESET ("Drawbar Organ", "drawbar-organ", presetValues::drawbarOrgan),
    MACHINE_PRESET ("Full Organ", "full-organ", presetValues::fullOrgan),
    MACHINE_PRESET ("Resonator Kick", "resonator-kick", presetValues::resonatorKick),
    MACHINE_PRESET ("Boom Kick", "boom-kick", presetValues::boomKick),
    MACHINE_PRESET ("Kit Explorer", "kit-explorer", presetValues::kitExplorer),
};
#undef MACHINE_PRESET
constexpr uint32_t presetCount = sizeof (presetTable) / sizeof (presetTable[0]);

inline const Preset* findPreset (const char* key)
{
    if (key == nullptr) return nullptr;
    for (const auto& p : presetTable)
        if (std::strcmp (p.key, key) == 0) return &p;
    return nullptr;
}

// Every parameter's value under `preset`, in the table's order.
inline void presetValuesOf (const Preset& preset, float* out)
{
    for (uint32_t i = 0; i < paramCount; ++i) out[i] = paramAt (i).def;
    for (uint32_t n = 0; n < preset.count; ++n) // its machine first: Data A-H start from that machine's defaults
        if (preset.values[n].id == ParamId::machine)
        {
            const uint8_t kind = (uint8_t) preset.values[n].value;
            out[paramIndex (ParamId::machine)] = (float) kind;
            for (uint32_t k = 0; k < dataCount; ++k)
                out[paramIndex (dataId (k))] = kind != kit && dataSpec (kind, k).used() ? dataSpec (kind, k).def : 0.0f;
        }
    for (uint32_t n = 0; n < preset.count; ++n) // a KIT slot's machine next: its Data start from that machine's defaults
    {
        uint32_t slot = 0, field = 0;
        if (! kitField (preset.values[n].id, slot, field) || field != ParamId::kitMachine) continue;
        const uint8_t kind = slotKinds[std::min<int> ((int) preset.values[n].value, slotKindCount - 1)];
        out[paramIndex (preset.values[n].id)] = preset.values[n].value;
        for (uint32_t k = 0; k < dataCount; ++k)
            out[paramIndex (kitDataId (slot, k))] = dataSpec (kind, k).used() ? dataSpec (kind, k).def : 0.0f;
    }
    const uint8_t mainKind = (uint8_t) out[paramIndex (ParamId::machine)];
    for (uint32_t n = 0; n < preset.count; ++n)
    {
        const uint32_t id = preset.values[n].id;
        const int index = paramIndex (id);
        uint32_t slot = 0, field = 0;
        const bool slotField = kitField (id, slot, field);
        if (index < 0 || id == ParamId::machine || (slotField && field == ParamId::kitMachine)) continue;
        float v = preset.values[n].value;
        const auto& p = paramAt ((uint32_t) index);
        if (v < 0.0f && (p.text == Text::time || p.text == Text::timeInf)) v = timeKnob (-v); // written in seconds
        if (v < 0.0f && dataOf (id) >= 0) // a Data time, in seconds, under its machine
        {
            const uint8_t kind = slotField ? slotKinds[std::min<int> ((int) out[paramIndex (kitId (slot, ParamId::kitMachine))], slotKindCount - 1)] : mainKind;
            const Unit unit = dataSpec (kind, (uint32_t) dataOf (id)).unit;
            if (unit == Unit::seconds || unit == Unit::decayInf) v = timeKnob (-v);
        }
        out[index] = clampParam (p, v);
    }
}
} // namespace machine
