// Foundry's factory presets: each starts from every default and moves what it needs. Load keys are like ids: never
// rename one once released (a host may keep it).
#pragma once

#include "params.h"

namespace foundry
{
enum VoiceIndex : int { BD, RS, SD, CP, LT, MT, HT, CH, OH, CY, RD, CB, CL, MA, CG, B2 };

inline void setValue (float* values, uint32_t id, float v)
{
    const int i = paramIndex (id);
    if (i >= 0)
        values[i] = clampParam (table().rows[i], v);
}
inline void vset (float* values, int voice, uint32_t offset, float v) { setValue (values, voiceId (voice, offset), v); }
inline void slotSet (float* values, int voice, int slot, float v) { vset (values, voice, slotId (slot), v); }
inline void allModels (float* values, int model)
{
    for (int v = 0; v < voiceCount; ++v)
        vset (values, v, V::model, (float) model);
}

namespace presets
{
inline void none (float*) {}

inline void kit808 (float* v)
{
    allModels (v, m808);
    vset (v, BD, V::decay, 0.55f), slotSet (v, BD, 0, 0.3f), slotSet (v, BD, 2, 0.15f);
    slotSet (v, SD, 1, 0.6f), slotSet (v, SD, 2, 0.35f), slotSet (v, SD, 5, 0.1f);
    for (int t : { LT, MT, HT })
        slotSet (v, t, 0, 0.3f), slotSet (v, t, 6, -1.0f);
    vset (v, B2, V::decay, 0.8f), vset (v, B2, V::tune, -2.0f), slotSet (v, B2, 0, 0.2f), slotSet (v, B2, 2, 0.1f);
}

inline void kit909 (float* v)
{
    allModels (v, m909);
    vset (v, BD, V::decay, 0.4f), slotSet (v, BD, 0, 0.6f), slotSet (v, BD, 1, 0.65f), slotSet (v, BD, 2, 0.5f), slotSet (v, BD, 3, 0.3f);
    vset (v, BD, V::drive, 0.15f);
    slotSet (v, SD, 0, 0.2f), slotSet (v, SD, 1, 0.65f), slotSet (v, SD, 3, 0.1f), slotSet (v, SD, 5, 0.35f);
    for (int t : { LT, MT, HT })
        slotSet (v, t, 3, 0.5f), slotSet (v, t, 6, -0.3f), slotSet (v, t, 0, 0.15f);
    slotSet (v, CH, 6, 0.2f), vset (v, CH, V::decay, 0.3f), vset (v, OH, V::decay, 0.45f);
    vset (v, B2, V::tune, 3.0f), slotSet (v, B2, 0, 0.8f);
}

inline void kit606 (float* v)
{
    allModels (v, m606);
    setValue (v, M::accent, 0.6f);
    vset (v, BD, V::decay, 0.4f), slotSet (v, BD, 0, 0.3f);
    slotSet (v, SD, 1, 0.7f);
    vset (v, CH, V::decay, 0.3f), vset (v, OH, V::decay, 0.4f), vset (v, CY, V::decay, 0.45f);
}

// The 606 with the classic hardware mods dialled in: tuned and lengthened drums, more snap, a harder accent, the
// hats' decays split, a little drive and crush.
inline void mods606 (float* v)
{
    kit606 (v);
    setValue (v, M::accent, 0.8f);
    for (int i = 0; i < voiceCount; ++i)
        vset (v, i, V::accent, 0.8f);
    vset (v, BD, V::tune, -3.0f), vset (v, BD, V::decay, 0.75f), slotSet (v, BD, 0, 0.6f), slotSet (v, BD, 2, 0.35f), vset (v, BD, V::drive, 0.2f);
    vset (v, SD, V::tune, 2.0f), vset (v, SD, V::decay, 0.5f), slotSet (v, SD, 0, 0.3f), slotSet (v, SD, 1, 0.8f), slotSet (v, SD, 2, 0.55f);
    vset (v, LT, V::tune, -4.0f), vset (v, LT, V::decay, 0.65f);
    vset (v, HT, V::tune, 3.0f), vset (v, HT, V::decay, 0.6f);
    vset (v, CH, V::decay, 0.25f), slotSet (v, CH, 0, 0.3f), vset (v, CH, V::bendType, 0.66f), vset (v, CH, V::bend, 0.25f);
    vset (v, OH, V::decay, 0.6f), slotSet (v, OH, 0, 0.2f);
    vset (v, CY, V::decay, 0.7f);
}

inline void bent808 (float* v)
{
    allModels (v, m808);
    slotSet (v, BD, 4, 0.4f), slotSet (v, BD, 2, 0.5f), vset (v, BD, V::bendType, 0.1f), vset (v, BD, V::bend, 0.3f);
    slotSet (v, SD, 7, 0.5f), vset (v, SD, V::bendType, 0.4f), vset (v, SD, V::bend, 0.5f);
    vset (v, CP, V::bendType, 0.9f), vset (v, CP, V::bend, 0.4f);
    slotSet (v, CH, 2, 0.6f), slotSet (v, CH, 5, 0.4f), vset (v, CH, V::bendType, 0.7f), vset (v, CH, V::bend, 0.3f);
    slotSet (v, OH, 2, 0.5f), slotSet (v, OH, 3, 0.6f);
    slotSet (v, CB, 2, 0.5f), slotSet (v, CB, 3, 0.37f), slotSet (v, CB, 4, 0.3f);
    slotSet (v, CY, 5, 0.6f), slotSet (v, CY, 4, 0.8f);
    for (int t : { LT, MT, HT })
        slotSet (v, t, 7, 0.5f), slotSet (v, t, 3, 0.6f);
}

inline void techno909 (float* v)
{
    kit909 (v);
    vset (v, BD, V::drive, 0.3f), vset (v, BD, V::decay, 0.5f), vset (v, BD, V::tune, -2.0f);
    setValue (v, M::comp, 0.45f);
    setValue (v, M::scSource, 1.0f); // BD
    for (int t : { CH, OH, CY, RD, CP })
        vset (v, t, V::duck, 0.5f);
    vset (v, SD, V::duck, 0.2f);
    vset (v, CP, V::delay, 0.25f), vset (v, CH, V::delay, 0.1f);
    vset (v, CP, V::reverb, 0.2f), vset (v, SD, V::reverb, 0.15f), vset (v, RD, V::reverb, 0.1f);
    setValue (v, M::afxDrive, 0.2f);
    setValue (v, M::dlyTime, 4.0f); // 1/16D
}

inline void bass808 (float* v)
{
    kit808 (v);
    setValue (v, M::keysVoice, 1.0f); // BD
    vset (v, BD, V::decay, 0.85f), slotSet (v, BD, 0, 0.2f), slotSet (v, BD, 1, 0.4f), slotSet (v, BD, 2, 0.15f), slotSet (v, BD, 4, 0.25f);
    vset (v, BD, V::drive, 0.2f);
    vset (v, B2, V::model, (float) m909), vset (v, B2, V::level, -6.0f), vset (v, B2, V::decay, 0.2f), vset (v, B2, V::tune, 5.0f);
}

inline void bigRoom (float* v)
{
    allModels (v, m909);
    vset (v, SD, V::reverb, 0.45f), vset (v, SD, V::gate, 0.5f), slotSet (v, SD, 1, 0.7f);
    for (int t : { LT, MT, HT })
        vset (v, t, V::reverb, 0.3f), slotSet (v, t, 3, 0.6f), vset (v, t, V::decay, 0.6f);
    vset (v, CP, V::reverb, 0.4f);
    setValue (v, M::revTime, 0.55f), setValue (v, M::revTone, 0.6f), setValue (v, M::revPreDelay, 20.0f);
}
} // namespace presets

struct Preset
{
    const char* key;
    const char* name;
    void (*apply) (float* values);
};

inline constexpr Preset presetTable[] = {
    { "default", "Default", presets::none },
    { "808-kit", "808 Kit", presets::kit808 },
    { "909-kit", "909 Kit", presets::kit909 },
    { "606-kit", "606 Kit", presets::kit606 },
    { "606-mods", "606 Mods", presets::mods606 },
    { "bent-808", "Bent 808", presets::bent808 },
    { "techno-909", "Techno 909", presets::techno909 },
    { "808-bass", "808 Bass Keys", presets::bass808 },
    { "big-room", "Big Room", presets::bigRoom },
};

inline const Preset* findPreset (const char* key)
{
    if (key == nullptr)
        return nullptr;
    for (const auto& p : presetTable)
        if (std::strcmp (p.key, key) == 0)
            return &p;
    return nullptr;
}

// Every value a preset gives, in table order.
inline void presetValuesOf (const Preset& preset, float* values)
{
    const auto& t = table();
    for (uint32_t i = 0; i < t.count; ++i)
        values[i] = t.rows[i].def;
    preset.apply (values);
}
} // namespace foundry
