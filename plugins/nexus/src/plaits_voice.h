// Copyright 2016 Emilie Gillet (plaits/dsp/voice.h, voice.cc), adapted 2026 for Nexus by hdslave.
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
//
// -----------------------------------------------------------------------------
//
// Plaits' synthesis voice (the 24 engines, the internal decay envelope, the low-pass gate and the output stage), as
// the module's firmware runs it, with three changes for a plug-in: the output is float (the module writes 16-bit
// words for its DAC; here each output is held to the same full scale, +-1), the user data slots of the module's
// flash are not read (the 6-op engines load their factory banks), and the two outputs are written to separate
// buffers. Everything that shapes the sound is the firmware's.
#pragma once

#include "stmlib/stmlib.h"

#include "stmlib/dsp/filter.h"
#include "stmlib/dsp/hysteresis_quantizer.h"
#include "stmlib/dsp/limiter.h"
#include "stmlib/utils/buffer_allocator.h"

#include "plaits/dsp/engine/additive_engine.h"
#include "plaits/dsp/engine/bass_drum_engine.h"
#include "plaits/dsp/engine/chord_engine.h"
#include "plaits/dsp/engine/engine.h"
#include "plaits/dsp/engine/fm_engine.h"
#include "plaits/dsp/engine/grain_engine.h"
#include "plaits/dsp/engine/hi_hat_engine.h"
#include "plaits/dsp/engine/modal_engine.h"
#include "plaits/dsp/engine/noise_engine.h"
#include "plaits/dsp/engine/particle_engine.h"
#include "plaits/dsp/engine/snare_drum_engine.h"
#include "plaits/dsp/engine/speech_engine.h"
#include "plaits/dsp/engine/string_engine.h"
#include "plaits/dsp/engine/swarm_engine.h"
#include "plaits/dsp/engine/virtual_analog_engine.h"
#include "plaits/dsp/engine/waveshaping_engine.h"
#include "plaits/dsp/engine/wavetable_engine.h"

#include "plaits/dsp/engine2/chiptune_engine.h"
#include "plaits/dsp/engine2/phase_distortion_engine.h"
#include "plaits/dsp/engine2/six_op_engine.h"
#include "plaits/dsp/engine2/string_machine_engine.h"
#include "plaits/dsp/engine2/virtual_analog_vcf_engine.h"
#include "plaits/dsp/engine2/wave_terrain_engine.h"

#include "plaits/dsp/envelope.h"
#include "plaits/dsp/fx/low_pass_gate.h"

namespace plaits
{
const int kNexusEngines = 24;
const int kNexusTriggerDelay = 5;
// What one voice's engines share (the module gives them 16 KB of its RAM).
const size_t kNexusVoiceRam = 16384;

struct NexusPatch
{
    float note;
    float harmonics;
    float timbre;
    float morph;
    float frequency_modulation_amount;
    float timbre_modulation_amount;
    float morph_modulation_amount;
    int engine;
    float decay;
    float lpg_colour;
};

struct NexusModulations
{
    float note;
    float trigger;
    float level;
    bool trigger_patched;
    bool level_patched;
};

// The output stage of one channel: the limiter some engines ask for, then the low-pass gate (or its bypass).
class NexusPostProcessor
{
public:
    void Init()
    {
        lpg_.Init();
        Reset();
    }
    void Reset() { limiter_.Init(); }
    void Process (float gain, bool bypass_lpg, float lpg_gain, float lpg_frequency, float lpg_hf_bleed, float* in_out, size_t size)
    {
        if (gain < 0.0f)
            limiter_.Process (-gain, in_out, size);
        const float post_gain = gain < 0.0f ? 1.0f : gain;
        if (! bypass_lpg)
            lpg_.Process (post_gain * lpg_gain, lpg_frequency, lpg_hf_bleed, in_out, size);
        else
            for (size_t i = 0; i < size; ++i)
                in_out[i] *= post_gain;
        // The DAC's full scale (the firmware's Clip16).
        for (size_t i = 0; i < size; ++i)
            in_out[i] = in_out[i] > 1.0f ? 1.0f : (in_out[i] < -1.0f ? -1.0f : in_out[i]);
    }

private:
    stmlib::Limiter limiter_;
    LowPassGate lpg_;
};

class NexusVoice
{
public:
    NexusVoice() {}
    void Init (stmlib::BufferAllocator* allocator);
    // Renders `size` (at most kMaxBlockSize) samples of both outputs.
    void Render (const NexusPatch& patch, const NexusModulations& modulations, float* out, float* aux, size_t size);
    int active_engine() const { return previous_engine_index_; }
    // How open the low-pass gate is now (1 when it is bypassed).
    float lpg_gain() const { return lpg_bypassed_ ? 1.0f : lpg_envelope_.gain(); }
    // Back to silence: the gate shut, the trigger low, the engine reset at the next render.
    void Silence();

private:
    VirtualAnalogEngine virtual_analog_engine_;
    WaveshapingEngine waveshaping_engine_;
    FMEngine fm_engine_;
    GrainEngine grain_engine_;
    AdditiveEngine additive_engine_;
    WavetableEngine wavetable_engine_;
    ChordEngine chord_engine_;
    SpeechEngine speech_engine_;
    SwarmEngine swarm_engine_;
    NoiseEngine noise_engine_;
    ParticleEngine particle_engine_;
    StringEngine string_engine_;
    ModalEngine modal_engine_;
    BassDrumEngine bass_drum_engine_;
    SnareDrumEngine snare_drum_engine_;
    HiHatEngine hi_hat_engine_;
    VirtualAnalogVCFEngine virtual_analog_vcf_engine_;
    PhaseDistortionEngine phase_distortion_engine_;
    SixOpEngine six_op_engine_;
    WaveTerrainEngine wave_terrain_engine_;
    StringMachineEngine string_machine_engine_;
    ChiptuneEngine chiptune_engine_;

    EngineRegistry<kNexusEngines> engines_;
    int previous_engine_index_ = -1;
    float previous_note_ = 0.0f;
    bool trigger_state_ = false;
    bool lpg_bypassed_ = true;
    bool fresh_ = true; // from silence: the first block's pitch is its own (no average with the last note)
    DecayEnvelope decay_envelope_;
    LPGEnvelope lpg_envelope_;
    float trigger_delay_line_[8];
    int trigger_write_ = 0;
    NexusPostProcessor out_post_processor_;
    NexusPostProcessor aux_post_processor_;
};
} // namespace plaits
