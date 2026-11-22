// Reverie's factory sounds: values that differ from the defaults (params.h), by parameter id. A sound never holds
// the chord section (ids below ParamId::chordSection): loading one keeps the progression, key, voicing, strum and
// latch as they are, so sounds and progressions are picked apart. Keys are like ids: never renamed once released (a
// host may store them).
#pragma once

#include "params.h"

#include <cstring>

namespace reverie
{
struct PresetValue { uint32_t id; float value; };
struct Preset { const char* key; const char* name; const PresetValue* values; int count; };

namespace presetdata
{
using namespace ParamId;
// The default sound (Velvet Saw and Choir, slow orbit), named.
inline const PresetValue reverie[] = { { volume, 0.7f } };
inline const PresetValue velvetNebula[] = {
    { osc1Pos, 0.55f }, { osc1Unison, 4 }, { osc1Detune, detuneKnob (18) }, { osc2Wave, 3 }, { osc2Pos, 0.3f }, { osc2Unison, 3 },
    { osc2Level, 0.55f }, { filterType, 1 }, { cutoff, cutoffKnob (2200) }, { resonance, 0.2f }, { filterEnv, 0.3f },
    { ampAttack, envKnob (1200) }, { ampRelease, envKnob (4000) }, { ensMix, 0.55f }, { revMix, 0.5f }, { revSize, 0.85f },
    { revDecay, decayKnob (8) }, { shimmer, 0.1f }, { motionRate, motionKnob (0.06f) }, { motionDepth, 0.45f } };
inline const PresetValue glassCathedral[] = {
    { osc1Wave, 5 }, { osc1Pos, 0.6f }, { osc1Unison, 2 }, { osc1Detune, detuneKnob (6) }, { osc1Level, 0.7f }, { osc2Wave, 7 },
    { osc2Pos, 0.7f }, { osc2Octave, 1 }, { osc2Unison, 2 }, { osc2Level, 0.45f }, { cutoff, cutoffKnob (7000) }, { resonance, 0.1f },
    { ampAttack, envKnob (400) }, { ampDecay, envKnob (3000) }, { ampSustain, 0.7f }, { ampRelease, envKnob (5000) },
    { revMix, 0.6f }, { revSize, 1.0f }, { revDecay, decayKnob (14) }, { revDamp, dampKnob (9000) }, { shimmer, 0.45f },
    { dlyMix, 0.2f }, { motionShape, Motion::figure8 }, { motionRate, motionKnob (0.1f) }, { xDest1, Dest::wave1 }, { xAmt1, 0.6f },
    { yAmt1, 0.3f }, { volume, 0.90f } };
inline const PresetValue choirOfMoons[] = {
    { osc1Wave, 2 }, { osc1Pos, 0.25f }, { osc1Unison, 3 }, { osc1Detune, detuneKnob (12) }, { osc1Level, 0.85f }, { osc2Pos, 0.7f },
    { osc2Fine, -6 }, { air, 0.25f }, { airTone, 0.5f }, { cutoff, cutoffKnob (3500) }, { ampAttack, envKnob (900) },
    { motionShape, Motion::pendulum }, { motionRate, motionKnob (0.07f) }, { xAmt1, 0.7f }, { xDest2, Dest::off }, { yAmt1, 0.25f },
    { yDest2, Dest::air }, { yAmt2, 0.4f }, { revMix, 0.5f }, { revDecay, decayKnob (9) }, { shimmer, 0.2f }, { volume, 0.62f } };
inline const PresetValue silkStrings[] = {
    { osc1Wave, 3 }, { osc1Pos, 0.4f }, { osc1Unison, 4 }, { osc1Detune, detuneKnob (16) }, { osc2Wave, 0 }, { osc2Pos, 0.2f },
    { osc2Octave, -1 }, { osc2Level, 0.35f }, { filterType, 1 }, { cutoff, cutoffKnob (3000) }, { filterEnv, 0.2f }, { keyTrack, 0.5f },
    { ampAttack, envKnob (500) }, { ampRelease, envKnob (2200) }, { ensMix, 0.75f }, { ensDepth, 0.7f }, { revMix, 0.4f },
    { revDecay, decayKnob (4) }, { motionShape, Motion::tide }, { motionRate, motionKnob (0.05f) }, { motionDepth, 0.3f },
    { lfoPitch, 0.08f } };
inline const PresetValue auroraDrift[] = {
    { osc1Wave, 6 }, { osc1Pos, 0.3f }, { osc1Detune, detuneKnob (10) }, { osc2Wave, 6 }, { osc2Pos, 0.65f }, { osc2Unison, 3 },
    { osc2Detune, detuneKnob (14) }, { osc2Level, 0.7f }, { cutoff, cutoffKnob (5000) }, { resonance, 0.25f },
    { motionRate, motionKnob (0.12f) }, { motionDepth, 0.7f }, { motionSpread, 0.8f }, { xDest1, Dest::wave1 }, { xAmt1, 0.8f },
    { xDest2, Dest::wave2 }, { xAmt2, -0.8f }, { yAmt1, 0.3f }, { yDest2, Dest::resonance }, { yAmt2, 0.3f }, { revMix, 0.45f },
    { revDecay, decayKnob (7) }, { shimmer, 0.25f }, { dlyMix, 0.18f } };
inline const PresetValue celestialKeys[] = {
    { osc1Wave, 11 }, { osc1Pos, 0.6f }, { osc1Unison, 1 }, { osc2Wave, 5 }, { osc2Pos, 0.4f }, { osc2Octave, 1 }, { osc2Level, 0.35f },
    { cutoff, cutoffKnob (6000) }, { filterEnv, 0.3f }, { velCutoff, 0.6f }, { ampAttack, envKnob (3) }, { ampDecay, envKnob (2500) },
    { ampSustain, 0.35f }, { ampRelease, envKnob (1500) }, { fltAttack, envKnob (2) }, { fltDecay, envKnob (1500) }, { fltSustain, 0.3f },
    { ensMix, 0.35f }, { revMix, 0.35f }, { revDecay, decayKnob (4.5f) }, { motionDepth, 0.2f }, { motionRate, motionKnob (0.05f) }, { volume, 1.00f } };
inline const PresetValue deepOcean[] = {
    { osc1Wave, 8 }, { osc1Pos, 0.4f }, { osc1Detune, detuneKnob (9) }, { osc2Wave, 8 }, { osc2Pos, 0.1f }, { osc2Octave, -1 },
    { osc2Unison, 1 }, { osc2Level, 0.5f }, { filterType, 1 }, { cutoff, cutoffKnob (900) }, { resonance, 0.3f }, { filterEnv, 0.35f },
    { ampAttack, envKnob (1500) }, { ampRelease, envKnob (5000) }, { motionShape, Motion::wander }, { motionRate, motionKnob (0.05f) },
    { motionDepth, 0.6f }, { yAmt1, 0.5f }, { revMix, 0.55f }, { revSize, 0.95f }, { revDecay, decayKnob (12) }, { revDamp, dampKnob (3000) },
    { dlyMix, 0.1f }, { volume, 0.72f } };
inline const PresetValue starfield[] = {
    { osc1Wave, 7 }, { osc1Pos, 0.5f }, { osc1Unison, 2 }, { osc2Wave, 5 }, { osc2Pos, 0.8f }, { osc2Octave, 1 }, { osc2Level, 0.4f },
    { air, 0.15f }, { cutoff, cutoffKnob (9000) }, { ampAttack, envKnob (1100) }, { ampRelease, envKnob (6000) },
    { motionShape, Motion::wander }, { motionRate, motionKnob (0.09f) }, { motionDepth, 0.65f }, { motionSpread, 1.0f },
    { xAmt1, 0.6f }, { xDest2, Dest::width }, { yDest1, Dest::air }, { yAmt1, 0.6f }, { yDest2, Dest::cutoff }, { yAmt2, 0.3f },
    { dlyMix, 0.28f }, { dlyFeedback, 0.5f }, { revMix, 0.55f }, { revDecay, decayKnob (16) }, { shimmer, 0.5f } };
inline const PresetValue lofiDream[] = {
    { osc1Wave, 8 }, { osc1Pos, 0.6f }, { osc1Unison, 2 }, { osc1Detune, detuneKnob (12) }, { osc2Wave, 4 }, { osc2Pos, 0.3f },
    { osc2Unison, 1 }, { osc2Level, 0.45f }, { drift, driftKnob (11) }, { cutoff, cutoffKnob (1600) }, { resonance, 0.12f },
    { ampAttack, envKnob (150) }, { ampRelease, envKnob (1200) }, { lfoShape, LfoShape::drift }, { lfoRate, lfoKnob (1.0f) },
    { lfoPitch, 0.2f }, { ensMix, 0.3f }, { revMix, 0.25f }, { revDecay, decayKnob (2.5f) }, { revDamp, dampKnob (3500) },
    { dlyMix, 0.15f }, { shimmer, 0.0f }, { volume, 0.62f } };
inline const PresetValue gospelGlow[] = {
    { osc1Wave, 4 }, { osc1Pos, 0.75f }, { osc1Unison, 2 }, { osc1Detune, detuneKnob (6) }, { osc2Wave, 3 }, { osc2Pos, 0.4f },
    { osc2Unison, 3 }, { osc2Level, 0.45f }, { cutoff, cutoffKnob (4500) }, { ampAttack, envKnob (120) }, { ampSustain, 0.9f },
    { ampRelease, envKnob (1400) }, { ensMix, 0.65f }, { ensDepth, 0.8f }, { ensRate, ensKnob (0.9f) }, { revMix, 0.3f },
    { revDecay, decayKnob (3) }, { shimmer, 0.0f }, { motionDepth, 0.25f }, { volume, 0.64f } };
inline const PresetValue breathOfAngels[] = {
    { osc1Wave, 9 }, { osc1Pos, 0.4f }, { osc1Detune, detuneKnob (10) }, { osc2Pos, 0.55f }, { osc2Level, 0.65f }, { air, 0.4f },
    { airTone, 0.7f }, { cutoff, cutoffKnob (4000) }, { ampAttack, envKnob (1600) }, { ampRelease, envKnob (5000) },
    { motionShape, Motion::figure8 }, { motionRate, motionKnob (0.05f) }, { yDest2, Dest::air }, { yAmt2, 0.5f }, { revMix, 0.6f },
    { revDecay, decayKnob (11) }, { shimmer, 0.35f } };
inline const PresetValue velourSwell[] = {
    { osc1Pos, 0.75f }, { osc1Unison, 4 }, { osc1Detune, detuneKnob (15) }, { osc2Wave, 1 }, { osc2Pos, 0.3f }, { osc2Level, 0.5f },
    { filterType, 1 }, { cutoff, cutoffKnob (600) }, { resonance, 0.25f }, { filterEnv, 0.6f }, { keyTrack, 0.4f },
    { fltAttack, envKnob (2500) }, { fltDecay, envKnob (4000) }, { fltSustain, 0.55f }, { ampAttack, envKnob (1400) }, { revMix, 0.4f } };
inline const PresetValue crystalHarp[] = {
    { osc1Wave, 5 }, { osc1Pos, 0.3f }, { osc1Unison, 1 }, { osc2Wave, 11 }, { osc2Pos, 0.3f }, { osc2Octave, 1 }, { osc2Level, 0.4f },
    { ampAttack, envKnob (1) }, { ampDecay, envKnob (1800) }, { ampSustain, 0.0f }, { ampRelease, envKnob (2000) },
    { cutoff, cutoffKnob (5000) }, { filterEnv, 0.4f }, { fltAttack, envKnob (1) }, { fltDecay, envKnob (800) }, { fltSustain, 0.0f },
    { revMix, 0.45f }, { revDecay, decayKnob (6) }, { dlyMix, 0.22f }, { dlyTime, 7 }, { motionDepth, 0.15f }, { volume, 1.25f } };
inline const PresetValue darkMatter[] = {
    { osc1Wave, 10 }, { osc1Pos, 0.6f }, { osc1Octave, -1 }, { osc1Detune, detuneKnob (20) }, { osc2Wave, 6 }, { osc2Pos, 0.2f },
    { osc2Level, 0.5f }, { filterType, 1 }, { cutoff, cutoffKnob (700) }, { resonance, 0.45f }, { motionShape, Motion::spiral },
    { motionRate, motionKnob (0.04f) }, { motionDepth, 0.8f }, { yAmt1, 0.6f }, { yDest2, Dest::resonance }, { yAmt2, 0.3f },
    { revMix, 0.5f }, { revDecay, decayKnob (14) }, { revDamp, dampKnob (2500) }, { shimmer, 0.1f }, { ampRelease, envKnob (6000) },
    { volume, 0.75f } };
inline const PresetValue sunriseBloom[] = {
    { osc1Pos, 0.3f }, { osc2Wave, 7 }, { osc2Pos, 0.6f }, { osc2Octave, 1 }, { osc2Level, 0.45f }, { cutoff, cutoffKnob (500) },
    { filterEnv, 0.75f }, { fltAttack, envKnob (4000) }, { fltDecay, envKnob (6000) }, { fltSustain, 0.7f },
    { ampAttack, envKnob (2500) }, { ampRelease, envKnob (5000) }, { motionShape, Motion::tide }, { motionRate, motionKnob (0.03f) },
    { motionRetrig, 1 }, { motionDepth, 0.6f }, { revMix, 0.45f }, { revDecay, decayKnob (8) }, { shimmer, 0.3f }, { volume, 0.78f } };
inline const PresetValue etherealVox[] = {
    { osc1Wave, 2 }, { osc1Pos, 0.6f }, { osc1Unison, 4 }, { osc1Detune, detuneKnob (16) }, { osc2Wave, 9 }, { osc2Pos, 0.3f },
    { osc2Unison, 1 }, { osc2Level, 0.35f }, { cutoff, cutoffKnob (3200) }, { resonance, 0.2f }, { motionShape, Motion::lissajous },
    { motionRate, motionKnob (0.08f) }, { xDest1, Dest::wave1 }, { xAmt1, 0.7f }, { xAmt2, 0.4f }, { yAmt1, 0.25f },
    { yDest2, Dest::pitch }, { yAmt2, 0.03f }, { revMix, 0.5f }, { revDecay, decayKnob (8) }, { volume, 0.78f } };
inline const PresetValue mysticReed[] = {
    { osc1Wave, 10 }, { osc1Pos, 0.4f }, { osc1Unison, 2 }, { osc1Detune, detuneKnob (8) }, { osc2Wave, 3 }, { osc2Pos, 0.6f },
    { osc2Level, 0.55f }, { cutoff, cutoffKnob (2600) }, { resonance, 0.25f }, { lfoRate, lfoKnob (4.5f) }, { lfoPitch, 0.08f },
    { lfoDelay, lfoDelayKnob (1000) }, { ensMix, 0.4f }, { revMix, 0.4f }, { revDecay, decayKnob (5) } };
inline const PresetValue polarLights[] = {
    { osc1Wave, 1 }, { osc1Pos, 0.5f }, { osc1Unison, 4 }, { osc1Detune, detuneKnob (14) }, { osc2Wave, 6 }, { osc2Pos, 0.5f },
    { osc2Level, 0.6f }, { filterType, 1 }, { cutoff, cutoffKnob (2600) }, { resonance, 0.35f }, { motionShape, Motion::star },
    { motionRate, motionKnob (0.07f) }, { motionDepth, 0.7f }, { motionSpread, 0.7f }, { xDest1, Dest::wave1 }, { xAmt1, 0.7f },
    { xDest2, Dest::wave2 }, { xAmt2, -0.6f }, { yAmt1, 0.45f }, { yDest2, Dest::width }, { yAmt2, 0.3f }, { dlyMix, 0.2f },
    { dlyFeedback, 0.45f }, { revMix, 0.5f }, { revDecay, decayKnob (10) }, { shimmer, 0.3f } };
inline const PresetValue satinKeys[] = {
    { osc1Wave, 11 }, { osc1Pos, 0.7f }, { osc1Unison, 2 }, { osc1Detune, detuneKnob (5) }, { osc2Wave, 8 }, { osc2Pos, 0.2f },
    { osc2Level, 0.4f }, { ampAttack, envKnob (5) }, { ampDecay, envKnob (3000) }, { ampSustain, 0.5f }, { ampRelease, envKnob (1800) },
    { cutoff, cutoffKnob (3500) }, { filterEnv, 0.25f }, { fltAttack, envKnob (3) }, { fltDecay, envKnob (1200) }, { fltSustain, 0.2f },
    { ensMix, 0.5f }, { ensRate, ensKnob (0.5f) }, { revMix, 0.3f }, { revDecay, decayKnob (3) }, { shimmer, 0.0f }, { motionDepth, 0.2f }, { volume, 0.88f } };
} // namespace presetdata

#define REVERIE_PRESET(key, name, list) { key, name, presetdata::list, (int) (sizeof (presetdata::list) / sizeof (presetdata::list[0])) }
inline const Preset presetTable[] = {
    REVERIE_PRESET ("reverie", "Reverie", reverie),
    REVERIE_PRESET ("velvet-nebula", "Velvet Nebula", velvetNebula),
    REVERIE_PRESET ("glass-cathedral", "Glass Cathedral", glassCathedral),
    REVERIE_PRESET ("choir-of-moons", "Choir of Moons", choirOfMoons),
    REVERIE_PRESET ("silk-strings", "Silk Strings", silkStrings),
    REVERIE_PRESET ("aurora-drift", "Aurora Drift", auroraDrift),
    REVERIE_PRESET ("celestial-keys", "Celestial Keys", celestialKeys),
    REVERIE_PRESET ("deep-ocean", "Deep Ocean", deepOcean),
    REVERIE_PRESET ("starfield", "Starfield", starfield),
    REVERIE_PRESET ("lofi-dream", "Lo-Fi Dream", lofiDream),
    REVERIE_PRESET ("gospel-glow", "Gospel Glow", gospelGlow),
    REVERIE_PRESET ("breath-of-angels", "Breath of Angels", breathOfAngels),
    REVERIE_PRESET ("velour-swell", "Velour Swell", velourSwell),
    REVERIE_PRESET ("crystal-harp", "Crystal Harp", crystalHarp),
    REVERIE_PRESET ("dark-matter", "Dark Matter", darkMatter),
    REVERIE_PRESET ("sunrise-bloom", "Sunrise Bloom", sunriseBloom),
    REVERIE_PRESET ("ethereal-vox", "Ethereal Vox", etherealVox),
    REVERIE_PRESET ("mystic-reed", "Mystic Reed", mysticReed),
    REVERIE_PRESET ("polar-lights", "Polar Lights", polarLights),
    REVERIE_PRESET ("satin-keys", "Satin Keys", satinKeys),
};
#undef REVERIE_PRESET
constexpr int presetCount = (int) (sizeof (presetTable) / sizeof (presetTable[0]));

inline const Preset* findPreset (const char* key)
{
    if (key == nullptr) return nullptr;
    for (const auto& p : presetTable)
        if (std::strcmp (p.key, key) == 0) return &p;
    return nullptr;
}

// A sound's values over `values` (the current ones): every sound parameter to its default, then the sound's own; the
// chord section kept.
inline void presetValuesOf (const Preset& preset, float* values)
{
    for (int i = (int) ParamId::chordSection; i < paramCount; ++i)
        values[i] = paramTable[i].def;
    for (int i = 0; i < preset.count; ++i)
        if (preset.values[i].id >= ParamId::chordSection && preset.values[i].id < (uint32_t) paramCount)
            values[preset.values[i].id] = clampParam (paramTable[preset.values[i].id], preset.values[i].value);
}
} // namespace reverie
