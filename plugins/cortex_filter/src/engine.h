// Cortex Filter's sound: input -> Drive (a soft saturator) -> the filter (filters.h: a cube of morph.h's filters, with
// its own offsets, gains, fades, levels and distortion) at the point the knobs, the LFO and the envelope follower pick
// -> Mix with the input -> Output -> a soft limit above full scale. The sequencer (filters.h) picks the filter, moved
// on by its clocks (the song's place), the envelope follower, the Inc / Dec / Reset parameters and the page.
//
// The point moves at the control rate, every `controlFrames` frames counted from activation, whatever the host's
// blocks: the same input and events give the same output in any block size. Gains (Drive, Mix, Output) glide every
// sample. Each channel has its own filter state; Spread gives each its own point.
#pragma once

#include "filters.h"
#include "morph.h"
#include "params.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numeric>

namespace cortex
{
#ifndef CORTEX_CONTROL_FRAMES
#define CORTEX_CONTROL_FRAMES 8 // tools/artifacts.sh builds it at 1 too, to measure what the steps cost
#endif
constexpr uint32_t controlFrames = CORTEX_CONTROL_FRAMES; // the point and the filter are worked out this often

// The sequencer's clocks in 1/24ths of a beat: every length is a whole number of them (a 1/32 is 3, a 1/16T 4, 16
// bars 1536), so the boundaries of two clocks that fall together are found to fall together.
constexpr int64_t clockTicksPerBeat = 24;
template <size_t N>
constexpr bool wholeTicks (const double (&lengths)[N])
{
    for (size_t i = 0; i < N; ++i)
    {
        const double t = lengths[i] * (double) clockTicksPerBeat, whole = (double) (int64_t) (t + 0.5);
        if (t - whole > 1e-9 || whole - t > 1e-9)
            return false;
    }
    return true;
}
static_assert (wholeTicks (clockBeats) && wholeTicks (resetBeats), "a clock's length in whole ticks");
inline int64_t floorDiv (int64_t a, int64_t b) { return a / b - (a % b != 0 && (a < 0) != (b < 0) ? 1 : 0); }

// The left and right channels side by side, as one 2-lane vector (a NEON register on the MPC): each band runs both
// sides at once.
typedef float Pair __attribute__ ((vector_size (8)));
struct PairCoefs
{
    Pair a1, a2, a3, m0, m1, m2; // morph.h's BandCoefs, left and right
};
struct PairState
{
    Pair ic1, ic2;
};

// A value from 32 bits: the same for the same cycle (S&H and Drift follow the song's place).
inline float hashUnit (int64_t n, uint32_t axis)
{
    uint64_t x = (uint64_t) n * 0x9E3779B97F4A7C15ull + (uint64_t) (axis + 1) * 0xD1B54A32D192ED03ull;
    x ^= x >> 33, x *= 0xff51afd7ed558ccdull, x ^= x >> 33, x *= 0xc4ceb9fe1a85ec53ull, x ^= x >> 33;
    return (float) (x >> 40) * (2.0f / 16777216.0f) - 1.0f;
}

// The LFO's value (-1..1) `cycles` cycles from its start, for axis 0-2 (the random shapes draw per axis).
inline float lfoValue (Shape shape, double cycles, uint32_t axis)
{
    const double whole = std::floor (cycles);
    const float u = (float) (cycles - whole);
    const int64_t n = (int64_t) whole;
    switch (shape)
    {
        case Shape::sine: return std::sin (2.0f * pi * u);
        case Shape::triangle: return u < 0.25f ? 4.0f * u : (u < 0.75f ? 2.0f - 4.0f * u : 4.0f * u - 4.0f);
        case Shape::sawUp: return 2.0f * u - 1.0f;
        case Shape::sawDown: return 1.0f - 2.0f * u;
        case Shape::square: return u < 0.5f ? 1.0f : -1.0f;
        case Shape::sampleHold: return hashUnit (n, axis);
        case Shape::drift:
        {
            const float a = hashUnit (n, axis), b = hashUnit (n + 1, axis), s = u * u * (3.0f - 2.0f * u);
            return a + (b - a) * s;
        }
    }
    return 0.0f;
}

// Drive's saturator: a rational tanh, exact 1 from |u| = 3.
__attribute__ ((always_inline)) inline float saturate (float u)
{
    if (u >= 3.0f) return 1.0f;
    if (u <= -3.0f) return -1.0f;
    const float u2 = u * u;
    return u * (27.0f + u2) / (27.0f + 9.0f * u2);
}

// The same on both sides at once, for the filter's distortion.
__attribute__ ((always_inline)) inline Pair saturatePair (Pair u)
{
    const Pair lo { -3.0f, -3.0f }, hi { 3.0f, 3.0f };
    u = u < lo ? lo : (u > hi ? hi : u);
    const Pair u2 = u * u;
    return u * (27.0f + u2) / (27.0f + 9.0f * u2);
}

// The output's limit: nothing below full scale, then a soft knee to at most 2. Not a number (a broken input) is
// silence, an infinity the ceiling.
__attribute__ ((always_inline)) inline float limit (float y)
{
    const float a = std::fabs (y);
    if (a <= 1.0f) return y;
    if (! (a < 1e30f))
        return y != y ? 0.0f : (y < 0.0f ? -2.0f : 2.0f);
    const float e = a - 1.0f, out = 1.0f + e / (1.0f + e);
    return y < 0.0f ? -out : out;
}

class Engine
{
public:
    void prepare (double sampleRate, uint32_t maxFrames)
    {
        (void) maxFrames; // the engine runs in control steps of its own, whatever the host's blocks
        rate = (float) sampleRate;
        derive();
        reset();
    }

    // Back to rest: filters emptied, every glide at its value, the LFO at its start, the sequence at its first step.
    void reset()
    {
        for (auto& s : state)
            s = PairState {};
        for (int a = 0; a < 3; ++a)
            knob[a] = target[a];
        emphasisNow = emphasisTarget;
        mixNow = mixTarget, outNow = outTarget, driveNow = driveTarget, compNow = compTarget;
        spreadNow = spreadAmount, envGainNow = envGainLin;
        for (int a = 0; a < 3; ++a)
            lfoDepthNow[a] = lfoDepth[a], envDepthNow[a] = envDepth[a];
        switchGain = 1.0f, switchDir = 0;
        env = 0.0f, envNorm = 0.0f;
        lfoCycles = 0.0;
        untilTick = 0;
        designed = false;
        songPlaying = false;
        restartSequence();
        snapFilter = true;
    }

    // A state loaded while running (a project opening): every glide lands on its value at once, so the sound starts
    // as it was saved; the filters keep what they hold, and a different filter comes in at once with its filter
    // empty; the sequence starts again. (A preset, by contrast, glides in.)
    void settle()
    {
        for (int a = 0; a < 3; ++a)
            knob[a] = target[a], lfoDepthNow[a] = lfoDepth[a], envDepthNow[a] = envDepth[a];
        emphasisNow = emphasisTarget, spreadNow = spreadAmount, envGainNow = envGainLin;
        mixNow = mixTarget, outNow = outTarget, driveNow = driveTarget, compNow = compTarget;
        restartSequence();
        const int slot = targetSlot();
        if (slot != loadedSlot || store.cubeOf (slot) != cubeIndex)
        {
            loadedSlot = slot, cubeIndex = store.cubeOf (slot);
            for (auto& s : state)
                s = PairState {};
        }
        snapFilter = true;
        switchGain = 1.0f, switchDir = 0;
    }

    void set (uint32_t id, float v)
    {
        using namespace ParamId;
        switch (id)
        {
            case morph: target[0] = v; break;
            case freq: target[1] = v; break;
            case transform: target[2] = v; break;
            case emphasis: emphasisTarget = v; break;
            case glide: glideKnob = v; break;
            case drive: driveDb = v; break;
            case mix: mixTarget = v; break;
            case output: outputDb = v; break;
            case spread: spreadAmount = v; break;
            case lfoSync: syncIndex = std::clamp ((int) v, 0, (int) (sizeof (lfoSyncBeats) / sizeof (lfoSyncBeats[0])) - 1); break;
            case lfoRate: lfoRateKnob = v; break;
            case lfoShape: shape = (Shape) std::clamp ((int) v, 0, 6); break;
            case lfoPhase: phaseOffset = v; break;
            case lfoMorph: lfoDepth[0] = v; break;
            case lfoFreq: lfoDepth[1] = v; break;
            case lfoTransform: lfoDepth[2] = v; break;
            case envGain: envGainDb = v; break;
            case envAttack: attackKnob = v; break;
            case envRelease: releaseKnob = v; break;
            case envMorph: envDepth[0] = v; break;
            case envFreq: envDepth[1] = v; break;
            case envTransform: envDepth[2] = v; break;
            case filter: manualSlot = std::clamp ((int) v, 1, slotCount); return;
            case sequence:
            {
                const int q = std::clamp ((int) v, 0, sequenceCount);
                if (q != sequenceParam)
                    sequenceParam = q, loadPending = true;
                return;
            }
            case incClock: setClock (0, clockIndexOf (v, clockBeats)); return;
            case decClock: setClock (1, clockIndexOf (v, clockBeats)); return;
            case resetClock: setClock (2, clockIndexOf (v, resetBeats)); return;
            case envTrigger: envTrigMode = std::clamp ((int) v, 0, 3); return;
            case trigLevel: trigLevelDb = v; return;
            case trigInc: gate (0, v); return;
            case trigDec: gate (1, v); return;
            case trigReset: gate (2, v); return;
            default: return; // the edit filter's fields live in the bank (bank()), not here
        }
        derive();
    }

    // The host's transport at the block's start: whether it plays, the song's place in beats (if it gives one) and
    // the tempo. A synced LFO follows the place while the song plays, and runs on at the tempo while it does not.
    // The place is counted on in frames from where the song started (or jumped, or changed tempo), not read again
    // each block: a host's place is rounded (CLAP's to 2^-31 beats), and reading it each block would make the LFO's
    // last bits depend on where the blocks fall. The song starting starts the sequence again.
    void transport (bool playing, bool placed, double beats, double bpm)
    {
        const double perFrame = bpm / 60.0 / (double) rate;
        const bool now = playing && placed;
        if (now && (! songPlaying || perFrame != beatsPerFrame || std::fabs (beats - songBeatsAt (frame)) > 1e-6))
            anchorBeats = beats, anchorFrame = frame; // started, jumped or a new tempo
        if (now && ! songPlaying)
            startPending = true;
        songPlaying = now;
        beatsPerFrame = perFrame;
    }

    // The page's triggers (0 Inc, 1 Dec, 2 Reset) and wheel, taken at the next control step.
    void trigger (int which, uint32_t times = 1)
    {
        if (which >= 0 && which < 3)
            pending[which] += times;
    }
    void browse (int steps) { pendingBrowse += steps; }

    // `n` frames of the input (either side may be null: silence) to the output (outR may be null). In and out may be
    // the same buffers.
    void render (const float* inL, const float* inR, float* outL, float* outR, uint32_t n)
    {
        if (inL == nullptr)
            inL = inR;
        while (n > 0)
        {
            if (untilTick == 0)
                tick();
            const uint32_t run = std::min (n, untilTick);
            const float* l = inL != nullptr ? inL : silence; // no input: a control step of silence
            const float* r = inR != nullptr ? inR : l;
            if (driving)
                distorting ? runBands<true, true> (l, r, outL, outR, run) : runBands<true, false> (l, r, outL, outR, run);
            else
                distorting ? runBands<false, true> (l, r, outL, outR, run) : runBands<false, false> (l, r, outL, outR, run);
            if (inL != nullptr) inL += run;
            if (inR != nullptr) inR += run;
            outL += run;
            if (outR != nullptr) outR += run;
            n -= run;
            untilTick -= run;
            frame += run;
        }
    }

    // The filters and sequences (shared with the plug-in's main thread: filters.h).
    Store& bank() { return store; }
    const Store& bank() const { return store; }

    // For the screen: where each channel's filter is, the envelope (0..1) and the LFO's place (in cycles); the filter
    // heard, the sequencer's place, the knobs' and modulation's sum on each axis before the filter scales it (-1..1,
    // the module's "CV"), whether Transform drives the distortion, and the meters since they were last taken.
    Position left() const { return pointL; }
    Position right() const { return pointR; }
    bool split() const { return splitNow; }
    float envelope() const { return envNorm; }
    double lfoPlace() const { return lfoCycles; }
    float sampleRate() const { return rate; }
    int slotNow() const { return loadedSlot; }
    const Sequencer& sequencer() const { return seq; }
    bool sequencing() const { return seq.on (store); }
    uint32_t triggerCount() const { return triggers; }
    float control (int axis) const { return controlNow[axis]; }
    bool transformDrives() const { return transformDist; }
    // The distortion's peak drive since the last call, left and right (1 is where its saturators start to bend; the
    // plug-in meters the input and output itself, from the block's buffers).
    void takeBend (float out[2])
    {
        out[0] = distPeak[0], out[1] = distPeak[1];
        distPeak[0] = distPeak[1] = 0.0f;
    }

private:
    float rate = 48000.0f;
    const float silence[controlFrames] {};
    Store store;
    Sequencer seq;

    // The knobs.
    float target[3] = { 0.0f, 0.6f, 0.3f }, emphasisTarget = 0.0f, glideKnob = 0.4f;
    float driveDb = 0.0f, mixTarget = 1.0f, outputDb = 0.0f, spreadAmount = 0.0f;
    int syncIndex = 0;
    float lfoRateKnob = 0.4f, phaseOffset = 0.25f, lfoDepth[3] = {}, envGainDb = 0.0f, attackKnob = 0.5f, releaseKnob = 0.5f, envDepth[3] = {};
    Shape shape = Shape::sine;
    // The sequencer's: Filter, Sequence, the clocks (Inc, Dec, Reset: an index into their lengths), the envelope's
    // trigger, and the Inc / Dec / Reset gates.
    int manualSlot = 1, sequenceParam = 0, clock[3] = { 7, 0, 0 }, envTrigMode = 0;
    float trigLevelDb = -18.0f;
    bool gateHigh[3] = {};

    // What they come to.
    float glideCoef = 1.0f, smoothCoef = 1.0f, tickSmooth = 1.0f, lfoStep = 0.0f;
    float driveTarget = 1.0f, compTarget = 1.0f, outTarget = 1.0f, envGainLin = 1.0f, attackCoef = 1.0f, releaseCoef = 1.0f;

    // The running state. A new filter comes in under a dip of the filtered sound: down over the old filter's Exit
    // Fade (switchDir -1), the filter changed and emptied, up over the new one's Enter Fade (+1), each scaled by the
    // sequence's Fade Time Scale.
    int cubeIndex = 0, loadedSlot = 1, switchDir = 0;
    float switchGain = 1.0f, switchStep = 1.0f;
    float spreadNow = 0.0f, lfoDepthNow[3] = {}, envDepthNow[3] = {}, envGainNow = 1.0f; // glided as Emphasis is
    float knob[3] = { 0.0f, 0.6f, 0.3f }, emphasisNow = 0.0f;
    float mixNow = 1.0f, outNow = 1.0f, driveNow = 1.0f, compNow = 1.0f;
    float env = 0.0f, envNorm = 0.0f;
    double lfoCycles = 0.0;
    bool songPlaying = false, startPending = false, loadPending = true;
    double anchorBeats = 0.0, beatsPerFrame = 120.0 / 60.0 / 48000.0;
    uint64_t frame = 0, anchorFrame = 0; // frames rendered since activation; the one the song's place was taken at
    uint32_t untilTick = 0;
    double songBeatsAt (uint64_t at) const { return anchorBeats + (double) (at - anchorFrame) * beatsPerFrame; }
    bool driving = false, distorting = false, splitNow = false, designed = false;
    Position pointL, pointR, designedL, designedR;
    int designedCube = -2;
    float designedEmphasis = 0.0f;
    Design designL, designR;
    int bandsNow = 0;
    bool pairedSplit = false; // pairs[] hold two designs (Spread) or one twice
    PairCoefs pairs[maxBands] {};
    Pair wetPair {}, dryPair {}, distDrive {}, distPost {}, distOn {};
    PairState state[maxBands] {};

    // The filter heard: its settings now (gliding to the bank's over 20 ms while it plays; landing at once when it
    // comes in).
    bool snapFilter = true, transformDist = false;
    float offsetNow[3] = {}, gainNow[3] = { 1.0f, 1.0f, 1.0f }, levelDbNow[2] = {}, distDbNow[2] = { distLowest, distLowest };
    float offsetTo[3] = {}, gainTo[3] = { 1.0f, 1.0f, 1.0f }, levelTo[2] = {}, distTo[2] = { distLowest, distLowest };
    int readSlot = -1;
    uint32_t readChanges = 0;
    bool filterSettled = false;
    // What the gains and the distortion were last worked out for.
    float levelsUsed[2] = { -1.0f, -1.0f }, distUsed[2] = { 1e9f, 1e9f };
    int distCube = -3;
    float controlNow[3] = {};

    // The sequencer's triggers: waiting (Inc, Dec, Reset), the wheel, each clock's length in ticks and where its grid
    // was, the song's place then, the envelope's arm.
    uint32_t pending[3] = {}, triggers = 0;
    int pendingBrowse = 0;
    int64_t clockTicks[3] = { clockTicksPerBeat, 0, 0 }, clockCell[3] = {};
    double clockPlace = 0.0;
    bool clockKnown[3] = {}, envArmed = true;
    // After a gap, at most this many of the clocks' boundaries are replayed one by one (replay()).
    static constexpr int64_t maxReplay = 8 * maxSteps;

    // The distortion's meter.
    float distPeak[2] = {};

    static float onePole (float seconds, float frames, float rate) { return seconds <= 0.0f ? 1.0f : 1.0f - std::exp (-frames / (seconds * rate)); }
    template <size_t N>
    static int clockIndexOf (float v, const double (&)[N]) { return std::clamp ((int) v, 0, (int) N - 1); }

    void derive()
    {
        glideCoef = onePole (glideSeconds (glideKnob), (float) controlFrames, rate);
        smoothCoef = onePole (0.02f, 1.0f, rate);                   // Drive, Mix, Output: 20 ms
        tickSmooth = onePole (0.02f, (float) controlFrames, rate);  // Emphasis
        lfoStep = lfoHz (lfoRateKnob) * (float) controlFrames / rate;
        driveTarget = std::exp2 (driveDb * dbToLog2);
        compTarget = 1.0f / std::sqrt (driveTarget);
        outTarget = std::exp2 (outputDb * dbToLog2);
        envGainLin = std::exp2 (envGainDb * dbToLog2);
        attackCoef = onePole (attackSeconds (attackKnob), 1.0f, rate);
        releaseCoef = onePole (releaseSeconds (releaseKnob), 1.0f, rate);
    }

    void setClock (int c, int index)
    {
        clock[c] = index, clockKnown[c] = false;
        clockTicks[c] = std::llround ((c < 2 ? clockBeats[index] : resetBeats[index]) * (double) clockTicksPerBeat);
    }

    void gate (int which, float v)
    {
        const bool high = v >= 0.5f;
        if (high && ! gateHigh[which])
            ++pending[which];
        gateHigh[which] = high;
    }

    // The sequence from its first step, nothing waiting, the clocks to be found again.
    void restartSequence()
    {
        seq.load (store, sequenceParam);
        loadPending = startPending = false;
        pending[0] = pending[1] = pending[2] = 0, pendingBrowse = 0;
        clockKnown[0] = clockKnown[1] = clockKnown[2] = false;
        envArmed = true;
    }

    // The filter to hear: the sequence's while it runs and has loaded one, else the Filter parameter's.
    int targetSlot() const { return seq.on (store) && seq.filter > 0 ? seq.filter : manualSlot; }

    // A glide's step; it lands on its value once close (or once a step is too small to move it in a float).
    static float glideTo (float now, float to, float coef)
    {
        const float next = now + (to - now) * coef;
        return next == now || std::fabs (to - next) < 1e-5f ? to : next;
    }

    // A fade's step per frame: the filter's fade (Enter or Exit) times the sequence's scale.
    float fadeStep (int slot, int field) const
    {
        const float seconds = fadeSeconds (store.field (slot, field)) * fadeScales[seq.fade];
        return 1.0f / (seconds * rate);
    }

    // The sequencer's control step: a sequence (re)loaded, then each trigger since the last step: the clocks
    // (runClocks()), the envelope rising through Trig Level, the gates and the page; Inc first, then Dec, then Reset,
    // so a Reset on the same beat as an Inc leaves the sequence at its first step.
    void stepSequence()
    {
        if (loadPending || startPending)
            restartSequence();
        if (sequenceParam == 0 && (pending[0] | pending[1] | pending[2]) == 0 && pendingBrowse == 0)
            return; // off: nothing to move (the clocks are found again when a sequence is loaded)
        uint32_t fire[3] = { pending[0], pending[1], pending[2] };
        pending[0] = pending[1] = pending[2] = 0;
        const bool on = seq.on (store);
        if (on && songPlaying)
            runClocks (fire);
        else // no sequence or no song: nothing to move (the clocks are found again when one runs)
            clockKnown[0] = clockKnown[1] = clockKnown[2] = false;
        if (envTrigMode != 0 && on)
        {
            const float level = (trigLevelDb - envFloorDb) / -envFloorDb, again = std::max (0.0f, level - trigHysteresisDb / -envFloorDb);
            if (envArmed && envNorm > level)
                ++fire[envTrigMode - 1], envArmed = false;
            else if (! envArmed && envNorm <= again)
                envArmed = true;
        }
        for (uint32_t i = 0; i < fire[0]; ++i)
            seq.inc (store);
        for (uint32_t i = 0; i < fire[1]; ++i)
            seq.dec (store);
        for (uint32_t i = 0; i < fire[2]; ++i)
            seq.reset (store);
        if (pendingBrowse != 0)
            seq.browse (store, pendingBrowse), pendingBrowse = 0, ++triggers;
        triggers += fire[0] + fire[1] + fire[2];
    }

    // The clocks at a control step: the boundaries of each one's grid (the multiples of its length) the song passed
    // since the last step, added to `fire`. Playing on, a step passes one at most (those of different clocks that
    // fall together, together). After a gap (the host let the sound sleep while the song played on) or a jump
    // forward it passes many: they are replayed in the order they came, so the sequence is where it would be had it
    // run throughout. A jump back fires a clock once if the song landed on one of its boundaries (a loop's start).
    void runClocks (uint32_t fire[3])
    {
        const double place = songBeatsAt (frame);
        const double lengths[3] = { clockBeats[clock[0]], clockBeats[clock[1]], resetBeats[clock[2]] };
        const bool back = place < clockPlace;
        clockPlace = place;
        int64_t from[3] = {}, to[3] = {};
        for (int c = 0; c < 3; ++c)
        {
            if (lengths[c] <= 0.0)
            {
                clockKnown[c] = false;
                continue;
            }
            const int64_t cell = (int64_t) std::floor (place / lengths[c] + 1e-9);
            if (clockKnown[c] && back) // the boundary at or after where it landed (anchorBeats), if it has passed it
                fire[c] += (double) cell >= anchorBeats / lengths[c] - 1e-9 ? 1u : 0u;
            else if (clockKnown[c])
                from[c] = clockCell[c], to[c] = cell;
            clockCell[c] = cell, clockKnown[c] = true;
        }
        if (! back)
            replay (from, to, fire);
    }

    // Each clock's boundaries after cell from[c] up to cell to[c], in the order they came (at one they share: Inc,
    // then Dec, then Reset); the last of them (one boundary, or several together) is left in `fire` with the step's
    // other triggers. More than maxReplay (a long gap) are cut, so the work stays bounded: first to a Reset boundary
    // passed (the one before the last if the rest fits, else the last; the Reset there takes the sequence to its
    // first step), then by whole laps of the Inc and Dec clocks' pattern (`length` repeats of it: the steps skipped
    // come to whole laps of the sequence). That is exact for one clock alone, and for both on a sequence without
    // Pauses, Halts or Bumpers; with them the walk ends against them as the whole replay's does once it has settled.
    // A single lap of more than maxReplay (an Inc of 1/32 with a Dec of bars): only the last maxReplay are replayed.
    void replay (int64_t from[3], const int64_t to[3], uint32_t fire[3])
    {
        const int64_t* ticks = clockTicks;
        auto count = [&] {
            int64_t n = 0;
            for (int c = 0; c < 3; ++c)
                n += to[c] - from[c];
            return n;
        };
        auto skipTo = [&] (int64_t at, int64_t* into) { // into[] for Inc and Dec: none of their boundaries up to tick `at`
            for (int c = 0; c < 2; ++c)
                if (to[c] > from[c])
                    into[c] = std::min (to[c], std::max (from[c], floorDiv (at, ticks[c])));
        };
        if (count() == 0)
            return; // most steps: no boundary
        if (count() > maxReplay && to[2] > from[2])
        {
            int64_t r = to[2] - 1 > from[2] ? to[2] - 1 : to[2], start[2] = { from[0], from[1] };
            skipTo (r * ticks[2], start);
            if (r < to[2] && (to[0] - start[0]) + (to[1] - start[1]) + (to[2] - r + 1) > maxReplay)
                r = to[2];
            skipTo (r * ticks[2], from); // the Inc and Dec at the Reset's own boundary come before it: skipped too
            from[2] = r - 1;
        }
        const int length = count() > maxReplay ? store.length (seq.sequence) : 0;
        const bool incs = to[0] > from[0], decs = to[1] > from[1];
        if (length > 0 && (incs || decs))
        {
            const int64_t span = incs && decs ? ticks[0] / std::gcd (ticks[0], ticks[1]) * ticks[1] : ticks[incs ? 0 : 1];
            const int64_t lap = span * length, perLap = (incs ? lap / ticks[0] : 0) + (decs ? lap / ticks[1] : 0);
            if (perLap <= maxReplay)
            {
                const int64_t laps = (count() - maxReplay + perLap - 1) / perLap;
                for (int c = 0; c < 2; ++c)
                    if (to[c] > from[c])
                        from[c] = std::min (to[c], from[c] + laps * (lap / ticks[c]));
            }
            else
            {
                const int64_t end = std::max (incs ? to[0] * ticks[0] : 0, decs ? to[1] * ticks[1] : 0);
                skipTo (end - maxReplay * ticks[0] * ticks[1] / (ticks[0] + ticks[1]), from); // both clocks: one alone laps sooner
            }
        }
        for (;;)
        {
            int64_t at = INT64_MAX;
            for (int c = 0; c < 3; ++c)
                if (from[c] < to[c])
                    at = std::min (at, (from[c] + 1) * ticks[c]);
            if (at == INT64_MAX)
                return;
            bool now[3], last = true;
            for (int c = 0; c < 3; ++c)
            {
                now[c] = from[c] < to[c] && (from[c] + 1) * ticks[c] == at;
                from[c] += now[c] ? 1 : 0;
                last &= from[c] == to[c];
            }
            if (last)
            {
                for (int c = 0; c < 3; ++c)
                    fire[c] += now[c] ? 1u : 0u;
                return;
            }
            if (now[0])
                seq.inc (store);
            if (now[1])
                seq.dec (store);
            if (now[2])
                seq.reset (store);
            triggers += (now[0] ? 1u : 0u) + (now[1] ? 1u : 0u) + (now[2] ? 1u : 0u);
        }
    }

    // The control step: the sequencer, the LFO, the envelope, the glides, the filter heard and its settings, the
    // point each channel is at and its filter.
    void tick()
    {
        untilTick = controlFrames;

        // The envelope, from -48 dB to 0 dB (read only if something uses it). A broken input (not a number,
        // infinite) has it start again from nothing.
        if (! (env < 1e30f))
            env = 0.0f;
        envNorm = envDepthNow[0] != 0.0f || envDepthNow[1] != 0.0f || envDepthNow[2] != 0.0f || envDepth[0] != 0.0f || envDepth[1] != 0.0f
                          || envDepth[2] != 0.0f || envTrigMode != 0
                    ? std::clamp ((6.0206f * std::log2 (env + 1e-12f) - envFloorDb) / -envFloorDb, 0.0f, 1.0f)
                    : 0.0f;
        stepSequence();

        // The LFO's place now: the song's while it plays (synced), else counted on.
        const double beatsLong = lfoSyncBeats[syncIndex];
        if (beatsLong > 0.0 && songPlaying)
            lfoCycles = songBeatsAt (frame) / beatsLong;
        // The depths, Spread and Env Gain glide (a knob turned or a preset loaded moves the point smoothly).
        for (int a = 0; a < 3; ++a)
            lfoDepthNow[a] = glideTo (lfoDepthNow[a], lfoDepth[a], tickSmooth), envDepthNow[a] = glideTo (envDepthNow[a], envDepth[a], tickSmooth);
        spreadNow = glideTo (spreadNow, spreadAmount, tickSmooth);
        envGainNow = glideTo (envGainNow, envGainLin, tickSmooth);
        float lfo[3] = {};
        for (uint32_t a = 0; a < 3; ++a)
            if (lfoDepthNow[a] != 0.0f) // an axis the LFO does not move needs no value
                lfo[a] = lfoValue (shape, lfoCycles + (double) (phaseOffset * (float) a), a);
        lfoCycles += beatsLong > 0.0 ? (double) controlFrames * beatsPerFrame / beatsLong : (double) lfoStep;
        if (lfoCycles > 1e9) // a free LFO left running for years: keep the double's precision
            lfoCycles -= std::floor (lfoCycles);

        // A new filter (or a new cube in the one heard): the dip down, then the change at the bottom of it (the filter
        // emptied, so nothing rings over from the old one), then up. Before anything is heard it changes at once.
        const int slot = targetSlot(), cube = store.cubeOf (slot);
        if (slot != loadedSlot || cube != cubeIndex)
        {
            if (! designed || switchGain <= 0.0f)
            {
                loadedSlot = slot, cubeIndex = cube;
                for (auto& st : state)
                    st = PairState {};
                snapFilter = true;
                switchDir = designed ? 1 : 0;
                switchStep = fadeStep (loadedSlot, fieldEnter);
            }
            else if (switchDir >= 0)
                switchDir = -1, switchStep = fadeStep (loadedSlot, fieldExit);
        }
        else if (switchDir < 0)
            switchDir = 1, switchStep = fadeStep (loadedSlot, fieldEnter); // back to the filter it was leaving
        if (switchDir > 0 && switchGain >= 1.0f)
            switchDir = 0, switchGain = 1.0f;

        // The filter's settings: its offsets and gains, its levels and distortion (read again only when the bank
        // changed or another filter came in; glided only until they land).
        if (snapFilter || loadedSlot != readSlot || store.changes() != readChanges)
        {
            readSlot = loadedSlot, readChanges = store.changes();
            for (int a = 0; a < 3; ++a)
                offsetTo[a] = store.field (loadedSlot, fieldMorphOffset + a) / 100.0f, gainTo[a] = store.field (loadedSlot, fieldMorphGain + a) / 10.0f;
            const bool used = store.used (loadedSlot);
            for (int c = 0; c < 2; ++c)
                levelTo[c] = used ? store.field (loadedSlot, fieldGainLeft + c) : 0.0f, distTo[c] = used ? store.field (loadedSlot, fieldDistLeft + c) : distLowest;
            transformDist = used && store.field (loadedSlot, fieldTransformDist) >= 0.5f;
            filterSettled = false;
        }
        if (! filterSettled)
        {
            const float s = snapFilter ? 1.0f : tickSmooth;
            bool landed = true;
            for (int a = 0; a < 3; ++a)
            {
                offsetNow[a] = glideTo (offsetNow[a], offsetTo[a], s), gainNow[a] = glideTo (gainNow[a], gainTo[a], s);
                landed &= offsetNow[a] == offsetTo[a] && gainNow[a] == gainTo[a];
            }
            for (int c = 0; c < 2; ++c)
            {
                levelDbNow[c] = glideTo (levelDbNow[c], levelTo[c], s), distDbNow[c] = glideTo (distDbNow[c], distTo[c], s);
                landed &= levelDbNow[c] == levelTo[c] && distDbNow[c] == distTo[c];
            }
            filterSettled = landed;
        }
        snapFilter = false;

        // The knobs' glides, then the point: the knobs and modulation on each axis, then the filter's gain and
        // offset (Transform's to the distortion when the filter says so: its point then stays at its offset).
        Position p;
        float* axes[3] = { &p.x, &p.y, &p.z };
        float distBy = 0.0f;
        for (int a = 0; a < 3; ++a)
        {
            knob[a] = glideTo (knob[a], target[a], glideCoef);
            const float v = knob[a] + lfoSwing * lfoDepthNow[a] * lfo[a] + envSwing * envDepthNow[a] * envNorm;
            controlNow[a] = std::clamp (2.0f * v - 1.0f, -1.0f, 1.0f);
            if (a == 2 && transformDist)
            {
                distBy = transformDistDb * gainNow[2] * (2.0f * v - 1.0f);
                *axes[a] = std::clamp (0.5f + 0.5f * offsetNow[2], 0.0f, 1.0f);
            }
            else
                *axes[a] = axisPoint (v, gainNow[a], offsetNow[a]);
        }
        emphasisNow = glideTo (emphasisNow, emphasisTarget, tickSmooth);
        Position l = p, r = p;
        splitNow = spreadNow > 0.0f;
        if (splitNow)
        {
            const float half = 0.5f * spreadWidth * spreadNow, centre = std::clamp (p.x, half, 1.0f - half);
            l.x = centre - half, r.x = centre + half;
        }
        else
            designedR = { -1.0f, -1.0f, -1.0f }; // unused: worked out again when Spread splits the sides
        pointL = l, pointR = r;

        const bool again = ! designed || designedCube != cubeIndex || designedEmphasis != emphasisNow;
        bool changed = again || splitNow != pairedSplit;
        if (cubeIndex < 0) // Null: no bands, the input as it is
        {
            if (again)
            {
                designL = Design {};
                designL.bands = 0, designL.wet = 1.0f, designL.dryGain = 0.0f;
                designR = designL;
            }
            designedL = l, designedR = r;
        }
        else
        {
            const CubeTable& cubeNow = cubeTable (cubeIndex);
            if (again || l != designedL)
                design (cubeNow, l, emphasisNow, rate, designL), designedL = l, changed = true;
            if (splitNow && (again || r != designedR))
                design (cubeNow, r, emphasisNow, rate, designR), designedR = r, changed = true;
        }
        if (again && designedCube != cubeIndex)
            for (int b = designL.bands; b < maxBands; ++b) // bands a new cube does not use start empty if it later does
                state[b] = PairState {};
        designed = true, designedCube = cubeIndex, designedEmphasis = emphasisNow;
        const Design& dr = splitNow ? designR : designL;
        bandsNow = designL.bands;
        pairedSplit = splitNow;
        for (int b = 0; changed && b < bandsNow; ++b) // the two sides' coefficients side by side, when they moved
        {
            const BandCoefs &cl = designL.band[b], &cr = dr.band[b];
            pairs[b] = { Pair { cl.a1, cr.a1 }, Pair { cl.a2, cr.a2 }, Pair { cl.a3, cr.a3 }, Pair { cl.m0, cr.m0 }, Pair { cl.m1, cr.m1 },
                         Pair { cl.m2, cr.m2 } };
        }
        // The filter's level on each side scales its output (the bands' and its parallel dry share alike).
        if (changed || levelDbNow[0] != levelsUsed[0] || levelDbNow[1] != levelsUsed[1])
        {
            const float gl = levelDbNow[0] == 0.0f ? 1.0f : std::exp2 (levelDbNow[0] * dbToLog2);
            const float gr = levelDbNow[1] == 0.0f ? 1.0f : std::exp2 (levelDbNow[1] * dbToLog2);
            wetPair = Pair { designL.wet * gl, dr.wet * gr }, dryPair = Pair { designL.dryGain * gl, dr.dryGain * gr };
            levelsUsed[0] = levelDbNow[0], levelsUsed[1] = levelDbNow[1];
        }

        // The distortion inside the filter: the bands' input scaled by the drive, each band's output through a
        // saturator (its ceiling full scale), the last band's output scaled back. Under 0 dB that only sets the
        // headroom (scaled back as much: clean until the signal inside reaches 1 / drive); over 0 dB it drives into the
        // ceilings, scaled back by half as much (as Drive is), so more of it is grittier and a little louder, never as
        // much louder as the drive. At -30 dB there is none.
        const float dbL = std::clamp (distDbNow[0] + distBy, distLowest, distHighest), dbR = std::clamp (distDbNow[1] + distBy, distLowest, distHighest);
        if (dbL != distUsed[0] || dbR != distUsed[1] || cubeIndex != distCube)
        {
            float drive[2], post[2], on[2];
            distorting = false;
            for (int c = 0; c < 2; ++c)
            {
                const float db = c == 0 ? dbL : dbR;
                on[c] = db > distLowest && cubeIndex >= 0 ? 1.0f : 0.0f; // a side at -30 dB passes it all by, untouched
                drive[c] = on[c] != 0.0f ? std::exp2 (db * dbToLog2) : 1.0f;
                post[c] = drive[c] < 1.0f ? 1.0f / drive[c] : (on[c] != 0.0f ? 1.0f / std::sqrt (drive[c]) : 1.0f);
                distorting |= on[c] != 0.0f;
            }
            distDrive = Pair { drive[0], drive[1] }, distPost = Pair { post[0], post[1] }, distOn = Pair { on[0], on[1] };
            distUsed[0] = dbL, distUsed[1] = dbR, distCube = cubeIndex;
        }

        // Drive and the gains: settled ones snap to their value (so 0 dB Drive is no saturator at all). A gain's
        // per-sample glide stalls a few 1e-5 short of its value, where its steps no longer move a float.
        for (auto [now, to] : { std::pair<float*, float> { &mixNow, mixTarget }, { &outNow, outTarget }, { &driveNow, driveTarget }, { &compNow, compTarget } })
            if (std::fabs (*now - to) <= 3e-4f * std::fabs (to) + 1e-6f)
                *now = to;
        driving = driveNow != 1.0f || driveTarget != 1.0f;

        // A filter that blew up (it should not): emptied.
        for (int b = 0; b < bandsNow; ++b)
            if (! std::isfinite (state[b].ic1[0] + state[b].ic1[1] + state[b].ic2[0] + state[b].ic2[1]))
            {
                for (auto& st : state)
                    st = PairState {};
                break;
            }
    }

    // A control step's frames, the bands' count fixed so the loop over them unrolls and their states stay in registers.
    template <bool drive, bool dist>
    void runBands (const float* inL, const float* inR, float* outL, float* outR, uint32_t n)
    {
        switch (bandsNow)
        {
            case 1: runFrames<drive, dist, 1> (inL, inR, outL, outR, n); break;
            case 2: runFrames<drive, dist, 2> (inL, inR, outL, outR, n); break;
            case 3: runFrames<drive, dist, 3> (inL, inR, outL, outR, n); break;
            case 4: runFrames<drive, dist, 4> (inL, inR, outL, outR, n); break;
            case 5: runFrames<drive, dist, 5> (inL, inR, outL, outR, n); break;
            case 6: runFrames<drive, dist, 6> (inL, inR, outL, outR, n); break;
            case 7: runFrames<drive, dist, 7> (inL, inR, outL, outR, n); break;
            default: runFrames<drive, false, 0> (inL, inR, outL, outR, n); break;
        }
        static_assert (maxBands == 7, "a case a band count");
    }

    // The frames: the envelope, the gains' glides, Drive, the bands (both sides at once, morph.h's runBand on each
    // lane, each through the distortion when it is on), Mix, Output and the limit. The state is copied into locals for
    // the step so nothing is reloaded after each store to the output (which may be the input).
    template <bool drive, bool dist, int N>
    void runFrames (const float* inL, const float* inR, float* outL, float* outR, uint32_t n)
    {
        PairCoefs c[N > 0 ? N : 1];
        PairState s[N > 0 ? N : 1];
        for (int b = 0; b < N; ++b)
            c[b] = pairs[b], s[b] = state[b];
        const Pair wet = wetPair, dryGain = dryPair, dd = distDrive, dp = distPost, dOn = distOn;
        const float eg = envGainNow, attack = attackCoef, release = releaseCoef, smooth = smoothCoef;
        const float switchBy = (float) switchDir * switchStep;
        float sw = switchGain;
        const float mixTo = mixTarget, outTo = outTarget, driveTo = driveTarget, compTo = compTarget;
        float follow = env, mixAt = mixNow, outAt = outNow, driveAt = driveNow, compAt = compNow;
        Pair bent { distPeak[0], distPeak[1] };
        for (uint32_t i = 0; i < n; ++i)
        {
            const float xl = inL[i], xr = inR[i];
            // The envelope follows the louder side.
            const float level = std::max (std::fabs (xl), std::fabs (xr)) * eg;
            follow += (level > follow ? attack : release) * (level - follow);
            mixAt += (mixTo - mixAt) * smooth;
            outAt += (outTo - outAt) * smooth;
            const Pair x { xl, xr };
            Pair in = x;
            if (drive)
            {
                // The saturator blends in over Drive's first 6 dB (its gain from 1 to 2), so Drive at 0 dB is the
                // input itself and turning it up from there is smooth.
                driveAt += (driveTo - driveAt) * smooth;
                compAt += (compTo - compAt) * smooth;
                const float amount = std::min (driveAt - 1.0f, 1.0f);
                in = x + amount * (Pair { saturate (xl * driveAt) * compAt, saturate (xr * driveAt) * compAt } - x);
            }
            Pair y = dist ? in * dd : in;
            for (int b = 0; b < N; ++b)
            {
                const Pair v3 = y - s[b].ic2;
                const Pair v1 = c[b].a1 * s[b].ic1 + c[b].a2 * v3;
                const Pair v2 = s[b].ic2 + c[b].a2 * s[b].ic1 + c[b].a3 * v3;
                s[b].ic1 = 2.0f * v1 - s[b].ic1;
                s[b].ic2 = 2.0f * v2 - s[b].ic2;
                y = c[b].m0 * y + c[b].m1 * v1 + c[b].m2 * v2;
                if (dist)
                {
                    const Pair a = y < 0.0f ? -y : y, bentBy = a * dOn;
                    bent = bentBy > bent ? bentBy : bent;
                    y = dOn > 0.5f ? saturatePair (y) : y;
                }
            }
            if (dist)
                y = y * dp;
            sw = std::clamp (sw + switchBy, 0.0f, 1.0f);
            y = (wet * y + dryGain * in) * sw;
            const Pair o = (x * (1.0f - mixAt) + y * mixAt) * outAt;
            outL[i] = limit (o[0]);
            if (outR != nullptr)
                outR[i] = limit (o[1]);
        }
        for (int b = 0; b < N; ++b)
            state[b] = s[b];
        env = follow, mixNow = mixAt, outNow = outAt, driveNow = driveAt, compNow = compAt, switchGain = sw;
        distPeak[0] = bent[0], distPeak[1] = bent[1];
    }
};
} // namespace cortex
