// The engine: the host's transport as TB-3PO's clock and reset inputs, the sequencer, and the voice it plays.
//
//   Clock: a pulse on every multiple of Clock Div from the song's top while the host plays. The applet waits 2 ms
//     after a clock for its CV inputs to settle before it steps; here the inputs are exact, so it steps on the clock.
//   Reset (the applet's Regen input): when Play starts (Reset: Play), or on every 1, 2, 4 or 8 bars from the song's
//     top (a Play or a jump between them places the pattern as if it had run from the last one), or never (Off).
//     Each one makes the pattern again, from a new seed unless the seed is locked. The Regen button is one too.
//   CV 1 (transpose): the Transpose knob plus the last MIDI note played (C4 is no transposition), held as a CV
//     keyboard holds its last note.
//   CV 2 (density): the Density knob (a host's automation of it does what CV 2 did).
//   The gate's half-step timer: half a clock at the host's tempo. (The applet measures it from the last two clocks:
//     the same while the tempo holds; on the module the first note after the clock starts is long.)
//   Swing: the second clock of each pair comes later, at Swing's share of the pair, as an MPC's swing does; a
//     swung step's gate is half of its own length.
//   Seed Back: the seeds whose patterns played, the last 16, to go back to; going back locks the seed.
//   Gate: the half-step timer's share of the step (half by default, as the applet). Mutate: see Tb3po::reset.
//   Pitch bend and the mod wheel: they bend the voice and open its filter, in either mode.
//   Mode: Notes: MIDI notes play the voice instead of TB-3PO (whose count goes on, unheard, so switching back lands
//     in time): a note over a held one glides to it with the 303's glide and keeps the gate open, a velocity of 100
//     or more accents, and letting go of the newest note goes back to the one still held. A key played again while
//     held is a new note, as if let go first (it strikes again, or glides over another key still held).
#pragma once

#include "params.h"
#include "tb3po.h"
#include "voice.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>

// tests/reference.cpp sees each step and gate change through this (0: a clock's step, 1: the half-step timer).
#ifndef TBMPC_TRACE
#define TBMPC_TRACE(what)
#endif

namespace tbmpc
{
// A clock less than this many frames away is due now. CLAP gives the song's place in steps of 2^-31 beats, which
// at 48 kHz is about 1e-5 frames: a clock that falls on a block's first frame could otherwise come a frame late, by
// how the host's rounding fell for that block, so the sound would depend on the block size.
constexpr double clockSlack = 1.0e-3;

class Engine
{
public:
    void prepare (double hostRate, uint32_t)
    {
        rate = hostRate;
        voice.prepare (hostRate);
        reset();
    }

    void reset()
    {
        voice.reset();
        seq.stop();
        voice.gate (false, false);
        playing = false;
        lastClock = -1;
        gateOffPending = false;
        regenPressed = false;
        frameCount = 0;
        clockedAt = -(int64_t (1) << 40); // no clock played yet
        clockedReset = false;
        held = 0;
    }

    // First values, before anything plays: the applet's Start(), then every parameter.
    void start (const float* values)
    {
        seq.start (entropy());
        for (uint32_t i = 0; i < paramCount; ++i)
            set (paramTable[i].id, values[i]);
        seq.regenerate();
    }

    // A parameter's value, from the frame the next render starts.
    void set (uint32_t id, float v)
    {
        if (id >= ParamId::count)
            return;
        knob[id] = v;
        switch (id)
        {
            case ParamId::seedMode: seq.setLocked (v >= 0.5f); break;
            case ParamId::seed: seq.setSeed ((uint16_t) v); break;
            case ParamId::density: seq.setDensity ((int) v + 7); break;
            case ParamId::scale: seq.setScale ((int) v); break;
            case ParamId::root: seq.setRoot ((int) v); break;
            case ParamId::octave: seq.setOctave ((int) v); break;
            case ParamId::steps: seq.setSteps ((int) v); break;
            case ParamId::transpose: transposeKnob = (int) v; updateTranspose(); break;
            case ParamId::clockDiv: regridWanted = true; break;
            case ParamId::mode:
                if ((v >= 0.5f) != notesMode)
                {
                    // Either way the note sounding stops: a held key or a sequencer's gate belongs to the other mode.
                    notesMode = v >= 0.5f;
                    held = 0;
                    seq.stop();
                    gateOffPending = false;
                    voice.gate (false, false);
                }
                break;
            case ParamId::regen:
            case ParamId::seedBack:
            case ParamId::swing:
            case ParamId::gate:
            case ParamId::mutate: break;
            default: voice.set (id, v); break;
        }
    }

    // The Regen button: a reset pulse now. Seed Back: the seed before, locked.
    void press (uint32_t id)
    {
        if (id == ParamId::regen)
            regenPressed = true;
        else if (id == ParamId::seedBack)
            seedBack();
    }

    // For the screen: the pattern, and whether the host's clock is moving it (TB-3PO mode, the host playing).
    const Tb3po& sequencer() const { return seq; }
    bool running() const { return playing && ! notesMode; }

    // A MIDI note. TB-3PO: it transposes the pattern from the next step, and holds (as a CV keyboard's last note
    // does). Notes: it plays.
    void note (int key, float velocity)
    {
        if (! notesMode)
        {
            midiTranspose = key - 60;
            updateTranspose();
            return;
        }
        // A key played again while held (a host retriggering it before its note off: Melodee then sends one off for
        // both) counts as let go first: on its own it strikes again, over another held key it glides as any note.
        release (key);
        const bool legato = held > 0;
        if (held == maxHeld)
            std::copy (heldKeys + 1, heldKeys + maxHeld, heldKeys), std::copy (heldAccent + 1, heldAccent + maxHeld, heldAccent), --held;
        heldKeys[held] = (int8_t) key;
        heldAccent[held] = velocity >= accentVelocity - 1.0e-6f;
        ++held;
        const int32_t cv = (key - noteAtZeroVolts) * 128;
        if (legato)
            voice.glide (cv); // the gate stays open: no new envelope
        else
        {
            voice.pitch (cv, cv);
            voice.gate (false, false); // still open (the key played again): shut on this frame, so both envelopes start over
        }
        voice.gate (true, heldAccent[held - 1]);
    }

    // A MIDI message other than a note: pitch bend (+-2 semitones), the mod wheel (CC 1), all notes off (CC 120, 123).
    void midi (int status, int data1, int data2)
    {
        if (status == 0xe0)
            voice.bendTo ((float) (((data2 << 7) | data1) - 8192) * (bendSemitones / 8192.0f));
        else if (status == 0xb0 && data1 == 1)
            voice.modWheelTo ((float) data2 / 127.0f);
        else if (status == 0xb0 && (data1 == 120 || data1 == 123) && notesMode && held > 0)
        {
            held = 0;
            voice.gate (false, false);
        }
    }

    void noteOff (int key)
    {
        if (! notesMode || held == 0)
            return;
        const bool wasTop = heldKeys[held - 1] == key;
        release (key);
        if (held == 0)
            voice.gate (false, false);
        else if (wasTop)
        {
            voice.glide ((heldKeys[held - 1] - noteAtZeroVolts) * 128);
            voice.gate (true, heldAccent[held - 1]);
        }
    }

    // A value the engine set itself (a new seed from a reset, the seed and Seed Mode from Seed Back), once each: the
    // host must hear them as the parameters' values.
    bool takeChange (uint32_t& id, float& value)
    {
        if (changed & (1u << 0))
        {
            changed &= ~(1u << 0);
            id = ParamId::seed, value = (float) seq.currentSeed();
            return true;
        }
        if (changed & (1u << 1))
        {
            changed &= ~(1u << 1);
            id = ParamId::seedMode, value = knob[ParamId::seedMode];
            return true;
        }
        return false;
    }

    // Where the sequence's own count is: for a host that gives no song position (0 when it last stopped).
    double ownPosition() const { return playing ? position : 0.0; }

    // The host's transport at the start of a process call of `frames`. barBeats: quarter notes in a bar.
    void transport (bool isPlaying, double beats, double bpm, double barBeats, uint32_t frames)
    {
        perFrame = bpm / 60.0 / rate;
        bar = barBeats > 0.0 ? barBeats : 4.0;
        const double window = (double) std::max (frames, lastBlock);
        lastBlock = frames;
        if (! isPlaying)
        {
            if (playing)
            {
                // Stopped: the sequencer's gate shuts (the note releases); notes played by hand go on.
                seq.stop();
                if (! notesMode)
                    voice.gate (false, false);
                gateOffPending = false;
            }
            playing = false;
            return;
        }
        // A Play, or a place other than where the last block ended by more than a block (a jump, a loop going
        // round): line up again. Less than that is drift (a tempo change inside the last block), followed as it is:
        // the clocks already played are not played again, one passed is played at once.
        const bool started = ! playing;
        const double moved = std::fabs (beats - position);
        if (started || (moved > 0.5 * perFrame && moved >= window * perFrame))
            relocate (beats, window, started);
        playing = true;
        position = beats;
    }

    void render (float* left, float* right, uint32_t frames)
    {
        while (frames > 0)
        {
            if (regenPressed)
            {
                regenPressed = false;
                resetPulse();
            }
            uint32_t run = frames;
            if (playing)
            {
                if (regridWanted)
                    regrid();
                const double next = clockBeat (lastClock + 1);
                const double ahead = (next - position) / perFrame;
                if (ahead <= clockSlack)
                {
                    ++lastClock;
                    clock (lastClock);
                    continue;
                }
                const double untilClock = std::ceil (ahead - clockSlack);
                if (untilClock < (double) run)
                    run = (uint32_t) untilClock;
            }
            if (gateOffPending)
            {
                if (gateOffIn == 0)
                {
                    gateOffPending = false;
                    seq.halfStepPassed();
                    voice.gate (seq.gateCv > 0, seq.gateCv == Tb3po::gateAccent);
                    TBMPC_TRACE (1);
                    continue;
                }
                run = std::min (run, gateOffIn);
            }
            voice.render (left, right, run);
            frameCount += run;
            if (gateOffPending)
                gateOffIn -= run;
            if (playing)
                position += run * perFrame;
            left += run;
            if (right)
                right += run;
            frames -= run;
        }
    }

private:
    double stepBeats() const { return clockDivBeats[(int) knob[ParamId::clockDiv]]; }
    bool swung() const { return knob[ParamId::swing] != 0.5f; }

    // Clock k's place in beats. Swung, every odd clock moves to its pair's start plus Swing's share of the pair; at
    // 50 % the clocks are k grids from the top, worked out exactly as before Swing was added.
    double clockBeat (int64_t k) const
    {
        const double grid = stepBeats();
        if (! swung() || (k & 1) == 0)
            return (double) k * grid;
        return (double) (k - 1) * grid + 2.0 * grid * (double) knob[ParamId::swing];
    }
    // The last clock at or before `beats`.
    int64_t clockAtOrBefore (double beats) const
    {
        const double grid = stepBeats();
        const int64_t b = (int64_t) std::floor (beats / grid + 1.0e-9);
        if (! swung() || (b & 1) == 0)
            return b;
        return beats / grid + 1.0e-9 < clockBeat (b) / grid ? b - 1 : b; // between the grid line and its late clock
    }
    // The first clock at or after `beats`.
    int64_t clockAtOrAfter (double beats) const
    {
        if (! swung())
            return (int64_t) std::ceil (beats / stepBeats() - 1.0e-9);
        const int64_t k = clockAtOrBefore (beats);
        return clockBeat (k) / stepBeats() >= beats / stepBeats() - 1.0e-9 ? k : k + 1;
    }
    int resetEvery() const { return resetBars[(int) knob[ParamId::reset]]; } // bars, 0: at Play or never
    bool resetAtPlay() const { return (int) knob[ParamId::reset] == 0; }

    // The bar-reset line a beat is in (counting from the song's top).
    int64_t lineOf (double beats) const { return (int64_t) std::floor (beats / (resetEvery() * bar) + 1.0e-9); }
    // A clock is a reset clock when a bar-reset line falls in (the clock before, this clock].
    bool resetClock (int64_t k) const
    {
        return resetEvery() > 0 && lineOf (clockBeat (k)) != lineOf (clockBeat (k - 1));
    }

    void relocate (double beats, double window, bool started)
    {
        const int64_t b = clockAtOrBefore (beats);
        const double late = (beats - clockBeat (b)) / perFrame; // frames since the host's clock b
        // A jump whose clock b came on the very frame a clock played here: a loop gone round inside a block (the host
        // tells only at the next one, so the clocks ran on past the loop's end; its end is its start). That clock was
        // b: it is not played again (the pattern would step twice); a bar reset that b would have made, it makes now.
        // Otherwise, just past a clock (less than a block: a Play landing a hair late, a wrap seen early enough):
        // that clock comes at once; further in, the next is the first.
        const bool played = ! started && std::fabs ((double) ((int64_t) frameCount - clockedAt) - late) <= 2.0;
        lastClock = played ? b : b - (late < window ? 1 : 0);
        position = beats;
        regridWanted = false;
        if (played && resetClock (b) && ! clockedReset)
            resetPulse();
        const int64_t first = lastClock + 1;
        if (resetEvery() > 0)
        {
            if (resetClock (first))
                return; // the first clock is a reset clock: it resets as it comes
            // Between lines: as if reset at the last one's clock and stepped on since (a Play is a reset there too).
            const double line = (double) lineOf (clockBeat (first)) * resetEvery() * bar;
            const int64_t from = clockAtOrAfter (line);
            if (started)
                resetPulse();
            seq.place (first - from);
            return;
        }
        if (started && resetAtPlay())
            resetPulse();
    }

    // Clock Div changed: count clocks on the new grid from here, without a burst of them. A clock of the new grid due
    // on this frame (in the last frame's time) still comes.
    void regrid()
    {
        lastClock = clockAtOrBefore (position - perFrame * (1.0 - clockSlack));
        regridWanted = false;
    }

    void resetPulse()
    {
        const uint16_t before = seq.currentSeed();
        seq.reset (entropy(), knob[ParamId::mutate]);
        if (seq.currentSeed() != before)
            changed |= 1u << 0;
    }

    void clock (int64_t k)
    {
        clockedAt = (int64_t) frameCount;
        clockedReset = resetClock (k);
        if (clockedReset)
            resetPulse();
        seq.clock();
        remember();
        if (notesMode)
            return; // the count goes on; the notes play the voice
        voice.pitch (seq.slideStart, seq.slideEnd);
        voice.gate (seq.gateCv > 0, seq.gateCv == Tb3po::gateAccent);
        if (seq.opened)
        {
            gateOffPending = true;
            // Half the step: at Swing 50 % half a grid, as before Swing; swung, half of this step's own length.
            const double length = swung() ? clockBeat (k + 1) - clockBeat (k) : stepBeats();
            gateOffIn = (uint32_t) std::max (1.0, std::floor ((double) knob[ParamId::gate] * length / perFrame));
        }
        TBMPC_TRACE (0);
    }

    // Seed Back's memory: when a clock plays a pattern from a new seed, the seed before goes in, if its pattern played.
    void remember()
    {
        const uint16_t now = seq.playingSeed();
        if (now != shownSeed)
        {
            if (shownPlayed && ! goingBack)
            {
                if (remembered == historySize)
                    std::copy (history + 1, history + historySize, history), --remembered;
                history[remembered++] = shownSeed;
            }
            goingBack = false;
            shownSeed = now;
            shownPlayed = false;
        }
        shownPlayed = true;
    }

    void seedBack()
    {
        // Back past seeds equal to the one asked for already.
        while (remembered > 0 && history[remembered - 1] == seq.currentSeed())
            --remembered;
        if (remembered == 0)
            return;
        const uint16_t s = history[--remembered];
        seq.setSeed (s);
        seq.setLocked (true);
        knob[ParamId::seed] = (float) s;
        knob[ParamId::seedMode] = 1.0f;
        goingBack = true; // the pattern left now is not remembered: Back again goes further back
        changed |= (1u << 0) | (1u << 1);
    }

    // A key let go (or played again): out of the held list.
    void release (int key)
    {
        int n = 0;
        for (int i = 0; i < held; ++i)
            if (heldKeys[i] != key)
                heldKeys[n] = heldKeys[i], heldAccent[n] = heldAccent[i], ++n;
        held = n;
    }

    void updateTranspose()
    {
        const int semitones = std::clamp (transposeKnob + midiTranspose, -36, 72); // the module's CV range, -3 to +6 V
        seq.setTransposeInput (semitones * 128);
    }

    // The module seeds random() from micros() for a new seed; here, the time and a count of the seeds asked for.
    uint32_t entropy()
    {
        const auto now = std::chrono::steady_clock::now().time_since_epoch();
        const uint64_t us = (uint64_t) std::chrono::duration_cast<std::chrono::microseconds> (now).count();
        uint32_t x = (uint32_t) (us ^ (us >> 32)) ^ (0x9e3779b9u * ++draws);
        x ^= x >> 16, x *= 0x7feb352du, x ^= x >> 15;
        return x;
    }

    Tb3po seq;
    Voice voice;
    double rate = 48000.0, perFrame = 0.0, position = 0.0, bar = 4.0;
    float knob[ParamId::count] {};
    int transposeKnob = 0, midiTranspose = 0;
    bool playing = false, regridWanted = false, gateOffPending = false, regenPressed = false, notesMode = false;
    uint32_t changed = 0; // values to tell the host: bit 0 the seed, bit 1 Seed Mode
    // Mode: Notes: the keys held, oldest first.
    static constexpr int maxHeld = 16;
    int8_t heldKeys[maxHeld] {};
    bool heldAccent[maxHeld] {};
    int held = 0;
    // Seed Back: the seeds whose patterns played, oldest first.
    static constexpr int historySize = 16;
    uint16_t history[historySize] {};
    int remembered = 0;
    uint16_t shownSeed = 0;
    bool shownPlayed = false, goingBack = false;
    int64_t lastClock = -1;
    uint32_t gateOffIn = 0, lastBlock = 0, draws = 0;
    uint64_t frameCount = 0; // frames rendered since prepare()
    int64_t clockedAt = 0;   // the frame the last clock came on
    bool clockedReset = false; // and whether it was a bar reset
};
} // namespace tbmpc
