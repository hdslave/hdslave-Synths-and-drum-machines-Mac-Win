// hdslave Machine's sound: sixteen voices, each one of the eighteen machines followed by the shared voice section
// (the filter with its envelope, drive, the amp envelope), a free-running LFO, and KIT's eight slots. It runs on the
// audio thread, a block at a time, in sub-blocks of 32 frames for its slower changes; nothing there allocates or
// locks (prepare(), which sizes PLUCK's lines, runs when the plug-in is activated).
#pragma once

#include "dsp.h"
#include "params.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

namespace machine
{
// FM TONE's algorithms. Operators: C 0, A 1, B1 2, B2 3. mods[t]: the operators (bits) that modulate t; heard: the
// operators that are heard. A modulator always has a higher number than what it modulates, so they are worked out
// B2, B1, A, C.
struct Algorithm
{
    uint8_t mods[4];
    uint8_t heard;
};
inline constexpr Algorithm algorithms[8] = {
    { { 1 << 1, 1 << 2, 1 << 3, 0 }, 1 << 0 },                       // 1: B2 > B1 > A > C
    { { 1 << 1, (1 << 2) | (1 << 3), 0, 0 }, 1 << 0 },               // 2: (B1 + B2) > A > C
    { { (1 << 1) | (1 << 2), 0, 1 << 3, 0 }, 1 << 0 },               // 3: B2 > B1 > C, A > C
    { { 1 << 1, 0, 1 << 3, 0 }, (1 << 0) | (1 << 2) },               // 4: A > C, B2 > B1; C and B1 heard
    { { 1 << 3, 1 << 3, 1 << 3, 0 }, (1 << 0) | (1 << 1) | (1 << 2) }, // 5: B2 into the other three, all three heard
    { { 0, 0, 1 << 3, 0 }, (1 << 0) | (1 << 1) | (1 << 2) },          // 6: B2 > B1; C, A and B1 heard
    { { (1 << 1) | (1 << 2) | (1 << 3), 0, 0, 0 }, 1 << 0 },          // 7: (A + B1 + B2) > C
    { { 0, 0, 0, 0 }, 0x0f },                                        // 8: none, all four heard
};

constexpr uint32_t voiceCount = 16;  // voices sounding and fading together (eight held, eight more fading out)
constexpr uint32_t polyphony = 8;    // a machine on the note (FM TONE, the second barrage): notes at once
constexpr uint32_t sub = 32;         // frames between the slower changes
constexpr float silent = 1e-4f;      // -80 dB: an envelope this low has ended
constexpr float fmIndexTurns = 1.5f; // FM TONE: A LEV / B LEV at full, a turn and a half of phase
constexpr float chokeSeconds = 0.003f;

// One sound's Data as the machines use it, worked out each sub-block (KIT: one per slot).
struct Patch
{
    uint8_t kind = fmTone;
    uint8_t valuesFor = 255;  // the machine v[] was worked out for (255: none yet)
    float knob[dataCount] {}; // 0..1, after the LFO
    float v[dataCount] {};    // in its units (dataValue), worked out again only when its knob moves
    float level = 1.0f, panL = 1.0f, panR = 1.0f;
};

struct Voice
{
    bool on = false;
    uint8_t kind = fmTone;
    int8_t slot = -1;      // KIT's slot, -1 the main machine
    int key = -1;
    float velocity = 1.0f;
    float noteHz = 261.63f;
    uint64_t order = 0;    // when it started (stealing takes the oldest)
    bool released = false; // its key was let go
    bool choking = false;
    float choke = 1.0f, chokeStep = 0.0f;
    float meter = 0.0f;
    bool fresh = true;        // started since the last sub-block began: its filter is set at once
    bool prime = false;       // its filter back from bypass: started from the signal, not stale values
    bool owns = false;        // choked: it plays out its fade from its own copy of its sound (`own`), never from
    Patch own;                // what its sound became since (a new machine's Data read as its own would blow up)
    float gainFrom = 0.0f, gainTo = 0.0f, panLFrom = 1.0f, panLTo = 1.0f, panRFrom = 1.0f, panRTo = 1.0f; // per sub-block, ramped
    bool filterBypass = true; // the filter this sub-block: set at the sub-block's start, the same whatever the blocks
    bool bypassFade = false;  // the sub-block the filter goes into or out of bypass: across it, not a jump
    float tail = 0.0f;        // the filter's loudest output this sub-block: its ringing outlasts the machine
    uint32_t fadeIn = 0, fadeInLen = 0; // TERRAIN and PHASE: the samples of their first millisecond's fade still to go

    // The shared section's state.
    enum : uint8_t { attack, hold, decay, sustain, release, ended };
    uint8_t ampStage = attack;
    float amp = 0.0f;
    uint32_t holdLeft = 0;
    uint8_t filterStage = attack;
    float filterEnv = 0.0f;
    dsp::Svf filter;

    // The machine's state (each uses what it needs).
    uint32_t ph[6] {};
    float e[4] {};       // envelopes
    float st[8] {};      // the newer machines' odd states (filters, counters, held values)
    float dcX = 0.0f, dcY = 0.0f; // a DC blocker's (VOX, PHASE, TERRAIN, GENDY, PULSAR)
    // GENDY: its breakpoints (level, time weight), the segment being drawn.
    float pointAmp[16] {}, pointDur[16] {};
    int segment = 0;
    uint32_t cycles = 0;
    float segPos = 0.0f, segLen = 1.0f;
    // MODAL: each mode's two last outputs and its coefficients.
    float my1[12] {}, my2[12] {}, mb1[12] {}, mb2[12] {}, mg[12] {};
    float modalFor[8] {}; // pitch and knob values used for the cached coefficients
    float modalRatio[12] {}, modalR[12] {}, modalG[12] {}, modalHz = 0.0f; // the parts that do not move with the pitch
    // PLUCK: where it writes in its line; PULSAR: the pulse count.
    uint32_t write = 0;
    uint32_t pulses = 0;
    uint8_t fmStage[2] {};
    uint32_t fmInc[4] {};         // FM TONE's operators' steps this sub-block
    float fmGx = 1.0f, fmGy = 0.0f; // and MIX's two gains
    float y1 = 0.0f, y2 = 0.0f;
    float lp = 0.0f;
    uint32_t t = 0, nextBurst = 0;
    int burst = 0;
    dsp::Noise noise;
    dsp::Svf aux;
};

class Engine
{
public:
    Engine()
    {
        for (uint32_t i = 0; i < params.count; ++i)
            v[params.rows[i].id] = params.rows[i].def;
        prepare (48000.0, 0); // ready before activation; activate() prepares it again at the host's rate
    }

    void prepare (double sampleRate, uint32_t)
    {
        rate = (float) sampleRate;
        invRate = 1.0f / rate;
        dcCoef = 1.0f - 2.0f * dsp::pi * 10.0f / rate;
        modalNoiseCoef = std::exp (-1.0f / (0.015f * rate));
        modalNoiseRate = std::sqrt (48000.0f / rate); // white noise into the modes: as loud at any rate as at 48 kHz
        pluckTensionCoef = std::exp (-1.0f / (0.03f * rate));
        modalStrikeRef = 0.0f;
        const float pos = 0.05f + 0.9f * dataSpec (modal, 4).def, tilt = dataSpec (modal, 2).def * 2.0f - 1.5f;
        for (int i = 0; i < 12; ++i) modalStrikeRef += modalStrike (i, pos, tilt);
        lineLength = 1024;
        while ((float) lineLength < rate / 11.5f) lineLength *= 2;
        lineStore.assign ((size_t) lineLength * voiceCount, 0.0f);
        sharedDirty = true;
        reset();
    }

    void reset()
    {
        for (auto& voice : voices)
            voice = Voice {};
        lfoPhase = 0;
        subLeft = 0;
        lfoHeld = 0.0f;
        bendSemis = 0.0f;
        wheel = 0.0f;
        for (auto& m : meters)
            m.store (0.0f, std::memory_order_relaxed);
    }

    // A parameter's value (by id, already clamped). A new machine stops what sounds (its Data mean something else).
    void set (uint32_t id, float value)
    {
        if (id >= ParamId::end) return;
        const float before = v[id];
        v[id] = value;
        if (id == ParamId::machine && (int) before != (int) value)
            allNotesOff();
        if (id >= ParamId::fmAAttack && id <= ParamId::volume) sharedDirty = true;
        uint32_t slot = 0, field = 0;
        if (kitField (id, slot, field) && field == ParamId::kitMachine && (int) before != (int) value)
            for (auto& voice : voices)
                if (voice.on && voice.slot == (int8_t) slot) chokeVoice (voice);
    }
    float sampleRate() const { return rate; }
    uint8_t machineKind() const { return (uint8_t) std::min<int> ((int) v[ParamId::machine], kindCount - 1); }
    uint8_t slotKind (uint32_t slot) const { return slotKinds[std::min<int> ((int) v[kitId (slot & 7, ParamId::kitMachine)], slotKindCount - 1)]; }

    void noteOn (int key, float vel, int32_t noteId = -1)
    {
        if (key < 0 || key > 127) return;
        vel = vel < 0.0f ? 0.0f : vel > 1.0f ? 1.0f : vel;
        const uint8_t kind = machineKind();
        if (kind == kit)
        {
            const int slot = key - kitFirstKey;
            if (slot < 0 || slot >= (int) kitSlots) return;
            const uint8_t slotMachine = slotKind ((uint32_t) slot);
            const bool closedHat = slotMachine == hh && v[kitId ((uint32_t) slot, ParamId::kitData)] < 0.5f;
            for (auto& voice : voices)
            {
                if (! voice.on || voice.choking) continue;
                if (voice.slot == slot) chokeVoice (voice);
                else if (closedHat && voice.kind == hh && v[kitId ((uint32_t) voice.slot, ParamId::kitData)] >= 0.5f) chokeVoice (voice); // open hats
            }
            lastSlot.store ((uint8_t) slot, std::memory_order_relaxed);
            slotHits.fetch_add (1, std::memory_order_relaxed);
            start (pick(), slotMachine, (int8_t) slot, key, vel, noteId);
            return;
        }
        if (drum (kind)) // a drum machine on its own: one hit at a time
        {
            for (auto& voice : voices)
                if (voice.on && ! voice.choking) chokeVoice (voice);
            start (pick(), kind, -1, key, vel, noteId);
            return;
        }
        // The key again while it still sounds (Melodee moves a pending note-off past the key's new note-on, so none
        // comes between): the note it was playing is let go as its note-off would let it go, never left held under
        // the new one (two of a held tone on one pitch would comb, piling up with each repeat).
        noteOff (key);
        uint32_t sounding = 0;
        for (auto& voice : voices)
            sounding += voice.on && ! voice.choking;
        if (sounding >= polyphony) // the oldest released note, else the oldest
        {
            Voice* oldest = nullptr;
            for (auto& voice : voices)
                if (voice.on && ! voice.choking && (oldest == nullptr || (voice.released && ! oldest->released)
                                                    || (voice.released == oldest->released && voice.order < oldest->order)))
                    oldest = &voice;
            if (oldest != nullptr) chokeVoice (*oldest);
        }
        start (pick(), kind, -1, key, vel, noteId);
    }

    // The key's notes let go; with a note id (>= 0) only that note, where its voice has an id of its own.
    void noteOff (int key, int32_t noteId = -1)
    {
        for (auto& voice : voices)
            if (voice.on && voice.key == key && ! voice.released && sameNote (voice, noteId))
            {
                voice.released = true;
                const bool gated = tonal (voice.kind) || (struck (voice.kind) && voice.slot < 0);
                if (gated && voice.ampStage != Voice::ended) voice.ampStage = Voice::release;
            }
    }

    // A choke: the voices of `key` (all of them for -1) gone at once; with a note id, as noteOff.
    void chokeKey (int key, int32_t noteId = -1)
    {
        for (auto& voice : voices)
            if (voice.on && (key < 0 || voice.key == key) && sameNote (voice, noteId)) chokeVoice (voice);
    }

    // Every voice choked (all sound off, a new machine).
    void allNotesOff()
    {
        for (auto& voice : voices)
            if (voice.on) chokeVoice (voice);
    }

    void bend (float semitones) { bendSemis = semitones; }
    void modWheel (float amount) { wheel = amount < 0.0f ? 0.0f : amount > 1.0f ? 1.0f : amount; }

    void render (float* left, float* right, uint32_t frames)
    {
        std::fill (left, left + frames, 0.0f);
        if (right != nullptr) std::fill (right, right + frames, 0.0f);
        // Sub-blocks run on a grid of their own, counted across calls, so the sound is the same however the host cuts
        // its blocks (and wherever its events fall).
        for (uint32_t done = 0; done < frames;)
        {
            const bool boundary = subLeft == 0;
            if (boundary)
            {
                control (sub);
                subLeft = sub;
            }
            const uint32_t n = std::min (subLeft, frames - done);
            const bool subEnd = n == subLeft; // this call finishes the sub-block: voices that have ended stop here
            const uint32_t at = sub - subLeft; // where in the sub-block this call starts
            for (auto& voice : voices)
                if (voice.on) renderVoice (voice, left + done, right != nullptr ? right + done : scratch, n, boundary, subEnd, at);
            if (right == nullptr) std::fill (scratch, scratch + n, 0.0f);
            subLeft -= n;
            done += n;
        }
        publishMeters();
    }

    // The screen's meters: each FM voice, the drum, or each KIT slot (0..1), and the slot the last pad played.
    float meter (uint32_t i) const { return i < 8 ? meters[i].load (std::memory_order_relaxed) : 0.0f; }
    uint8_t slotShown() const { return lastSlot.load (std::memory_order_relaxed); }
    uint32_t kitHits() const { return slotHits.load (std::memory_order_relaxed); } // KIT pads played so far
    uint32_t voicesOn() const
    {
        uint32_t n = 0;
        for (const auto& voice : voices) n += voice.on;
        return n;
    }

private:
    float rate = 48000.0f, invRate = 1.0f / 48000.0f, dcCoef = 0.9987f;
    // MODAL's noise burst's fall and its level against the rate, PLUCK's TENSION glide; the default patch's strike.
    float modalNoiseCoef = 0.0f, modalNoiseRate = 1.0f, pluckTensionCoef = 0.0f, modalStrikeRef = 1.0f;
    float v[ParamId::end] {};
    Voice voices[voiceCount];
    uint64_t started = 0;
    uint32_t lfoPhase = 0, subLeft = 0;
    float lfoHeld = 0.0f, lfoNow = 0.0f;
    uint32_t dataDestNow = 99;
    float dataOffsetNow = 0.0f;
    dsp::Noise lfoNoise;
    float bendSemis = 0.0f, wheel = 0.0f;
    float scratch[sub] {};
    // PLUCK's strings, one a voice, long enough for about 12 Hz at the rate in use (a power of two, made when the
    // plug-in is activated, never on the audio thread).
    std::vector<float> lineStore;
    uint32_t lineLength = 4096;
    std::atomic<float> meters[8] {};
    std::atomic<uint8_t> lastSlot { 0 };
    std::atomic<uint32_t> slotHits { 0 };

    // This sub-block's shared values.
    Patch main, slots[kitSlots];
    float pitchRatio = 1.0f, lfoAmp = 1.0f, volumeNow = 1.0f;
    float cutoffOct = 0.0f, reso = 0.0f, envOct = 0.0f;
    uint8_t filterKind = 0;
    float filterAttackStep = 1.0f, filterDecayCoef = 0.0f;
    float ampAttackStep = 1.0f, ampDecayCoef = 0.0f, ampReleaseCoef = 0.0f;
    uint32_t ampHoldFrames = 0;
    bool ampInf = true;
    float driveGain = 1.0f, driveMakeup = 1.0f;
    bool driving = false;
    float fmAtk[2] {}, fmDec[2] {}, fmEnd[2] {}, fmLev[2] {};
    // The shared section's coefficients worked out again only when one of its parameters (or the rate) changed.
    bool sharedDirty = true;
    float cutoffBase = 0.0f, volumeBase = 1.0f;
    // Each voice's note id from the host (-1: none given), set as it starts. Kept here rather than in Voice, whose
    // layout (and the render loop's code over it) stays as it was.
    int32_t noteIds[voiceCount] {};

    Voice& pick()
    {
        for (auto& voice : voices)
            if (! voice.on) return voice;
        Voice* pick = &voices[0]; // all busy: the quietest fading one, else the oldest
        for (auto& voice : voices)
        {
            if (voice.choking && ! pick->choking) pick = &voice;
            else if (voice.choking && pick->choking ? voice.choke * voice.amp < pick->choke * pick->amp
                                                    : voice.choking == pick->choking && voice.order < pick->order)
                pick = &voice;
        }
        return *pick;
    }

    // CLAP's matching: an id of -1 (Melodee sends no other) is any note; a voice started without one answers to any.
    bool sameNote (const Voice& voice, int32_t noteId) const
    {
        const int32_t own = noteIds[&voice - voices];
        return noteId < 0 || own < 0 || own == noteId;
    }

    void chokeVoice (Voice& voice)
    {
        if (voice.choking) return;
        const Patch& patch = voice.slot >= 0 ? slots[voice.slot] : main;
        if (! voice.owns && patch.kind == voice.kind) voice.own = patch, voice.owns = true;
        voice.choking = true;
        voice.chokeStep = voice.choke / std::max (1.0f, chokeSeconds * rate);
    }

    void start (Voice& voice, uint8_t kind, int8_t slot, int key, float vel, int32_t noteId)
    {
        const uint32_t seed = voice.noise.s ^ (uint32_t) (started * 2654435761u) ^ (uint32_t) key;
        voice = Voice {};
        voice.on = true;
        voice.kind = kind;
        voice.slot = slot;
        voice.key = key;
        noteIds[&voice - voices] = noteId;
        voice.velocity = vel;
        voice.noteHz = 440.0f * std::exp2 (((float) (slot >= 0 ? slotBaseKey : key) - 69.0f) / 12.0f); // a KIT slot at its own pitch
        voice.order = ++started;
        voice.noise.seed (seed * 1664525u + 1013904223u);
        voice.e[0] = voice.e[1] = voice.e[2] = voice.e[3] = 1.0f;
        voice.ampStage = Voice::attack;
        voice.amp = 0.0f;
        voice.filterStage = Voice::attack;
        if (kind == fmTone) voice.e[0] = voice.e[1] = 0.0f, voice.fmStage[0] = voice.fmStage[1] = 0;
        if (kind == pulsar) voice.st[0] = 0.0f; // the first pulse at once
        if (kind == pluck) voice.write = 0, voice.st[0] = -1.0f; // its line is filled on its first sub-block
        if (kind == cp) voice.burst = 1, voice.nextBurst = 0, voice.e[1] = 0.0f;
        if (kind == gnd) voice.lp = 0.0f;
        // Its Data as they are now, not as the sub-block began: a value set at the note's own frame (a step's
        // automation) is what it strikes with (KICK's ACCENT, GENDY's POINTS, PLUCK's pick, MODAL's mallet), and a new
        // machine's Data are its own. Knobs that have not moved keep their values (fillPatch works out only the rest).
        Patch& patch = slot >= 0 ? slots[slot] : main;
        fillPatch (patch, kind, slot, dataDestNow, dataOffsetNow);
        if (slot >= 0) fillSlotMix ((uint32_t) slot);
        if (kind == gendy) // a sine over its POINTS to start from, so the first cycles are a tone with no offset
        {
            const int points = std::min (16, std::max (3, (int) patch.v[0]));
            for (int i = 0; i < 16; ++i)
            {
                voice.pointAmp[i] = i < points ? 0.7f * dsp::sine ((uint32_t) ((float) i * (dsp::cycle / (float) points))) : 0.0f;
                voice.pointDur[i] = 1.0f;
            }
        }
        if (kind == terrain || kind == phase) // they start mid-wave: a millisecond's fade in, not a click
            voice.fadeIn = voice.fadeInLen = std::max<uint32_t> (1, (uint32_t) (0.001f * rate));
        if (kind == kick)
        {
            // ACCENT: how far the velocity reaches (none: every hit the same), and its push into DRIVE.
            voice.st[6] = vel;
            voice.velocity = 1.0f - patch.knob[9] * (1.0f - vel);
            voice.st[7] = slot >= 0 ? 1.0f : std::min (8.0f, std::max (0.125f, std::exp2 ((float) (key - kickBaseKey) / 12.0f)));
            voice.e[1] = 0.0f; // the amp from its attack
        }
    }

    void fillSlotMix (uint32_t s)
    {
        slots[s].level = v[kitId (s, ParamId::kitLevel)] * 1.25f;
        const float pan = v[kitId (s, ParamId::kitPan)];
        slots[s].panL = std::min (1.0f, 1.0f - pan);
        slots[s].panR = std::min (1.0f, 1.0f + pan);
    }

    // --- The slower changes ------------------------------------------------------------------------------------
    float lfoValue()
    {
        const uint32_t wave = (uint32_t) v[ParamId::lfoWave];
        const float p = (float) lfoPhase * (1.0f / dsp::cycle);
        switch (wave)
        {
            case 0: return p < 0.5f ? 4.0f * p - 1.0f : 3.0f - 4.0f * p; // triangle
            case 1: return 1.0f - 2.0f * p;                              // saw (down)
            case 2: return p < 0.5f ? 1.0f : -1.0f;                      // square
            case 3: return lfoHeld;                                      // sample and hold
            default: return dsp::sine (lfoPhase);
        }
    }

    void fillPatch (Patch& patch, uint8_t kind, int slot, uint32_t dataDest, float offset)
    {
        const bool known = patch.valuesFor == kind;
        patch.kind = kind;
        patch.valuesFor = kind;
        for (uint32_t k = 0; k < dataCount; ++k)
        {
            float x = v[slot >= 0 ? kitDataId ((uint32_t) slot, k) : dataId (k)];
            if (k == dataDest) x = std::min (1.0f, std::max (0.0f, x + offset));
            if (known && x == patch.knob[k]) continue; // unchanged: its value (pow, exp, log) as it was
            patch.knob[k] = x;
            patch.v[k] = dataValue (dataSpec (kind, k), x);
        }
    }

    void control (uint32_t frames)
    {
        using namespace ParamId;
        // The LFO, free-running.
        const uint32_t before = lfoPhase;
        lfoPhase += dsp::step (lfoHz (v[lfoSpeed]), invRate) * frames;
        if (lfoPhase < before) lfoHeld = lfoNoise.next(); // a new step each turn
        lfoNow = lfoValue();
        float depth = v[lfoDepth];
        depth += (depth < 0.0f ? -wheel : wheel); // the mod wheel deepens it
        depth = std::min (1.0f, std::max (-1.0f, depth));
        const float l = depth * lfoNow;
        const uint32_t dest = (uint32_t) v[lfoDest];
        pitchRatio = std::exp2 ((bendSemis + (dest == 0 ? 12.0f * l : 0.0f)) / 12.0f);
        lfoAmp = dest == 2 ? 1.0f - std::fabs (depth) * 0.5f * (1.0f - (depth < 0.0f ? -lfoNow : lfoNow)) : 1.0f;
        const uint32_t dataDest = dest >= 3 ? dest - 3 : 99;
        const float dataOffset = 0.5f * l;
        dataDestNow = dataDest;
        dataOffsetNow = dataOffset;

        // The patches: the main machine's Data, or each slot's.
        const uint8_t kind = machineKind();
        if (kind == kit)
        {
            for (uint32_t s = 0; s < kitSlots; ++s)
            {
                bool used = false;
                for (const auto& voice : voices) used |= voice.on && voice.slot == (int8_t) s;
                if (! used) continue;
                fillPatch (slots[s], slotKind (s), (int) s, dataDest, dataOffset);
                fillSlotMix (s);
            }
        }
        else
            fillPatch (main, kind, -1, dataDest, dataOffset);

        // The shared section: its coefficients when its parameters moved, the LFO's part each sub-block.
        if (sharedDirty)
        {
            sharedDirty = false;
            cutoffBase = std::log2 (filterHz (v[filterFreq]));
            reso = v[filterReso];
            envOct = v[filterEnv] * filterEnvOctaves;
            filterKind = (uint8_t) v[filterType];
            const float fa = timeSeconds (v[filterAttack]);
            filterAttackStep = fa > 0.0f ? 1.0f / (fa * rate) : 1.0f;
            filterDecayCoef = dsp::decayCoef (std::max (0.001f, timeSeconds (v[filterDecay])), rate);
            const float aa = timeSeconds (v[ampAttack]);
            ampAttackStep = aa > 0.0f ? 1.0f / (aa * rate) : 1.0f;
            ampHoldFrames = (uint32_t) (timeSeconds (v[ampHold]) * rate);
            ampInf = v[ampDecay] >= infAbove;
            ampDecayCoef = dsp::decayCoef (std::max (0.001f, timeSeconds (v[ampDecay])), rate);
            ampReleaseCoef = dsp::decayCoef (std::max (0.001f, timeSeconds (v[ampRelease])), rate);
            const float drive = v[ampDrive];
            driving = drive > 0.0f;
            driveGain = 1.0f + 12.0f * drive * drive;
            driveMakeup = 1.0f / (1.0f + 1.5f * drive);
            volumeBase = volumeGain (v[volume]);
            for (uint32_t k = 0; k < 2; ++k)
            {
                const uint32_t base = k == 0 ? fmAAttack : fmBAttack;
                const float atk = timeSeconds (v[base]);
                fmAtk[k] = atk > 0.0f ? 1.0f / (atk * rate) : 1.0f;
                fmDec[k] = dsp::decayCoef (std::max (0.001f, timeSeconds (v[base + 1])), rate);
                fmEnd[k] = v[base + 2];
                fmLev[k] = v[base + 3] * v[base + 3] * fmIndexTurns;
            }
        }
        cutoffOct = cutoffBase + (dest == 1 ? 4.0f * l : 0.0f);
        volumeNow = volumeBase * lfoAmp;
    }

    // --- A voice ----------------------------------------------------------------------------------------------
    void renderVoice (Voice& voice, float* left, float* right, uint32_t n, bool boundary, bool subEnd, uint32_t at)
    {
        const Patch& patch = voice.owns ? voice.own : voice.slot >= 0 ? slots[voice.slot] : main;
        const bool slow = boundary || voice.fresh; // where the slower changes happen: the grid, or the voice's start
        float buf[sub];
        bool machineDone = false;
        if (patch.kind != voice.kind) // its sound is another machine's now (never expected): silent, and gone
        {
            if (subEnd) voice.on = false;
            return;
        }
        switch (voice.kind)
        {
            case fmTone: renderFm (voice, patch, buf, n, slow); break;
            case bd: machineDone = renderBd (voice, patch, buf, n); break;
            case sd: machineDone = renderSd (voice, patch, buf, n); break;
            case cp: machineDone = renderCp (voice, patch, buf, n); break;
            case hh: machineDone = renderHh (voice, patch, buf, n); break;
            case cycb: machineDone = renderCyCb (voice, patch, buf, n); break;
            case efm: machineDone = renderEfm (voice, patch, buf, n); break;
            case gnd: machineDone = renderGnd (voice, patch, buf, n); break;
            case terrain: renderTerrain (voice, patch, buf, n, slow); break;
            case phase: renderPhase (voice, patch, buf, n); break;
            case vox: machineDone = renderVox (voice, patch, buf, n); break;
            case modal: machineDone = renderModal (voice, patch, buf, n, slow); break;
            case pluck: machineDone = renderPluck (voice, patch, buf, n, slow); break;
            case gendy: renderGendy (voice, patch, buf, n); break;
            case pulsar: machineDone = renderPulsar (voice, patch, buf, n); break;
            case kick: machineDone = renderKick (voice, patch, buf, n); break;
            default: renderOrgan (voice, patch, buf, n); break;
        }
        for (uint32_t i = 0; i < n && voice.fadeIn > 0; ++i, --voice.fadeIn)
            buf[i] *= (float) (voice.fadeInLen - voice.fadeIn + 1) / (float) voice.fadeInLen;

        // The filter, its cutoff set from the envelope where this sub-block starts (or where the voice did).
        if (boundary || voice.fresh)
        {
            // A voice's first sub-block takes the envelope where its first sample does (ATTACK 0: open at once), so a
            // hit sounds the same wherever it lands on the grid.
            const float env = voice.fresh ? std::min (1.0f, voice.filterEnv + filterAttackStep) : voice.filterEnv;
            const float cutoff = std::exp2 (cutoffOct + envOct * env);
            const bool wasBypassed = voice.filterBypass && ! voice.fresh, wasFiltered = ! voice.filterBypass && ! voice.fresh;
            // Fully open (at the top of hearing, whatever the rate) and flat: no filter at all.
            voice.filterBypass = filterKind == 0 && cutoff >= std::min (0.42f * rate, 19500.0f) && reso < 0.01f;
            if (! voice.filterBypass) voice.filter.set (cutoff, reso, rate);
            if (wasBypassed && ! voice.filterBypass) voice.prime = true;
            voice.bypassFade = (wasFiltered && voice.filterBypass) || (wasBypassed && ! voice.filterBypass);
            voice.tail = 0.0f;
            // The gain and pan this sub-block ends on, ramped to from where the last one ended (a voice starting
            // mid-sub-block starts on them).
            const float gain = voice.velocity * volumeNow * (voice.slot >= 0 ? patch.level : 1.0f);
            const float panL = voice.slot >= 0 ? patch.panL : 1.0f, panR = voice.slot >= 0 ? patch.panR : 1.0f;
            if (voice.fresh) voice.gainFrom = gain, voice.panLFrom = panL, voice.panRFrom = panR;
            else voice.gainFrom = voice.gainTo, voice.panLFrom = voice.panLTo, voice.panRFrom = voice.panRTo;
            voice.gainTo = gain, voice.panLTo = panL, voice.panRTo = panR;
            voice.fresh = false;
        }
        const bool bypass = voice.filterBypass, fading = voice.bypassFade;
        const bool ramping = voice.gainFrom != voice.gainTo || voice.panLFrom != voice.panLTo || voice.panRFrom != voice.panRTo;
        const float gainStep = (voice.gainTo - voice.gainFrom) * (1.0f / (float) sub);
        const float panLStep = (voice.panLTo - voice.panLFrom) * (1.0f / (float) sub), panRStep = (voice.panRTo - voice.panRFrom) * (1.0f / (float) sub);
        float gain = voice.gainTo, panL = voice.panLTo, panR = voice.panRTo;
        float peak = 0.0f;
        // The loop's state and coefficients in locals: the stores to left/right could otherwise alias them, and
        // every one would be loaded and stored again each sample.
        float filterEnvL = voice.filterEnv, ampL = voice.amp, chokeL = voice.choke;
        uint8_t filterStageL = voice.filterStage, ampStageL = voice.ampStage;
        uint32_t holdLeftL = voice.holdLeft;
        bool primeL = voice.prime;
        dsp::Svf f = voice.filter;
        float tailL = voice.tail;
        const bool choking = voice.choking;
        const float chokeStep = voice.chokeStep, gainFrom = voice.gainFrom, panLFrom = voice.panLFrom, panRFrom = voice.panRFrom;
        const float fAtk = filterAttackStep, fDec = filterDecayCoef, aAtk = ampAttackStep, aDec = ampDecayCoef, aRel = ampReleaseCoef;
        const float dGain = driveGain, dMakeup = driveMakeup;
        const uint32_t holdFrames = ampHoldFrames;
        const bool inf = ampInf, drv = driving;
        const uint8_t fKind = filterKind;
        for (uint32_t i = 0; i < n; ++i)
        {
            // The filter envelope: attack, then decay.
            if (filterStageL == Voice::attack)
            {
                filterEnvL += fAtk;
                if (filterEnvL >= 1.0f) filterEnvL = 1.0f, filterStageL = Voice::decay;
            }
            else
                filterEnvL *= fDec;

            float x = buf[i];
            if (primeL) // back from bypass: the filter starts where the signal is
            {
                f.ic1 = 0.0f;
                f.ic2 = x;
                primeL = false;
            }
            if (ramping)
            {
                const float t = (float) (at + i + 1);
                gain = gainFrom + gainStep * t;
                panL = panLFrom + panLStep * t;
                panR = panRFrom + panRStep * t;
            }
            if (! bypass || fading)
            {
                float lo, bp, hi;
                f.run (x, lo, bp, hi);
                const float filtered = fKind == 0 ? lo : fKind == 1 ? hi : bp;
                if (fading) // into bypass (filtered to the signal) or out of it (the other way) across the sub-block
                {
                    const float t = (float) (at + i + 1) * (1.0f / (float) sub);
                    x = bypass ? filtered + (x - filtered) * t : x + (filtered - x) * t;
                }
                else
                    x = filtered;
                if (! bypass) tailL = std::max (tailL, std::fabs (x));
            }
            if (drv) x = dsp::softClip (x * dGain) * dMakeup;

            // The amp envelope.
            switch (ampStageL)
            {
                case Voice::attack:
                    ampL += aAtk;
                    if (ampL >= 1.0f)
                    {
                        ampL = 1.0f;
                        ampStageL = Voice::hold;
                        holdLeftL = holdFrames;
                    }
                    break;
                case Voice::hold:
                    if (holdLeftL > 0) --holdLeftL;
                    else ampStageL = inf ? Voice::sustain : Voice::decay;
                    break;
                case Voice::decay:
                    if (inf) { ampStageL = Voice::sustain; break; }
                    ampL *= aDec;
                    if (ampL < silent) ampStageL = Voice::ended;
                    break;
                case Voice::sustain:
                    if (! inf) ampStageL = Voice::decay;
                    break;
                case Voice::release:
                    ampL *= aRel;
                    if (ampL < silent) ampStageL = Voice::ended;
                    break;
                default: ampL = 0.0f; break;
            }
            float g = ampL * gain;
            if (choking)
            {
                chokeL -= chokeStep;
                if (chokeL <= 0.0f) chokeL = 0.0f;
                g *= chokeL;
            }
            const float y = x * g;
            peak = std::max (peak, std::fabs (y));
            left[i] += y * panL;
            right[i] += y * panR;
        }
        voice.filterEnv = filterEnvL, voice.amp = ampL, voice.choke = chokeL;
        voice.filterStage = filterStageL, voice.ampStage = ampStageL, voice.holdLeft = holdLeftL, voice.prime = primeL;
        voice.filter = f;
        voice.tail = tailL;
        voice.meter = std::max (peak, voice.meter * 0.85f);
        // Ended (its machine quiet, its amp closed, or choked away): it stops where the sub-block does, whatever the
        // host's blocks, so the last samples are the same however they are cut.
        // A machine that is done still waits for a resonant filter's ringing to die away.
        const bool quiet = machineDone && (bypass || voice.tail < silent);
        if (subEnd && (quiet || voice.ampStage == Voice::ended || (voice.choking && voice.choke <= 0.0f))) voice.on = false;
        if (! std::isfinite (voice.filter.ic1) || ! std::isfinite (voice.filter.ic2)) voice.filter.reset(); // never stuck on NaN
    }

    void publishMeters()
    {
        float m[8] {};
        const uint8_t kind = machineKind();
        uint32_t fmIndex = 0;
        for (const auto& voice : voices)
        {
            if (! voice.on && voice.meter < 0.001f) continue;
            const float level = voice.on ? std::min (1.0f, voice.meter) : 0.0f;
            if (kind == kit && voice.slot >= 0) m[voice.slot] = std::max (m[voice.slot], level);
            else if (! drum (kind)) { if (voice.on && fmIndex < 8) m[fmIndex++] = level; }
            else m[0] = std::max (m[0], level);
        }
        for (uint32_t i = 0; i < 8; ++i) meters[i].store (m[i], std::memory_order_relaxed);
    }

    float op (uint32_t phase, float shape) const
    {
        const float s = dsp::sine (phase);
        return shape > 0.0f ? dsp::sine (phase + dsp::turns (shape * 0.2f * s)) : s;
    }

    // FM TONE: four sine operators in the algorithm ALGO, the modulators' levels from FM ENV A and B.
    void renderFm (Voice& voice, const Patch& p, float* __restrict out, uint32_t n, bool slow)
    {
        const Algorithm& alg = algorithms[std::min (7, std::max (0, (int) p.v[0] - 1))];
        if (slow) // the operators' steps and MIX's gains: they change with the sub-block, not the sample
        {
            const float detune = p.v[5] * 25.0f / 1200.0f; // DETUNE: up to 25 cents, A up and B down
            const float hz = voice.noteHz * pitchRatio;
            voice.fmInc[0] = dsp::step (hz * p.v[1], invRate);
            voice.fmInc[1] = dsp::step (hz * p.v[2] * std::exp2 (detune), invRate);
            voice.fmInc[2] = dsp::step (hz * p.v[3] * std::exp2 (-detune), invRate);
            voice.fmInc[3] = dsp::step (hz * p.v[3] * std::exp2 (-0.5f * detune), invRate);
            const float mixAngle = p.knob[7] * 0.5f * dsp::pi;
            voice.fmGx = std::cos (mixAngle);
            voice.fmGy = std::sin (mixAngle);
        }
        const uint32_t* inc = voice.fmInc;
        const float harm = p.v[4];
        const float carrierShape = harm > 0.0f ? harm : 0.0f, modShape = harm < 0.0f ? -harm : 0.0f;
        const float feedback = p.knob[6] * p.knob[6] * 0.25f;
        const float gx = voice.fmGx, gy = voice.fmGy;
        const float velIndex = 0.6f + 0.4f * voice.velocity;
        // Bits as factors: which modulators reach each operator, and which are heard.
        auto bit = [] (uint8_t mask, int b) { return (mask >> b) & 1 ? 1.0f : 0.0f; };
        const float m2b2 = bit (alg.mods[2], 3);
        const float m1b1 = bit (alg.mods[1], 2), m1b2 = bit (alg.mods[1], 3);
        const float m0a = bit (alg.mods[0], 1), m0b1 = bit (alg.mods[0], 2), m0b2 = bit (alg.mods[0], 3);
        const float hA = bit (alg.heard, 1), hB1 = bit (alg.heard, 2), hB2 = bit (alg.heard, 3);
        const bool onlyC = (alg.heard & 0x0e) == 0;
        const float heardScale = 1.0f / std::max (1.0f, hA + hB1 + hB2);
        // State and coefficients in locals (the stores to out[] could otherwise alias them).
        float env[2] = { voice.e[0], voice.e[1] };
        uint8_t stage[2] = { voice.fmStage[0], voice.fmStage[1] };
        float y1 = voice.y1, y2 = voice.y2;
        uint32_t ph[4] = { voice.ph[0], voice.ph[1], voice.ph[2], voice.ph[3] };
        const uint32_t incL[4] = { inc[0], inc[1], inc[2], inc[3] };
        const float atk[2] = { fmAtk[0], fmAtk[1] }, end[2] = { fmEnd[0], fmEnd[1] }, dec[2] = { fmDec[0], fmDec[1] }, lev[2] = { fmLev[0], fmLev[1] };
        for (uint32_t i = 0; i < n; ++i)
        {
            for (int k = 0; k < 2; ++k) // FM ENV A and B: rise, then fall to END and stay
            {
                if (stage[k] == 0)
                {
                    env[k] += atk[k];
                    if (env[k] >= 1.0f) env[k] = 1.0f, stage[k] = 1;
                }
                else
                    env[k] = end[k] + (env[k] - end[k]) * dec[k];
            }
            const float iA = env[0] * lev[0] * velIndex, iB = env[1] * lev[1] * velIndex;
            const float o3 = op (ph[3] + dsp::turns (feedback * 0.5f * (y1 + y2)), modShape);
            const float o2 = op (ph[2] + dsp::turns (m2b2 * o3 * iB), modShape);
            const float o1 = op (ph[1] + dsp::turns ((m1b1 * o2 + m1b2 * o3) * iB), modShape);
            const float o0 = op (ph[0] + dsp::turns (m0a * o1 * iA + (m0b1 * o2 + m0b2 * o3) * iB), carrierShape);
            y2 = y1;
            y1 = o3;
            const float x = o0;
            const float y = onlyC ? o1 * env[0] : (hA * o1 * env[0] * lev[0] + (hB1 * o2 + hB2 * o3) * env[1] * lev[1]) * heardScale
                                                       * (1.0f / fmIndexTurns);
            out[i] = 0.45f * (x * gx + y * gy); // a voice at about -7 dB: a three-note chord peaks near +2.5 dBFS
            for (int k = 0; k < 4; ++k) ph[k] += incL[k];
        }
        voice.e[0] = env[0], voice.e[1] = env[1], voice.fmStage[0] = stage[0], voice.fmStage[1] = stage[1];
        voice.y1 = y1, voice.y2 = y2;
        for (int k = 0; k < 4; ++k) voice.ph[k] = ph[k];
    }

    float driveOf (float x, float drive) const { return drive > 0.0f ? dsp::softClip (x * (1.0f + 8.0f * drive)) / (1.0f + 1.5f * drive) : x; }

    // BD: a sine swept down from SWEEP octaves above TUNE, a click, FM (the sine bending itself), noise, drive.
    bool renderBd (Voice& voice, const Patch& p, float* __restrict out, uint32_t n)
    {
        const float f0 = p.v[0] * pitchRatio, sweep = p.v[2];
        const float pc = std::exp (-1.0f / (std::max (0.0005f, p.v[3]) * rate));
        const float ac = dsp::decayCoef (std::max (0.005f, p.v[1]), rate);
        const float cc = std::exp (-1.0f / (0.0012f * rate));
        const float click = p.v[4], fm = p.v[6] * 0.35f, noise = p.v[7], drive = p.v[5];
        const float nlp = dsp::onePole (std::min (8000.0f, f0 * 8.0f), rate);
        float& pe = voice.e[0];
        float& ae = voice.e[1];
        float& ce = voice.e[2];
        for (uint32_t i = 0; i < n; ++i)
        {
            const uint32_t inc = dsp::step (f0 * dsp::exp2Fast (sweep * pe), invRate);
            const float body = dsp::sine (voice.ph[0] + dsp::turns (fm * ae * dsp::sine (voice.ph[0])));
            const float nz = voice.noise.next();
            voice.lp += nlp * (nz - voice.lp);
            const float y = body * ae + ce * click * nz + voice.lp * noise * 2.0f * ae;
            out[i] = driveOf (y, drive);
            voice.ph[0] += inc;
            pe *= pc;
            ae *= ac;
            ce *= cc;
        }
        return ae < silent && ce < silent;
    }

    // SD: two partials with a short pitch drop (BODY, over TONE DEC), and high-passed noise (SNAPPY, over NOIS DEC).
    bool renderSd (Voice& voice, const Patch& p, float* __restrict out, uint32_t n)
    {
        const float f0 = p.v[0] * pitchRatio;
        const float tc = dsp::decayCoef (std::max (0.005f, p.v[1]), rate), nc = dsp::decayCoef (std::max (0.005f, p.v[2]), rate);
        const float pc = std::exp (-1.0f / (0.008f * rate));
        const float snappy = p.v[3], body = p.v[4], drive = p.v[6];
        const float hp = dsp::onePole (p.v[5], rate);
        float& te = voice.e[0];
        float& ne = voice.e[1];
        float& pe = voice.e[2];
        for (uint32_t i = 0; i < n; ++i)
        {
            const float hz = f0 * (1.0f + 0.6f * pe);
            const float b = (dsp::sine (voice.ph[0]) + 0.6f * dsp::sine (voice.ph[1])) * (1.0f / 1.6f);
            voice.ph[0] += dsp::step (hz, invRate);
            voice.ph[1] += dsp::step (hz * 1.47f, invRate);
            const float nz = voice.noise.next();
            voice.lp += hp * (nz - voice.lp);
            const float y = b * body * te + (nz - voice.lp) * snappy * ne;
            out[i] = driveOf (y, drive);
            te *= tc;
            ne *= nc;
            pe *= pc;
        }
        return te < silent && ne < silent;
    }

    // CP: BURSTS of band-passed noise SPREAD apart, the last decaying over DECAY, and ROOM's longer, darker tail.
    bool renderCp (Voice& voice, const Patch& p, float* __restrict out, uint32_t n)
    {
        const int bursts = (int) p.v[0];
        const uint32_t spread = (uint32_t) std::max (1.0f, std::max (0.001f, p.v[1]) * rate);
        const float tail = std::max (0.01f, p.v[2]);
        const float burstCoef = std::exp (-1.0f / (std::max (0.0005f, 0.3f * p.v[1]) * rate));
        const float lastCoef = dsp::decayCoef (tail, rate), roomCoef = dsp::decayCoef (3.0f * tail + 0.2f, rate);
        const float room = p.v[5] * 0.6f;
        voice.aux.set (p.v[3], 0.3f + 0.65f * p.v[4], rate);
        const float roomLp = dsp::onePole (p.v[3] * 0.6f, rate);
        float& env = voice.e[0];
        float& roomEnv = voice.e[1];
        for (uint32_t i = 0; i < n; ++i)
        {
            if (voice.t >= voice.nextBurst && voice.burst <= bursts)
            {
                env = 1.0f;
                if (voice.burst == bursts) roomEnv = 1.0f;
                ++voice.burst;
                voice.nextBurst += spread;
            }
            const bool last = voice.burst > bursts;
            const float nz = voice.noise.next();
            voice.lp += roomLp * (nz - voice.lp);
            out[i] = 1.8f * voice.aux.band (nz) * env + 1.4f * voice.lp * room * roomEnv;
            env *= last ? lastCoef : burstCoef;
            roomEnv *= roomCoef;
            ++voice.t;
        }
        return voice.burst > bursts && env < silent && roomEnv < silent;
    }

    // HH: six square waves between harmonic and metallic ratios (METAL), TONE up or down an octave, and noise, high
    // passed at FILTER; closed short, open long.
    bool renderHh (Voice& voice, const Patch& p, float* __restrict out, uint32_t n)
    {
        static constexpr float metal[6] = { 205.3f, 304.4f, 369.6f, 522.7f, 540.0f, 800.0f };
        static constexpr float plain[6] = { 205.3f, 410.6f, 615.9f, 821.2f, 1026.5f, 1231.8f };
        const bool open = p.v[0] >= 0.5f;
        const float scale = std::exp2 (p.v[1]) * pitchRatio;
        uint32_t inc[6];
        for (int k = 0; k < 6; ++k)
            inc[k] = dsp::step (scale * plain[k] * std::pow (metal[k] / plain[k], p.v[4]), invRate);
        const float decay = open ? 0.08f * std::pow (40.0f, p.knob[2]) : 0.008f * std::pow (40.0f, p.knob[2]);
        const float coef = dsp::decayCoef (decay, rate);
        const float noise = p.v[5];
        voice.aux.set (p.v[3], 0.2f, rate);
        float& env = voice.e[0];
        for (uint32_t i = 0; i < n; ++i)
        {
            float sq = 0.0f;
            for (int k = 0; k < 6; ++k)
            {
                sq += (float) ((int32_t) (voice.ph[k] >> 31) * 2 - 1);
                voice.ph[k] += inc[k];
            }
            const float x = sq * (1.0f / 6.0f) * (1.0f - noise) + voice.noise.next() * noise;
            out[i] = 1.6f * voice.aux.high (x) * env;
            env *= coef;
        }
        return env < silent;
    }

    // CY/CB: a cymbal (six squares, RING their ring-modulated pairs, long, high passed) or a cowbell (two squares
    // through a band pass, RING its sharpness).
    bool renderCyCb (Voice& voice, const Patch& p, float* __restrict out, uint32_t n)
    {
        const bool bell = p.v[0] >= 0.5f;
        const float scale = std::exp2 (p.v[1]) * pitchRatio;
        const float ring = p.v[3];
        float& env = voice.e[0];
        float& ping = voice.e[1];
        if (bell)
        {
            const uint32_t i0 = dsp::step (540.0f * scale, invRate), i1 = dsp::step (800.0f * scale, invRate);
            const float coef = dsp::decayCoef (0.02f * std::pow (50.0f, p.knob[2]), rate);
            const float pc = std::exp (-1.0f / (0.006f * rate));
            voice.aux.set (std::min (p.v[4] * 0.25f, 4000.0f) * scale, 0.5f + 0.45f * ring, rate);
            for (uint32_t i = 0; i < n; ++i)
            {
                const float x = ((voice.ph[0] & 0x80000000u) ? 0.5f : -0.5f) + ((voice.ph[1] & 0x80000000u) ? 0.5f : -0.5f);
                voice.ph[0] += i0;
                voice.ph[1] += i1;
                out[i] = 0.9f * voice.aux.band (x) * env * (0.7f + 0.6f * ping);
                env *= coef;
                ping *= pc;
            }
            return env < silent;
        }
        static constexpr float ratios[6] = { 1.0f, 1.342f, 1.627f, 2.105f, 2.565f, 3.287f };
        uint32_t inc[6];
        for (int k = 0; k < 6; ++k) inc[k] = dsp::step (380.0f * ratios[k] * scale, invRate);
        const float coef = dsp::decayCoef (0.2f * std::pow (30.0f, p.knob[2]), rate);
        voice.aux.set (p.v[4], 0.15f, rate);
        for (uint32_t i = 0; i < n; ++i)
        {
            float s[6];
            for (int k = 0; k < 6; ++k)
            {
                s[k] = (float) ((int32_t) (voice.ph[k] >> 31) * 2 - 1);
                voice.ph[k] += inc[k];
            }
            const float sum = (s[0] + s[1] + s[2] + s[3] + s[4] + s[5]) * (1.0f / 6.0f);
            const float rm = (s[0] * s[1] + s[2] * s[3] + s[4] * s[5]) * (1.0f / 3.0f);
            const float x = sum * (1.0f - ring) + rm * ring + 0.1f * voice.noise.next();
            out[i] = 1.4f * voice.aux.high (x) * env;
            env *= coef;
        }
        return env < silent;
    }

    // EFM DRUM: a carrier at PITCH (swept down by SWEEP over P DECAY) modulated by an operator at RATIO, its INDEX
    // falling over I DECAY, the level over A DECAY; NOISE on top.
    bool renderEfm (Voice& voice, const Patch& p, float* __restrict out, uint32_t n)
    {
        const float f0 = p.v[0] * pitchRatio, ratio = p.v[1], index = p.knob[2] * p.knob[2] * fmIndexTurns, sweep = p.v[6];
        const float pc = std::exp (-1.0f / (std::max (0.0005f, p.v[3]) * rate));
        const float ic = dsp::decayCoef (std::max (0.002f, p.v[4]), rate), ac = dsp::decayCoef (std::max (0.005f, p.v[5]), rate);
        const float noise = p.v[7];
        float& pe = voice.e[0];
        float& ie = voice.e[1];
        float& ae = voice.e[2];
        for (uint32_t i = 0; i < n; ++i)
        {
            const float hz = f0 * dsp::exp2Fast (sweep * pe);
            const float m = dsp::sine (voice.ph[1]) * index * ie;
            out[i] = (dsp::sine (voice.ph[0] + dsp::turns (m)) + noise * voice.noise.next()) * ae;
            voice.ph[0] += dsp::step (hz, invRate);
            voice.ph[1] += dsp::step (hz * ratio, invRate);
            pe *= pc;
            ie *= ic;
            ae *= ac;
        }
        return ae < silent;
    }

    // --- The second barrage -------------------------------------------------------------------------------------
    // A DC blocker (about 10 Hz), for the machines whose waves sit off the middle.
    float dcBlock (Voice& voice, float x) const
    {
        const float y = x - voice.dcX + dcCoef * voice.dcY;
        voice.dcX = x;
        voice.dcY = y;
        return y;
    }
    static float wave (float t) { return dsp::sine (dsp::turns (t)); } // sin (2 pi t)
    // MODAL: a mode's share of the strike, from where it is struck (a node gives nothing) and BRIGHT's tilt.
    static float modalStrike (int i, float pos, float tilt) { return std::fabs (wave (0.5f * (float) (i + 1) * pos)) * std::pow ((float) (i + 1), tilt); }
    static constexpr uint32_t quarter = 0x40000000u;

    // TERRAIN's surfaces: heights over the plane, -1..1, WARP (k) packing their hills closer.
    static float surfaceAt (int surface, float x, float y, float k)
    {
        switch (surface)
        {
            case 0: return wave (k * std::sqrt (x * x + y * y));                // RIPPLE: rings round the middle
            case 1: return wave (0.5f * k * (x * x - y * y));                   // SADDLE: up one way, down the other
            case 2: return wave (0.5f * k * x) * wave (0.5f * k * y + 0.25f);   // LATTICE: an egg box
            case 3: return wave (0.5f * k * x + 0.3f * wave (0.5f * k * y));    // DUNE: ridges bent by waves
            case 4: return wave (0.5f * k * x * y + 0.25f * x);                 // KNOT: hyperbolas, twisted
            default:
            {
                const float r2 = x * x + y * y;                                  // CRATER: rings that fall away
                return wave (0.5f * k * r2) / (1.0f + r2);
            }
        }
    }

    // TERRAIN: a point orbits the surface once a period, on an ellipse (WIDTH, HEIGHT) round (X, Y); SPIN turns the
    // ellipse, DRIFT lets its centre wander.
    void renderTerrain (Voice& voice, const Patch& p, float* __restrict out, uint32_t n, bool slow)
    {
        if (slow)
        {
            voice.ph[1] += dsp::step (p.v[6], invRate) * sub;
            voice.st[2] = dsp::sine (voice.ph[1] + quarter);
            voice.st[3] = dsp::sine (voice.ph[1]);
            const float drift = p.knob[7] * 0.04f;
            voice.st[0] = voice.st[0] * 0.995f + drift * voice.noise.next();
            voice.st[1] = voice.st[1] * 0.995f + drift * voice.noise.next();
        }
        const int surface = (int) p.v[0];
        const float k = p.v[5], w = p.v[3] * 1.2f, h = p.v[4] * 1.2f;
        const float cr = voice.st[2], sr = voice.st[3];
        const float cx = p.v[1] + voice.st[0], cy = p.v[2] + voice.st[1];
        const uint32_t inc = dsp::step (voice.noteHz * pitchRatio, invRate);
        for (uint32_t i = 0; i < n; ++i)
        {
            const float ex = w * dsp::sine (voice.ph[0] + quarter), ey = h * dsp::sine (voice.ph[0]);
            const float x = cx + ex * cr - ey * sr, y = cy + ex * sr + ey * cr;
            out[i] = 0.45f * dcBlock (voice, surfaceAt (surface, x, y, k));
            voice.ph[0] += inc;
        }
    }

    // PHASE: vector phaseshaping. The phase runs to V by D, then on to 1 (V above 1: extra turns, a formant), read
    // as -cos. TWIN adds one bent the other way, detuned; SUB an octave below; FOLD folds it; GRIT holds samples.
    static float vps (uint32_t phase, float d, float invD, float invRest, float v)
    {
        const float x = (float) phase * (1.0f / dsp::cycle);
        const float bent = x < d ? v * x * invD : v + (1.0f - v) * (x - d) * invRest;
        return -dsp::sine (dsp::turns (bent) + quarter);
    }
    void renderPhase (Voice& voice, const Patch& p, float* __restrict out, uint32_t n)
    {
        const float d = 0.02f + 0.96f * p.knob[0], invD = 1.0f / d, invRest = 1.0f / (1.0f - d);
        const float d2 = 1.0f - d, invD2 = 1.0f / d2, invRest2 = 1.0f / (1.0f - d2);
        const float hz = voice.noteHz * pitchRatio;
        const float envCoef = dsp::decayCoef (std::max (0.002f, p.v[3]), rate), venv = p.v[2] * 4.0f;
        const float twin = p.knob[4], sub = p.knob[5], fold = p.knob[6];
        const uint32_t inc = dsp::step (hz, invRate), incTwin = dsp::step (hz * std::exp2 (twin * 30.0f / 1200.0f), invRate);
        const uint32_t incSub = dsp::step (hz * 0.5f, invRate);
        const uint32_t hold = 1 + (uint32_t) (p.knob[7] * p.knob[7] * 31.0f);
        const float foldGain = 0.25f * (1.0f + 5.0f * fold);
        float& env = voice.e[0];
        uint32_t held = voice.t % hold; // GRIT's count, kept as t % hold
        for (uint32_t i = 0; i < n; ++i)
        {
            const float v = std::max (0.0f, std::min (10.0f, p.v[1] + venv * env));
            float y = vps (voice.ph[0], d, invD, invRest, v);
            if (twin > 0.0f) y = (1.0f - 0.5f * twin) * y + 0.5f * twin * vps (voice.ph[1], d2, invD2, invRest2, v);
            y += sub * dsp::sine (voice.ph[2]);
            if (fold > 0.0f) y = wave (foldGain * y);
            if (held == 0) voice.st[0] = y;
            ++voice.t;
            if (++held == hold) held = 0;
            out[i] = 0.4f * dcBlock (voice, voice.st[0]);
            voice.ph[0] += inc;
            voice.ph[1] += incTwin;
            voice.ph[2] += incSub;
            env *= envCoef;
        }
    }

    // ORGAN: eight drawbars, 3 dB a step, all from one phase at the 16' (so they stay locked, as tonewheels on one
    // shaft), and a key click.
    void renderOrgan (Voice& voice, const Patch& p, float* __restrict out, uint32_t n)
    {
        static constexpr uint32_t multiple[8] = { 1, 3, 2, 4, 6, 8, 10, 12 }; // of the 16', half the note
        const float base = voice.noteHz * pitchRatio * 0.5f;
        const uint32_t inc = dsp::step (base, invRate);
        float level[8];
        for (int k = 0; k < 8; ++k)
        {
            const float bar = std::min (8.0f, std::max (0.0f, p.v[k]));
            level[k] = bar <= 0.0f || base * (float) multiple[k] > 0.45f * rate ? 0.0f : 0.15f * std::pow (10.0f, (bar - 8.0f) * 0.15f);
        }
        const float clickCoef = std::exp (-1.0f / (0.0015f * rate));
        float& click = voice.e[1];
        for (uint32_t i = 0; i < n; ++i)
        {
            const uint32_t ph = voice.ph[0];
            float y = 0.0f;
            for (int k = 0; k < 8; ++k)
                if (level[k] > 0.0f) y += level[k] * dsp::sine (ph * multiple[k]);
            y += 0.06f * click * voice.noise.next();
            click *= clickCoef;
            out[i] = y;
            voice.ph[0] += inc;
        }
    }

    // GENDY: dynamic stochastic synthesis. POINTS breakpoints make one period; each RATE's worth of periods every
    // point's level (AMP WALK) and time (TIMEWALK) take a random step, WILD making the steps' tails heavier, BOUNDS
    // the walls they bounce off. LOCK keeps the period at the note (else it wanders with the times); SMOOTH curves
    // the lines between points.
    float walk (dsp::Noise& noise, float wild) const
    {
        const float u = noise.next();
        const float tail = std::max (-4.0f, std::min (4.0f, 0.2f * u / (1.05f - std::fabs (u))));
        return u + (tail - u) * wild;
    }
    static float bounce (float x, float lo, float hi)
    {
        if (x > hi) x = 2.0f * hi - x;
        if (x < lo) x = 2.0f * lo - x;
        return std::max (lo, std::min (hi, x));
    }
    void renderGendy (Voice& voice, const Patch& p, float* __restrict out, uint32_t n)
    {
        const int points = std::min (16, std::max (3, (int) p.v[0]));
        const float ampWalk = p.knob[1] * 0.3f, timeWalk = p.knob[2] * 0.5f, wild = p.knob[3], lock = p.knob[4];
        const float smooth = p.knob[5], bound = 0.2f + 0.8f * p.knob[6];
        const float rest = 1.0f - p.knob[7];
        const int every = 1 + (int) std::lround (rest * rest * 63.0f);
        const float period = rate / std::max (1.0f, voice.noteHz * pitchRatio);
        // The points' total time, summed again only when they walk or their number changes (st[0], st[1]).
        if ((int) voice.st[1] != points)
        {
            float total = 0.0f;
            for (int j = 0; j < points; ++j) total += voice.pointDur[j];
            voice.st[0] = total, voice.st[1] = (float) points;
        }
        auto segmentLength = [&] (int at) {
            const float total = voice.st[0];
            const float locked = period * voice.pointDur[at] / total;
            const float loose = period / (float) points * voice.pointDur[at] * 1.6f;
            return std::max (1.0f, loose + (locked - loose) * lock);
        };
        if (voice.segment >= points) voice.segment = 0;
        if (voice.segLen <= 1.0f && voice.segPos == 0.0f) voice.segLen = segmentLength (voice.segment);
        float segStep = 1.0f / voice.segLen;
        for (uint32_t i = 0; i < n; ++i)
        {
            if (voice.segPos >= 1.0f)
            {
            while (voice.segPos >= 1.0f)
            {
                voice.segPos -= 1.0f;
                voice.segment = (voice.segment + 1) % points;
                if (voice.segment == 0 && ++voice.cycles % (uint32_t) every == 0)
                    for (int j = 0; j < points; ++j)
                    {
                        voice.pointAmp[j] = bounce (voice.pointAmp[j] + ampWalk * walk (voice.noise, wild), -bound, bound);
                        voice.pointDur[j] = bounce (voice.pointDur[j] + timeWalk * walk (voice.noise, wild), 0.1f, 1.0f);
                    }
                if (voice.segment == 0 && voice.cycles % (uint32_t) every == 0) voice.st[1] = 0.0f; // walked: the total again
                if ((int) voice.st[1] != points)
                {
                    float total = 0.0f;
                    for (int j = 0; j < points; ++j) total += voice.pointDur[j];
                    voice.st[0] = total, voice.st[1] = (float) points;
                }
                voice.segLen = segmentLength (voice.segment);
            }
            segStep = 1.0f / voice.segLen;
            }
            const float a = voice.pointAmp[voice.segment], b = voice.pointAmp[(voice.segment + 1) % points];
            const float t = voice.segPos;
            const float curved = 0.5f - 0.5f * dsp::sine (dsp::turns (0.5f * t) + quarter);
            const float y = a + (b - a) * (t + (curved - t) * smooth);
            out[i] = 0.75f * dcBlock (voice, y);
            voice.segPos += segStep;
        }
    }

    // LENGTH, VOX's and PULSAR's own decay (its top INF: held until note-off): its envelope and its fall a sample.
    // True for INF.
    bool lengthEnvelope (Voice& voice, const Patch& p, float*& env, float& coef)
    {
        const bool inf = p.knob[7] >= infAbove;
        env = &voice.e[1];
        coef = inf ? 1.0f : dsp::decayCoef (std::max (0.005f, p.v[7]), rate);
        return inf;
    }

    // VOX: VOSIM. Each period starts PULSES sin^2 pulses at a formant, each P DECAY softer, then silence; two such
    // (VOWEL's formants, moved by SHIFT and swept by F ENV) make a vowel.
    bool renderVox (Voice& voice, const Patch& p, float* __restrict out, uint32_t n)
    {
        static constexpr float f1[5] = { 730.0f, 530.0f, 270.0f, 570.0f, 300.0f }, f2[5] = { 1090.0f, 1840.0f, 2290.0f, 840.0f, 870.0f };
        const float at = p.knob[1] * 4.0f;
        const int lo = std::min (3, (int) at);
        const float frac = at - (float) lo;
        const float form1 = f1[lo] + (f1[lo + 1] - f1[lo]) * frac, form2 = f2[lo] + (f2[lo + 1] - f2[lo]) * frac;
        const float hz = voice.noteHz * pitchRatio * std::exp2 (p.v[0] / 12.0f);
        const uint32_t inc = dsp::step (hz, invRate);
        const int pulses = std::min (8, std::max (0, (int) p.v[3]));
        float level[8];
        level[0] = 1.0f;
        for (int k = 1; k < 8; ++k) level[k] = level[k - 1] * (1.0f - 0.95f * p.knob[4]);
        const float fenv = p.v[5] * 3.0f, fc = dsp::decayCoef (std::max (0.002f, p.v[6]), rate);
        const float step1 = form1 * p.v[2] * invRate, step2 = form2 * p.v[2] * invRate;
        float coef, *env;
        const bool inf = lengthEnvelope (voice, p, env, coef);
        float& fe = voice.e[0];
        auto pulse = [&] (float q) {
            const int k = (int) q;
            if (k >= pulses) return 0.0f;
            return level[k] * (0.5f - 0.5f * dsp::sine (dsp::turns (q - (float) k) + quarter));
        };
        for (uint32_t i = 0; i < n; ++i)
        {
            const uint32_t before = voice.ph[0];
            voice.ph[0] += inc;
            if (voice.ph[0] < before) voice.st[0] = voice.st[1] = 0.0f; // a new period: the bursts start again
            const float y = pulse (voice.st[0]) + 0.55f * pulse (voice.st[1]);
            const float sweep = dsp::exp2Fast (fenv * fe);
            voice.st[0] += step1 * sweep;
            voice.st[1] += step2 * sweep;
            out[i] = 0.5f * dcBlock (voice, y) * *env;
            fe *= fc;
            *env *= coef;
        }
        return ! inf && *env < silent;
    }

    // PULSAR: a pulsaret (one cycle at FORMANT times the note, its SHAPE from sine to saw to square to noise, its
    // ENVELOPE from flat to falling to a bell) fired at the note, or DENSITY octaves below it (down to a rhythm);
    // MASK leaves some out, JITTER moves them.
    static bool maskPlays (int mask, uint32_t k, dsp::Noise& noise)
    {
        switch (mask)
        {
            case 1: return k % 2 == 0;
            case 2: return k % 3 != 2;
            case 3: return k % 4 != 3;
            case 4: return (0xb5 >> (k % 8)) & 1; // five of eight
            case 5: return noise.next() > 0.0f;
            case 6: return noise.next() > 0.5f;
            case 7: return (0x25 >> (k % 8)) & 1; // three of eight, spread
            default: return true;
        }
    }
    bool renderPulsar (Voice& voice, const Patch& p, float* __restrict out, uint32_t n)
    {
        const float hz = voice.noteHz * pitchRatio * std::exp2 (p.v[0] / 12.0f);
        const float every = rate / std::max (0.05f, hz * std::exp2 (-8.0f * p.knob[2]));
        const float qStep = hz * p.v[1] * invRate;
        const float shape = p.knob[3] * 3.0f, envShape = p.knob[4], jitter = p.knob[6];
        const int mask = (int) p.v[5];
        float coef, *env;
        const bool inf = lengthEnvelope (voice, p, env, coef);
        for (uint32_t i = 0; i < n; ++i)
        {
            if (voice.st[0] <= 0.0f)
            {
                voice.st[2] = maskPlays (mask, voice.pulses++, voice.noise) ? 1.0f : 0.0f;
                voice.st[1] = 0.0f;
                voice.st[0] += every * (1.0f + jitter * 0.5f * voice.noise.next());
            }
            float y = 0.0f;
            if (voice.st[1] < 1.0f)
            {
                const float q = voice.st[1];
                const float sine = wave (q), saw = 1.0f - 2.0f * q, square = q < 0.5f ? 1.0f : -1.0f;
                float w;
                if (shape < 1.0f) w = sine + (saw - sine) * shape;
                else if (shape < 2.0f) w = saw + (square - saw) * (shape - 1.0f);
                else w = square + (voice.noise.next() - square) * (shape - 2.0f);
                const float expo = envShape > 0.0f ? dsp::exp2Fast (-7.2f * q) : 1.0f;
                const float e = envShape < 0.5f ? 1.0f + (expo - 1.0f) * 2.0f * envShape
                                                : expo + (dsp::exp2Fast (-36.0f * (q - 0.5f) * (q - 0.5f)) - expo) * (2.0f * envShape - 1.0f);
                y = w * e * voice.st[2];
                voice.st[1] += qStep;
            }
            voice.st[0] -= 1.0f;
            out[i] = 1.3f * dcBlock (voice, y) * *env;
            *env *= coef;
        }
        return ! inf && *env < silent;
    }

    // MODAL: twelve resonating modes, their ratios from MATERIAL (a string's harmonics, a bar's, a drum skin's, a
    // bell's), struck by a mallet (MALLET soft to hard) and NOISE; BRIGHT tilts them, DAMP shortens them, the higher
    // ones sooner, POSITION is where it is struck, SCATTER pulls the modes off their ratios.
    bool renderModal (Voice& voice, const Patch& p, float* __restrict out, uint32_t n, bool slow)
    {
        static constexpr float ratios[4][12] = {
            { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12 },
            { 1, 2.756f, 5.404f, 8.933f, 13.34f, 18.64f, 24.81f, 31.87f, 39.81f, 48.63f, 58.33f, 68.92f },
            { 1, 1.594f, 2.136f, 2.296f, 2.653f, 2.918f, 3.156f, 3.501f, 3.6f, 3.652f, 4.06f, 4.154f },
            { 1, 2.0f, 2.39f, 3.0f, 4.0f, 5.34f, 6.04f, 6.8f, 8.15f, 9.4f, 10.8f, 12.5f },
        };
        static constexpr float scatter[12] = { 0.31f, -0.72f, 0.55f, -0.18f, 0.93f, -0.47f, 0.08f, -0.86f, 0.64f, -0.29f, 0.77f, -0.61f };
        if (slow)
            voice.st[6] = 0.0f; // the loudest this sub-block: quiet through a whole one, it has ended
        const float key[] = { voice.noteHz * pitchRatio, p.knob[0], p.knob[1], p.knob[2], p.knob[3], p.knob[4], p.knob[5], p.knob[7] };
        const bool restMoved = voice.fresh || std::memcmp (key + 1, voice.modalFor + 1, sizeof (key) - sizeof (float)) != 0;
        if (slow && (restMoved || key[0] != voice.modalFor[0] || voice.modalHz != p.v[0]))
        {
            std::memcpy (voice.modalFor, key, sizeof (key));
            voice.modalHz = p.v[0];
            const float hz = voice.noteHz * pitchRatio * std::exp2 (p.v[0] / 12.0f);
            if (restMoved) // what does not follow the pitch: the ratios, the losses, the strike's gains
            {
                const float m = p.knob[1] * 3.0f;
                const int lo = std::min (2, (int) m);
                const float frac = m - (float) lo;
                const float t60 = std::max (0.03f, 8.0f * std::pow (0.01f, p.knob[3]));
                const float pos = 0.05f + 0.9f * p.knob[4], tilt = p.knob[2] * 2.0f - 1.5f;
                for (int i = 0; i < 12; ++i)
                {
                    voice.modalRatio[i] = std::exp (std::log (ratios[lo][i]) * (1.0f - frac) + std::log (ratios[lo + 1][i]) * frac)
                                          * (1.0f + p.knob[7] * 0.06f * scatter[i]);
                    voice.modalR[i] = std::exp (-6.9077553f * (1.0f + 0.25f * (float) i) / (t60 * rate));
                    voice.modalG[i] = modalStrike (i, pos, tilt);
                }
                voice.st[1] = 1.0f + rate * (0.0001f + 0.006f * (1.0f - p.knob[5]) * (1.0f - p.knob[5])); // the mallet's width
            }
            // The strike's gains: scaled down when they add up to more than the default patch's (BRIGHT and POSITION
            // can sum them to five times as much), never up.
            float sum = 0.0f;
            for (int i = 0; i < 12; ++i)
                if (hz * voice.modalRatio[i] <= 0.45f * rate) sum += voice.modalG[i];
            const float norm = sum > modalStrikeRef * 1.001f ? modalStrikeRef / sum : 1.0f; // (a hair over: the default itself)
            for (int i = 0; i < 12; ++i)
            {
                const float f = hz * voice.modalRatio[i];
                if (f > 0.45f * rate) { voice.mg[i] = voice.mb1[i] = voice.mb2[i] = 0.0f; continue; }
                // The coupled (rotating) form: its tuning rests on sin w, exact to the bottom of the range at any
                // rate, where 2 r cos w (the direct form's) runs out of float's precision close to 2.
                const float r = voice.modalR[i];
                const float w = 2.0f * dsp::pi * f * invRate;
                voice.mb1[i] = r * std::cos (w);
                voice.mb2[i] = r * std::sin (w);
                voice.mg[i] = voice.modalG[i] * norm;
            }
        }
        // NOISE: 0.1 (the default) as it was, easing off toward the top so a full NOISE is about as loud as the strike.
        const float width = voice.st[1], noise = p.knob[6] * 0.3f * 1.22f / (1.0f + 2.2f * p.knob[6]) * modalNoiseRate;
        const float nc = modalNoiseCoef;
        // The strike first (the mallet's pulse and the noise), then each mode across the whole call, its state in
        // registers: the sum is added in the same order, mode by mode.
        float x[sub], y[sub];
        bool struckNow = false; // the strike still under way (after it, the modes only ring)
        for (uint32_t i = 0; i < n; ++i)
        {
            const float t = (float) voice.t++;
            x[i] = t < width ? (1.0f - dsp::sine (dsp::turns (t / width) + quarter)) / width : 0.0f;
            x[i] += noise * voice.e[2] * voice.noise.next();
            voice.e[2] *= nc;
            struckNow |= x[i] != 0.0f;
            y[i] = 0.0f;
        }
        // Four modes at a time: four independent recurrences keep the A55's pipeline busy, and each sample's sum is
        // still added mode by mode in order.
        for (int m = 0; m < 12; m += 4)
        {
            float b1[4], b2[4], g[4], y1[4], y2[4];
            bool any = false;
            for (int j = 0; j < 4; ++j)
            {
                b1[j] = voice.mb1[m + j], b2[j] = voice.mb2[m + j], g[j] = voice.mg[m + j];
                y1[j] = voice.my1[m + j], y2[j] = voice.my2[m + j];
                any |= b1[j] != 0.0f || b2[j] != 0.0f || g[j] != 0.0f;
            }
            if (! any) continue; // above what the rate can hold
            // Each mode a point turning by w and shrinking by r each sample (u, v); the strike pushes u, v is heard.
            for (uint32_t i = 0; i < n; ++i)
            {
                const float xi = struckNow ? x[i] : 0.0f;
                float v[4];
                for (int j = 0; j < 4; ++j)
                {
                    const float u = b1[j] * y1[j] - b2[j] * y2[j] + g[j] * xi;
                    v[j] = b2[j] * y1[j] + b1[j] * y2[j];
                    y1[j] = u;
                    y2[j] = v[j];
                }
                y[i] += v[0];
                y[i] += v[1];
                y[i] += v[2];
                y[i] += v[3];
            }
            for (int j = 0; j < 4; ++j) voice.my1[m + j] = y1[j], voice.my2[m + j] = y2[j];
        }
        for (uint32_t i = 0; i < n; ++i)
        {
            out[i] = 0.5f * y[i];
            voice.st[6] = std::max (voice.st[6], std::fabs (y[i]));
        }
        return (float) voice.t > width + rate * 0.05f && voice.st[6] < silent;
    }

    // PLUCK: Karplus-Strong. A burst of noise (PICK its brightness, POSITION a comb from where it is plucked) in a
    // line one period long, round a damping filter (BRIGHT) and a loss (DECAY); STIFF bends its overtones sharp,
    // BODY rings a wooden box, TENSION starts it sharp (or flat) and lets it settle.
    bool renderPluck (Voice& voice, const Patch& p, float* __restrict out, uint32_t n, bool slow)
    {
        float* line = &lineStore[(size_t) (&voice - voices) * lineLength];
        const uint32_t mask = lineLength - 1;
        const float hz = voice.noteHz * pitchRatio * std::exp2 (p.v[0] / 12.0f);
        const float a = 0.08f + 0.92f * p.knob[2];
        const float c = -0.6f * p.knob[5];
        const float t60 = std::max (0.02f, p.v[1]);
        if (voice.st[0] < 0.0f) // plucked: the line filled with its burst
        {
            const uint32_t period = (uint32_t) std::max (2.0f, std::min ((float) lineLength - 4.0f, rate / hz));
            const float pick = 0.05f + 0.95f * p.knob[3] * p.knob[3];
            float lp = 0.0f;
            for (uint32_t j = 0; j < period; ++j)
            {
                lp += pick * (voice.noise.next() - lp);
                line[j] = lp;
            }
            const uint32_t comb = 1 + (uint32_t) (p.knob[4] * 0.5f * (float) period);
            for (uint32_t j = period - 1; j >= comb && j < period; --j) line[j] -= line[j - comb];
            for (uint32_t j = period; j < lineLength; ++j) line[j] = 0.0f;
            voice.write = period;
            voice.st[0] = 0.0f;
            voice.aux.set (220.0f, 0.6f, rate); // BODY's box
        }
        if (slow)
        {
            voice.st[6] = 0.0f; // the loudest this sub-block, as MODAL's
            // The loop's length and loss for this sub-block, worked out here rather than each sample. TENSION's
            // glide still moves the length sample by sample (to second order: it is a few per cent at most).
            voice.st[4] = rate / hz;
            // What the damping filter (BRIGHT) and STIFF's allpass add to the loop, at the note's own frequency: their
            // delay at DC would leave the line too short (the note sharp) when BRIGHT is low or STIFF high.
            const float w0 = std::max (1e-5f, std::min (2.0f * dsp::pi * hz * invRate, 0.95f * dsp::pi)), sw = std::sin (w0), cw = std::cos (w0);
            const float b = 1.0f - a;
            const float lpDelay = std::atan2 (b * sw, 1.0f - b * cw) / w0;
            const float apDelay = (std::atan2 (-c * sw, 1.0f + c * cw) - std::atan2 (-sw, c + cw)) / w0;
            voice.st[7] = lpDelay + apDelay;
            voice.st[5] = std::exp (-6.9077553f * std::max (2.0f, voice.st[4] - voice.st[7]) / (t60 * rate));
        }
        const float period = voice.st[4], loss = voice.st[5], comp = voice.st[7], top = (float) lineLength - 4.0f;
        const float tension = p.v[7] * 0.06f, tc = pluckTensionCoef;
        const float body = p.knob[6] * 2.0f;
        for (uint32_t i = 0; i < n; ++i)
        {
            const float u = tension * voice.e[2];
            const float delay = std::max (2.0f, std::min (top, period * (1.0f - u + u * u) - comp));
            float readAt = (float) voice.write - delay; // the write position stays inside the line
            if (readAt < 0.0f) readAt += (float) lineLength;
            const uint32_t i0 = (uint32_t) readAt;
            const float f = readAt - (float) i0;
            const float y0 = line[i0 & mask];
            const float y = y0 + (line[(i0 + 1) & mask] - y0) * f;
            voice.st[1] += a * (y - voice.st[1]);
            const float v = voice.st[1] * loss;
            const float ap = c * v + voice.st[2] - c * voice.st[3];
            voice.st[2] = v;
            voice.st[3] = ap;
            line[voice.write] = ap;
            voice.write = (voice.write + 1) & mask;
            ++voice.t;
            voice.e[2] *= tc;
            out[i] = 0.6f * (y + body * voice.aux.band (y));
            voice.st[6] = std::max (voice.st[6], std::fabs (y));
        }
        return voice.t > (uint32_t) (rate * 0.05f) && voice.st[6] < silent;
    }

    // KICK: a resonator struck by a pulse (COLOUR from a clean click to a noisy burst, PULSE its level), its PITCH
    // bent down from BEND octaves over TIME, an amp (ATTACK, DECAY) driven into DRIVE and darkened by TONE.
    bool renderKick (Voice& voice, const Patch& p, float* __restrict out, uint32_t n)
    {
        const float pitch = p.v[0] * voice.st[7] * pitchRatio, bend = p.v[1];
        const float bc = std::exp (-1.0f / (std::max (0.0005f, p.v[2]) * rate));
        const float attack = p.v[4], atkStep = attack > 0.0f ? 1.0f / (attack * rate) : 1.0f;
        const float dc = dsp::decayCoef (std::max (0.01f, p.v[3]), rate);
        const float colour = p.knob[5], pulse = p.knob[6], drive = p.knob[7];
        const float pc = std::exp (-1.0f / ((0.00015f + 0.003f * colour) * rate));
        const float driveGain = 1.0f + 25.0f * drive * drive * (1.0f + 0.5f * p.knob[9] * voice.st[6]);
        const float makeup = 1.0f / (1.0f + 1.2f * drive);
        const float tone = dsp::onePole (p.v[8], rate);
        voice.aux.set (std::min (pitch * 4.0f, 8000.0f), 0.85f, rate);
        float& be = voice.e[0];
        float& amp = voice.e[1];
        float& pe = voice.e[2];
        for (uint32_t i = 0; i < n; ++i)
        {
            if (voice.st[5] == 0.0f)
            {
                amp += atkStep;
                if (amp >= 1.0f) amp = 1.0f, voice.st[5] = 1.0f;
            }
            else
                amp *= dc;
            const float osc = dsp::sine (voice.ph[0]);
            voice.ph[0] += dsp::step (pitch * dsp::exp2Fast (bend * be), invRate);
            const float click = pe * ((1.0f - colour) + colour * voice.noise.next());
            const float struck = click + 1.5f * voice.aux.band (click);
            float y = (osc + pulse * struck) * amp;
            y = dsp::softClip (y * driveGain) * makeup;
            voice.lp += tone * (y - voice.lp);
            out[i] = 0.9f * voice.lp;
            be *= bc;
            pe *= pc;
        }
        return voice.st[5] != 0.0f && amp < silent && pe < silent;
    }

    // GND: a sine (swept down by SWEEP), noise coloured by PITCH, or one click shaped by PITCH, each over DECAY.
    bool renderGnd (Voice& voice, const Patch& p, float* __restrict out, uint32_t n)
    {
        const int source = (int) p.v[0];
        const float hz = p.v[1] * pitchRatio, sweep = p.v[3];
        const float decay = std::max (0.002f, p.v[2]);
        const float ac = dsp::decayCoef (decay, rate);
        const float pc = std::exp (-1.0f / (std::max (0.0005f, 0.3f * decay) * rate));
        const float a = dsp::onePole (source == 2 ? hz * 4.0f : hz * 2.0f, rate);
        float& ae = voice.e[0];
        float& pe = voice.e[1];
        for (uint32_t i = 0; i < n; ++i)
        {
            float y;
            if (source == 0)
            {
                y = dsp::sine (voice.ph[0]);
                voice.ph[0] += dsp::step (hz * dsp::exp2Fast (sweep * pe), invRate);
            }
            else if (source == 1)
            {
                voice.lp += a * (voice.noise.next() - voice.lp);
                y = voice.lp * std::min (4.0f, 1.5f / std::sqrt (std::max (a, 1e-4f)));
            }
            else
            {
                const float impulse = voice.t == 0 ? 1.0f : 0.0f;
                voice.lp += a * (impulse - voice.lp);
                y = voice.lp / std::max (a, 1e-4f);
                ++voice.t;
            }
            out[i] = y * ae;
            ae *= ac;
            pe *= pc;
        }
        return ae < silent;
    }
};
} // namespace machine
