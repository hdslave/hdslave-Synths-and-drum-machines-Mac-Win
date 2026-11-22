// Copyright 2016 Emilie Gillet (plaits/dsp/voice.cc), adapted 2026 for Nexus by hdslave. MIT licence: see
// plaits_voice.h. The rendering is the firmware's; see plaits_voice.h for what changed around it.
#include "plaits_voice.h"
#include "plaits/resources.h"

#include <algorithm>
#include <cmath>

namespace plaits
{
using namespace stmlib;

void NexusVoice::Init (BufferAllocator* allocator)
{
    engines_.Init();
    // The module's order and output gains (the first eight are the green-LED bank of firmware 1.2).
    engines_.RegisterInstance (&virtual_analog_vcf_engine_, false, 1.0f, 1.0f);
    engines_.RegisterInstance (&phase_distortion_engine_, false, 0.7f, 0.7f);
    engines_.RegisterInstance (&six_op_engine_, true, 1.0f, 1.0f);
    engines_.RegisterInstance (&six_op_engine_, true, 1.0f, 1.0f);
    engines_.RegisterInstance (&six_op_engine_, true, 1.0f, 1.0f);
    engines_.RegisterInstance (&wave_terrain_engine_, false, 0.7f, 0.7f);
    engines_.RegisterInstance (&string_machine_engine_, false, 0.8f, 0.8f);
    engines_.RegisterInstance (&chiptune_engine_, false, 0.5f, 0.5f);

    engines_.RegisterInstance (&virtual_analog_engine_, false, 0.8f, 0.8f);
    engines_.RegisterInstance (&waveshaping_engine_, false, 0.7f, 0.6f);
    engines_.RegisterInstance (&fm_engine_, false, 0.6f, 0.6f);
    engines_.RegisterInstance (&grain_engine_, false, 0.7f, 0.6f);
    engines_.RegisterInstance (&additive_engine_, false, 0.8f, 0.8f);
    engines_.RegisterInstance (&wavetable_engine_, false, 0.6f, 0.6f);
    engines_.RegisterInstance (&chord_engine_, false, 0.8f, 0.8f);
    engines_.RegisterInstance (&speech_engine_, false, -0.7f, 0.8f);

    engines_.RegisterInstance (&swarm_engine_, false, -3.0f, 1.0f);
    engines_.RegisterInstance (&noise_engine_, false, -1.0f, -1.0f);
    engines_.RegisterInstance (&particle_engine_, false, -2.0f, 1.0f);
    engines_.RegisterInstance (&string_engine_, true, -1.0f, 0.8f);
    engines_.RegisterInstance (&modal_engine_, true, -1.0f, 0.8f);
    engines_.RegisterInstance (&bass_drum_engine_, true, 0.8f, 0.8f);
    engines_.RegisterInstance (&snare_drum_engine_, true, 0.8f, 0.8f);
    engines_.RegisterInstance (&hi_hat_engine_, true, 0.8f, 0.8f);

    for (int i = 0; i < engines_.size(); ++i)
    {
        // All engines share the same RAM.
        allocator->Free();
        engines_.get (i)->Init (allocator);
    }
    previous_engine_index_ = -1;
    out_post_processor_.Init();
    aux_post_processor_.Init();
    Silence();
}

void NexusVoice::Silence()
{
    out_post_processor_.Init(); // the gates shut, their filters empty
    aux_post_processor_.Init();
    decay_envelope_.Init();
    lpg_envelope_.Init();
    trigger_state_ = false;
    lpg_bypassed_ = true;
    previous_note_ = 0.0f;
    fresh_ = true;
    std::fill (trigger_delay_line_, trigger_delay_line_ + 8, 0.0f);
    trigger_write_ = 0;
    previous_engine_index_ = -1; // the engine is reset (and its gate state cleared) at the next render
}

void NexusVoice::Render (const NexusPatch& patch, const NexusModulations& modulations, float* out, float* aux, size_t size)
{
    // The trigger, delayed by four blocks as the firmware does it (a sequencer's CV may lag its gate).
    trigger_write_ = (trigger_write_ + 1) & 7;
    trigger_delay_line_[trigger_write_] = modulations.trigger;
    const float trigger_value = trigger_delay_line_[(trigger_write_ - 4) & 7];

    const bool previous_trigger_state = trigger_state_;
    if (! previous_trigger_state)
    {
        if (trigger_value > 0.3f)
        {
            trigger_state_ = true;
            if (! modulations.level_patched)
                lpg_envelope_.Trigger();
            decay_envelope_.Trigger();
        }
    }
    else if (trigger_value < 0.1f)
        trigger_state_ = false;

    const int engine_index = std::clamp (patch.engine, 0, engines_.size() - 1);
    Engine* e = engines_.get (engine_index);
    if (engine_index != previous_engine_index_)
    {
        const uint8_t* data = nullptr;
        if (engine_index >= 2 && engine_index <= 4)
            data = fm_patches_table[engine_index - 2];
        e->LoadUserData (data);
        e->Reset();
        out_post_processor_.Reset();
        previous_engine_index_ = engine_index;
    }

    EngineParameters p;
    const bool rising_edge = trigger_state_ && ! previous_trigger_state;
    if (fresh_)
        previous_note_ = modulations.note, fresh_ = false;
    const float note = (modulations.note + previous_note_) * 0.5f;
    previous_note_ = modulations.note;
    const PostProcessingSettings& pp_s = e->post_processing_settings;

    if (modulations.trigger_patched)
        p.trigger = (rising_edge ? TRIGGER_RISING_EDGE : TRIGGER_LOW) | (trigger_state_ ? TRIGGER_HIGH : TRIGGER_LOW);
    else
        p.trigger = TRIGGER_UNPATCHED;

    const float short_decay = (200.0f * kBlockSize) / kSampleRate * SemitonesToRatio (-96.0f * patch.decay);
    decay_envelope_.Process (short_decay * 2.0f);

    float compressed_level = 1.3f * modulations.level / (0.3f + std::fabs (modulations.level));
    CONSTRAIN (compressed_level, 0.0f, 1.0f);
    p.accent = modulations.level_patched ? compressed_level : 0.8f;
    // At an accent of exactly 1 the 6-op engines read one entry past their velocity table (lut_cube_root); a hair under
    // it they read the last two. A level at full scale (a gate, Env 1 at its peak) gives exactly 1.
    p.accent = std::min (p.accent, 0.999999f);

    const bool use_internal_envelope = modulations.trigger_patched;

    p.harmonics = patch.harmonics;
    CONSTRAIN (p.harmonics, 0.0f, 1.0f);

    float internal_envelope_amplitude = 1.0f;
    float internal_envelope_amplitude_timbre = 1.0f;
    if (engine_index == 15)
    {
        internal_envelope_amplitude = 2.0f - p.harmonics * 6.0f;
        CONSTRAIN (internal_envelope_amplitude, 0.0f, 1.0f);
        speech_engine_.set_prosody_amount (! modulations.trigger_patched ? 0.0f : patch.frequency_modulation_amount);
        speech_engine_.set_speed (! modulations.trigger_patched ? 0.0f : patch.morph_modulation_amount);
    }
    else if (engine_index == 7)
    {
        if (modulations.trigger_patched)
        {
            // The chiptune engine's own envelope instead of the internal one on TIMBRE.
            internal_envelope_amplitude_timbre = 0.0f;
            chiptune_engine_.set_envelope_shape (patch.timbre_modulation_amount);
        }
        else
            chiptune_engine_.set_envelope_shape (ChiptuneEngine::NO_ENVELOPE);
    }

    // The attenuverters: depth of the internal envelope when the trigger is patched (FM: a fine tune when not).
    auto modulate = [] (float base, float amount, bool envelope_on, float envelope, float idle, float low, float high) {
        amount *= std::max (std::fabs (amount) - 0.05f, 0.05f);
        amount *= 1.05f;
        float value = base + amount * (envelope_on ? envelope : idle);
        CONSTRAIN (value, low, high);
        return value;
    };
    const float env = decay_envelope_.value();
    p.note = modulate (patch.note + note, patch.frequency_modulation_amount, use_internal_envelope,
                       internal_envelope_amplitude * env * env * 48.0f, 1.0f, -119.0f, 120.0f);
    p.timbre = modulate (patch.timbre, patch.timbre_modulation_amount, use_internal_envelope,
                         internal_envelope_amplitude_timbre * env, 0.0f, 0.0f, 1.0f);
    p.morph = modulate (patch.morph, patch.morph_modulation_amount, use_internal_envelope, internal_envelope_amplitude * env,
                        0.0f, 0.0f, 1.0f);

    bool already_enveloped = pp_s.already_enveloped;
    e->Render (p, out, aux, size, &already_enveloped);

    const bool lpg_bypass = already_enveloped || (! modulations.level_patched && ! modulations.trigger_patched);
    if (! lpg_bypass)
    {
        const float hf = patch.lpg_colour;
        const float decay_tail = (20.0f * kBlockSize) / kSampleRate * SemitonesToRatio (-72.0f * patch.decay + 12.0f * hf) - short_decay;
        if (modulations.level_patched)
            lpg_envelope_.ProcessLP (compressed_level, short_decay, decay_tail, hf);
        else
        {
            const float attack = NoteToFrequency (p.note) * float (kBlockSize) * 2.0f;
            lpg_envelope_.ProcessPing (attack, short_decay, decay_tail, hf);
        }
    }
    else
        lpg_envelope_.Init();
    lpg_bypassed_ = lpg_bypass;

    out_post_processor_.Process (pp_s.out_gain, lpg_bypass, lpg_envelope_.gain(), lpg_envelope_.frequency(), lpg_envelope_.hf_bleed(), out, size);
    aux_post_processor_.Process (pp_s.aux_gain, lpg_bypass, lpg_envelope_.gain(), lpg_envelope_.frequency(), lpg_envelope_.hf_bleed(), aux, size);
}
} // namespace plaits
