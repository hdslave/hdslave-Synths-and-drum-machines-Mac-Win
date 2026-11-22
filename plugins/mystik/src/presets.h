// Mystik's factory presets. A preset sets every parameter: the ones listed here, the rest to their defaults. Load keys
// are like ids: never renamed once released (a host may store them).
//
// The fourteen of 0.1 are ported to the 0.2 engine: their values exactly as they were, and Human (new in 0.2) at 0, as
// 0.1 had no variation from hit to hit. Goblet Maqsum is 0.2's own.
#pragma once

#include "params.h"

#include <cstring>

namespace mystik
{
struct PresetValue
{
    uint32_t id;
    float value;
};

struct Preset
{
    const char* name; // as a host lists it
    const char* key;  // its load key (CLAP preset-load)
    const PresetValue* values;
    uint32_t count;
};

namespace presetValues
{
using namespace ParamId;
// Choices by their place: Body Tabla 0 Conga 1 Frame 2 Udu 3 Slit 4 Handpan 5 Bowl 6 Bell 7 Goblet 8; FM Ratio 1 is "1", 10 is
// 1.594, 14 is 2.918; Seq Mode Pattern 0 Euclid 1 Turing 2 Automaton 3 Bounce 4 Rhythm 5; Rate 1/4 1, 1/8 3, 1/8T 4,
// 1/16 5; Direction Brownian 3 Random 4; Scale Minor 2 Phrygian 4 Hijaz 5 Bhairav 6 Pent Major 8 Hirajoshi 9 Pelog 10
// Slendro 11; Rhythm Bell 12/8 5 Tumbao 6 Maqsum 7 Teental 11; Echo Time 1/16T 1, 1/16D 4, 1/4T 6, 1/8D 7, 1/4D 10;
// Trig Rest 0 Hit 1 Accent 2 Slide 3 Acc+Slide 4; Stroke Open 0 Slap 1 Bass 2 Mute 3 Ghost 4 Flam 5 Roll 6 Press 7.
constexpr PresetValue standard[] = { { human, 0.0f }, { tune, 9.0f } };

// A west-coast bongo: the oscillator alone, pinged through the gate, an inharmonic FM ratio.
constexpr PresetValue westBongo[] = { { human, 0.0f },
    { tune, 12.0f }, { source, 0.0f }, { fmRatio, 10.0f }, { fmIndex, 0.35f }, { timbre, 0.25f }, { bloom, 0.5f }, { bend, 0.15f },
    { strike, 0.25f }, { decay, 0.2f }, { memory, 0.15f }, { seqMode, 1.0f }, { density, 0.45f }, { accents, 0.4f }, { scale, 7.0f },
    { echo, 0.2f }, { space, 0.15f },
    { pitch1 + 0, 0 }, { pitch1 + 1, 2 }, { pitch1 + 2, 4 }, { pitch1 + 3, 1 }, { pitch1 + 4, 3 }, { pitch1 + 5, 0 }, { pitch1 + 6, 5 },
    { pitch1 + 7, 2 }, { pitch1 + 8, 0 }, { pitch1 + 9, 4 }, { pitch1 + 10, 1 }, { pitch1 + 11, 3 }, { pitch1 + 12, 5 }, { pitch1 + 13, 2 },
    { pitch1 + 14, 4 }, { pitch1 + 15, 6 },
};

// A tabla theka: the rhythm's strokes, the bayan's low dha an octave under (its steps' Pitch at -7 in Bhairav).
constexpr PresetValue tablaTeental[] = { { human, 0.0f },
    { body, 0.0f }, { tune, 12.0f }, { source, 0.85f }, { ring, 0.55f }, { position, 0.4f }, { snap, 0.15f }, { bend, 0.25f },
    { fmIndex, 0.1f }, { timbre, 0.05f }, { decay, 0.35f }, { tune, 14.0f }, { seqMode, 5.0f }, { rhythm, 11.0f }, { rate, 3.0f }, { scale, 6.0f },
    { accents, 0.4f }, { echo, 0.0f }, { space, 0.12f },
    { pitch1 + 0, -7 }, { pitch1 + 3, -7 }, { pitch1 + 4, -7 }, { pitch1 + 7, -7 }, { pitch1 + 8, -7 }, { pitch1 + 15, -7 },
    { pitch1 + 1, 0 }, { pitch1 + 2, 0 }, { pitch1 + 5, 0 }, { pitch1 + 6, 0 }, { pitch1 + 9, 1 }, { pitch1 + 10, 1 },
    { pitch1 + 11, 2 }, { pitch1 + 12, 2 }, { pitch1 + 13, 0 }, { pitch1 + 14, 0 },
};

constexpr PresetValue congaTumbao[] = { { human, 0.0f },
    { body, 1.0f }, { tune, 12.0f }, { source, 0.7f }, { position, 0.35f }, { decay, 0.28f }, { seqMode, 5.0f }, { rhythm, 6.0f }, { rate, 3.0f },
    { swing, 0.56f }, { accents, 0.3f }, { scale, 2.0f }, { space, 0.2f }, { echo, 0.08f },
    { pitch1 + 2, 0 }, { pitch1 + 6, 0 }, { pitch1 + 7, 2 },
};

// The pot's air rises after each strike (Udu bends up), walking a Hirajoshi scale.
constexpr PresetValue uduBloom[] = { { human, 0.0f },
    { body, 3.0f }, { tune, -2.0f }, { source, 0.9f }, { ring, 0.6f }, { bend, 0.5f }, { decay, 0.55f }, { memory, 0.6f },
    { bloom, 0.2f }, { seqMode, 0.0f }, { direction, 3.0f }, { density, 0.55f }, { scale, 9.0f }, { space, 0.35f },
    { spaceDecay, 0.65f }, { echo, 0.1f },
};

// A 303-ish line through the gate's Lowpass mode: a folded oscillator, resonance near ringing, held notes and slides.
constexpr PresetValue acidLine[] = { { human, 0.0f },
    { tune, -12.0f }, { source, 0.0f }, { fmIndex, 0.05f }, { fmRatio, 1.0f }, { timbre, 0.45f }, { symmetry, 0.35f },
    { bloom, 0.45f }, { gateMode, 1.0f }, { resonance, 0.85f }, { strike, 0.55f }, { decay, 0.15f }, { hold, 0.45f }, { memory, 0.0f },
    { accent, 0.8f }, { glide, 0.35f }, { bend, 0.0f }, { drift, 0.05f }, { scale, 2.0f }, { gate, 0.55f }, { swing, 0.52f },
    { drive, 0.35f }, { echo, 0.15f }, { space, 0.05f },
    { pitch1 + 0, 0 }, { pitch1 + 1, 0 }, { pitch1 + 2, 7 }, { pitch1 + 3, 0 }, { pitch1 + 4, 3 }, { pitch1 + 5, 0 }, { pitch1 + 6, 5 },
    { pitch1 + 7, 7 }, { pitch1 + 8, 0 }, { pitch1 + 9, 10 }, { pitch1 + 10, 0 }, { pitch1 + 11, 2 }, { pitch1 + 12, -2 },
    { pitch1 + 13, 0 }, { pitch1 + 14, 3 }, { pitch1 + 15, 4 },
    { trig1 + 0, 2 }, { trig1 + 1, 1 }, { trig1 + 2, 3 }, { trig1 + 3, 1 }, { trig1 + 4, 2 }, { trig1 + 5, 0 }, { trig1 + 6, 1 },
    { trig1 + 7, 4 }, { trig1 + 8, 1 }, { trig1 + 9, 3 }, { trig1 + 10, 1 }, { trig1 + 11, 1 }, { trig1 + 12, 2 }, { trig1 + 13, 1 },
    { trig1 + 14, 3 }, { trig1 + 15, 1 },
    { stroke1 + 0, 0 }, { stroke1 + 1, 3 }, { stroke1 + 2, 0 }, { stroke1 + 3, 3 }, { stroke1 + 4, 0 }, { stroke1 + 5, 0 },
    { stroke1 + 6, 0 }, { stroke1 + 7, 0 }, { stroke1 + 8, 3 }, { stroke1 + 9, 0 }, { stroke1 + 10, 0 }, { stroke1 + 11, 6 },
    { stroke1 + 12, 0 }, { stroke1 + 13, 3 }, { stroke1 + 14, 0 }, { stroke1 + 15, 0 },
    { chance1 + 6, 1 }, { chance1 + 13, 1 },
};

constexpr PresetValue turingAcid[] = { { human, 0.0f },
    { tune, -12.0f }, { source, 0.0f }, { fmIndex, 0.05f }, { fmRatio, 1.0f }, { timbre, 0.4f }, { symmetry, 0.3f },
    { bloom, 0.5f }, { gateMode, 0.9f }, { resonance, 0.75f }, { strike, 0.5f }, { decay, 0.15f }, { hold, 0.4f }, { memory, 0.0f },
    { accent, 0.8f }, { glide, 0.3f }, { bend, 0.0f }, { seqMode, 2.0f }, { length, 12.0f }, { mutate, 0.15f }, { density, 0.7f },
    { range, 0.35f }, { accents, 0.45f }, { scale, 4.0f }, { gate, 0.6f }, { drive, 0.3f }, { echo, 0.25f }, { echoTime, 4.0f },
    { stroke1 + 0, 0 }, { stroke1 + 1, 0 }, { stroke1 + 2, 0 }, { stroke1 + 3, 0 }, { stroke1 + 4, 0 }, { stroke1 + 5, 0 },
    { stroke1 + 6, 0 }, { stroke1 + 7, 0 }, { stroke1 + 8, 0 }, { stroke1 + 9, 0 }, { stroke1 + 10, 0 }, { stroke1 + 11, 0 },
};

constexpr PresetValue singingBowl[] = { { human, 0.0f },
    { body, 6.0f }, { source, 1.0f }, { ring, 0.95f }, { position, 0.5f }, { snap, 0.05f }, { bend, 0.0f }, { open, 0.8f },
    { decay, 0.8f }, { bloom, 0.0f }, { fmIndex, 0.0f }, { timbre, 0.0f }, { seqMode, 3.0f }, { rate, 1.0f }, { length, 8.0f },
    { rule, 90.0f }, { density, 0.3f }, { scale, 10.0f }, { space, 0.5f }, { spaceDecay, 0.8f }, { echo, 0.15f }, { echoTime, 10.0f },
    { volume, 0.6f },
};

constexpr PresetValue slitEuclid[] = { { human, 0.0f },
    { body, 4.0f }, { source, 0.9f }, { tune, 5.0f }, { ring, 0.4f }, { position, 0.6f }, { seqMode, 1.0f }, { density, 0.31f },
    { rotate, 2.0f }, { accents, 0.4f }, { scale, 11.0f }, { echo, 0.3f }, { repeats, 0.55f }, { absorb, 0.6f },
    { pitch1 + 0, 0 }, { pitch1 + 1, 3 }, { pitch1 + 2, 1 }, { pitch1 + 3, 4 }, { pitch1 + 4, 2 }, { pitch1 + 5, 5 }, { pitch1 + 6, 0 },
    { pitch1 + 7, 3 }, { pitch1 + 8, -1 }, { pitch1 + 9, 2 }, { pitch1 + 10, 4 }, { pitch1 + 11, 1 }, { pitch1 + 12, 5 }, { pitch1 + 13, 3 },
    { pitch1 + 14, 0 }, { pitch1 + 15, 2 },
};

constexpr PresetValue handpanDrift[] = { { human, 0.0f },
    { body, 5.0f }, { source, 0.85f }, { tune, 12.0f }, { ring, 0.8f }, { drift, 0.35f }, { direction, 4.0f }, { density, 0.45f },
    { mutate, 0.4f }, { scale, 8.0f }, { rate, 3.0f }, { space, 0.4f }, { echo, 0.2f }, { echoTime, 6.0f },
    { pitch1 + 0, 0 }, { pitch1 + 1, 2 }, { pitch1 + 2, 4 }, { pitch1 + 3, 5 }, { pitch1 + 4, 7 }, { pitch1 + 5, 9 }, { pitch1 + 6, 4 },
    { pitch1 + 7, 2 }, { pitch1 + 8, 0 }, { pitch1 + 9, 5 }, { pitch1 + 10, 3 }, { pitch1 + 11, 7 }, { pitch1 + 12, 1 }, { pitch1 + 13, 4 },
    { pitch1 + 14, 6 }, { pitch1 + 15, 2 },
};

// A doumbek's maqsum: dum (Bass) and tek (Slap, a fifth up), ghost fills from Mutate, in Hijaz.
constexpr PresetValue maqsumNight[] = { { human, 0.0f },
    { body, 2.0f }, { tune, 9.0f }, { source, 0.75f }, { snap, 0.4f }, { ring, 0.35f }, { seqMode, 5.0f }, { rhythm, 7.0f },
    { rate, 3.0f }, { scale, 5.0f }, { accents, 0.5f }, { mutate, 0.2f }, { space, 0.25f },
    { pitch1 + 0, 0 }, { pitch1 + 1, 4 }, { pitch1 + 3, 4 }, { pitch1 + 4, 0 }, { pitch1 + 6, 4 },
};

constexpr PresetValue feedbackWitch[] = { { human, 0.0f },
    { source, 0.2f }, { fmIndex, 0.7f }, { fmRatio, 14.0f }, { feedback, 0.75f }, { timbre, 0.55f }, { symmetry, -0.4f },
    { bloom, 0.8f }, { gateMode, 0.25f }, { strike, 0.7f }, { decay, 0.4f }, { memory, 0.8f }, { seqMode, 3.0f }, { rule, 30.0f },
    { length, 16.0f }, { density, 0.5f }, { mutate, 0.1f }, { scale, 0.0f }, { range, 0.6f }, { crush, 0.35f }, { drive, 0.4f },
    { echo, 0.3f }, { echoTime, 1.0f }, { repeats, 0.65f },
    { pitch1 + 0, 0 }, { pitch1 + 1, 6 }, { pitch1 + 2, -5 }, { pitch1 + 3, 11 }, { pitch1 + 4, 1 }, { pitch1 + 5, -8 }, { pitch1 + 6, 3 },
    { pitch1 + 7, 13 }, { pitch1 + 8, -2 }, { pitch1 + 9, 7 }, { pitch1 + 10, -11 }, { pitch1 + 11, 4 }, { pitch1 + 12, 9 },
    { pitch1 + 13, -6 }, { pitch1 + 14, 2 }, { pitch1 + 15, 15 },
};

constexpr PresetValue bouncingBall[] = { { human, 0.0f },
    { body, 4.0f }, { source, 0.8f }, { tune, 10.0f }, { ring, 0.35f }, { seqMode, 4.0f }, { length, 16.0f }, { density, 0.6f },
    { curve, 0.7f }, { scale, 8.0f }, { space, 0.3f }, { echo, 0.1f },
    { pitch1 + 0, 9 }, { pitch1 + 1, 8 }, { pitch1 + 2, 7 }, { pitch1 + 3, 6 }, { pitch1 + 4, 5 }, { pitch1 + 5, 4 }, { pitch1 + 6, 3 },
    { pitch1 + 7, 2 }, { pitch1 + 8, 1 }, { pitch1 + 9, 0 }, { pitch1 + 10, -1 }, { pitch1 + 11, -2 }, { pitch1 + 12, -3 },
    { pitch1 + 13, -4 }, { pitch1 + 14, -5 }, { pitch1 + 15, -6 },
};

// The standard bell pattern on a two-tone iron bell, twelve triplet eighths to the bar.
constexpr PresetValue gankoguiBell[] = { { human, 0.0f },
    { body, 7.0f }, { tune, 19.0f }, { source, 0.95f }, { ring, 0.3f }, { position, 0.7f }, { seqMode, 5.0f }, { rhythm, 5.0f },
    { rate, 4.0f }, { scale, 2.0f }, { space, 0.2f },
    { pitch1 + 0, 0 }, { pitch1 + 2, 3 }, { pitch1 + 4, 0 }, { pitch1 + 5, 3 }, { pitch1 + 7, 0 }, { pitch1 + 9, 3 }, { pitch1 + 11, 0 },
};
// 0.2: the maqsum on the Goblet body (a doumbek's own: the dum booms from its air, the tek pops), Human as by default.
constexpr PresetValue gobletMaqsum[] = {
    { body, 8.0f }, { tune, 9.0f }, { source, 0.75f }, { snap, 0.4f }, { ring, 0.35f }, { seqMode, 5.0f }, { rhythm, 7.0f },
    { rate, 3.0f }, { scale, 5.0f }, { accents, 0.5f }, { mutate, 0.2f }, { space, 0.25f },
    { pitch1 + 0, 0 }, { pitch1 + 1, 4 }, { pitch1 + 3, 4 }, { pitch1 + 4, 0 }, { pitch1 + 6, 4 }, { volume, 0.74f },
};
} // namespace presetValues

#define MYSTIK_PRESET(name, key, list) Preset { name, key, presetValues::list, (uint32_t) (sizeof (presetValues::list) / sizeof (presetValues::list[0])) }
inline constexpr Preset presetTable[] = {
    MYSTIK_PRESET ("Mystik", "mystik", standard),
    MYSTIK_PRESET ("West Bongo", "west-bongo", westBongo),
    MYSTIK_PRESET ("Tabla Teental", "tabla-teental", tablaTeental),
    MYSTIK_PRESET ("Conga Tumbao", "conga-tumbao", congaTumbao),
    MYSTIK_PRESET ("Udu Bloom", "udu-bloom", uduBloom),
    MYSTIK_PRESET ("Acid Gate", "acid-gate", acidLine),
    MYSTIK_PRESET ("Turing Acid", "turing-acid", turingAcid),
    MYSTIK_PRESET ("Singing Bowl", "singing-bowl", singingBowl),
    MYSTIK_PRESET ("Slit Euclid", "slit-euclid", slitEuclid),
    MYSTIK_PRESET ("Handpan Drift", "handpan-drift", handpanDrift),
    MYSTIK_PRESET ("Maqsum Night", "maqsum-night", maqsumNight),
    MYSTIK_PRESET ("Feedback Witch", "feedback-witch", feedbackWitch),
    MYSTIK_PRESET ("Bouncing Ball", "bouncing-ball", bouncingBall),
    MYSTIK_PRESET ("Gankogui Bell", "gankogui-bell", gankoguiBell),
    MYSTIK_PRESET ("Goblet Maqsum", "goblet-maqsum", gobletMaqsum),
};
#undef MYSTIK_PRESET
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
} // namespace mystik
