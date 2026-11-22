// Cortex Filter's filters and sequences, after the way the Morpheus-style filter modules work: a filter is a cube and
// the settings that go with it, kept in a bank; a sequence is a list of steps, each a filter or a command, that the
// sequencer moves through on Inc, Dec and Reset triggers.
//
// A filter (one of 64, F01..F64): its cube (Null: no filtering), and for each axis an offset (where the knobs' middle
// sits on the axis: +100 % puts it at the axis's end) and a gain (how far the knobs and their modulation reach: -9.9x
// to +9.9x, negative turns them round), the times it fades out (Exit) and in (Enter) when it hands over to another
// filter, the gain from input to output of each side (-40 to +40 dB) and the distortion inside each side's filter
// (-30 dB, none, to +70 dB), and Transform > Dist, which turns the Transform knob and its modulation over to that
// distortion (Transform then sits where its offset puts it). An empty filter is heard as Null.
//
// A sequence (one of 16, up to 32 steps): each step a filter or a command. Pause and Halt stop Inc and Dec from moving
// on (a Reset ends a Pause and moves nothing; out of a Halt it goes back to the first step); a Bumper is a wall that
// Inc and Dec cannot cross; Fade x1/32 .. x8 scales the fades of every change after it. Inc and Dec move circularly,
// a step at a time, loading the filter there; Reset goes back to the first step.
//
// Threads: the bank and the sequences are written by the main thread (the page, a state loaded) and, for the edit
// filter's fields, by whichever thread a host's parameter change arrives on; the audio thread reads them. Every value
// is an atomic of its own, read and written relaxed: a change lands within a block, and an edit's halfway state lasts
// no longer than that. Names are the main thread's alone.
#pragma once

#include "params.h"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace cortex
{
// A filter's settings, in the order of the edit filter's parameters (ParamId::editCube + field).
enum FilterField : int
{
    fieldCube, fieldMorphOffset, fieldFreqOffset, fieldTransformOffset, fieldMorphGain, fieldFreqGain, fieldTransformGain,
    fieldEnter, fieldExit, fieldGainLeft, fieldGainRight, fieldDistLeft, fieldDistRight, fieldTransformDist, fieldCount
};
static_assert (ParamId::editCube + fieldCount - 1 == ParamId::transformDist, "a field a parameter");
inline uint32_t fieldParam (int field) { return ParamId::editCube + (uint32_t) field; }
inline const Param& fieldInfo (int field) { return paramTable[paramIndex (fieldParam (field))]; }

constexpr int sequenceCount = 16, maxSteps = 32;
constexpr int nameLength = 12; // as the module's: names of up to 12 characters

// A step: 1..64 a filter, or a command.
enum StepCode : uint8_t
{
    stepPause = 100, stepHalt = 101, stepBumper = 102,
    stepFade = 110, // + 0..8: x1/32, x1/16, x1/8, x1/4, x1/2, x1, x2, x4, x8
};
constexpr int fadeScaleCount = 9, unityFade = 5;
constexpr float fadeScales[fadeScaleCount] = { 1.0f / 32.0f, 1.0f / 16.0f, 1.0f / 8.0f, 0.25f, 0.5f, 1.0f, 2.0f, 4.0f, 8.0f };
inline const char* const fadeScaleNames[fadeScaleCount] = { "x1/32", "x1/16", "x1/8", "x1/4", "x1/2", "x1", "x2", "x4", "x8" };
inline bool isFilterStep (int code) { return code >= 1 && code <= slotCount; }
inline bool isFadeStep (int code) { return code >= stepFade && code < stepFade + fadeScaleCount; }

// --- The factory's filters and sequences ------------------------------------------------------------------------

// One factory filter: F01..F16 one for each cube, as it is; F17..F32 ways of using them. Offsets in %, gains in tenths
// (10 = 1.0x), fades in seconds, levels in dB.
struct FactoryFilter
{
    const char* name; // nullptr: named after its cube
    int cube;         // 0 Null, 1..16 cubes.h's
    int offset[3];    // Morph, Freq, Transform
    int gain[3];
    float enter, exit;
    int gainLeft, gainRight, distLeft, distRight;
    int transformDist;
};
constexpr float fadeDefaultSeconds = 0.003f;
constexpr FactoryFilter plain (int cube)
{
    return { nullptr, cube, { 0, 0, 0 }, { 10, 10, 10 }, fadeDefaultSeconds, fadeDefaultSeconds, 0, 0, -30, -30, 0 };
}
inline constexpr FactoryFilter factoryFilters[] = {
    plain (1), plain (2), plain (3), plain (4), plain (5), plain (6), plain (7), plain (8),
    plain (9), plain (10), plain (11), plain (12), plain (13), plain (14), plain (15), plain (16),
    // 17-32.
    { "Deep Acid", 10, { 0, -30, 0 }, { 10, 10, 10 }, 0.003f, 0.003f, -4, -4, 6, 6, 0 },
    { "Bright Acid", 10, { 0, 30, 0 }, { 10, 10, 10 }, 0.003f, 0.003f, 1, 1, -30, -30, 0 },
    { "Vowel Crush", 3, { 0, 0, 0 }, { 10, 10, 10 }, 0.003f, 0.003f, 0, 0, -6, -6, 1 },
    { "Fuzz Wah", 7, { 0, 0, 0 }, { 10, 10, 10 }, 0.003f, 0.003f, -2, -2, 12, 12, 0 },
    { "Wide Sweep", 1, { 0, 0, 0 }, { 10, 20, 10 }, 0.003f, 0.003f, 0, 0, -30, -30, 0 },
    { "Narrow Band", 2, { 60, 0, 0 }, { 10, 10, 10 }, 0.003f, 0.003f, -2, -2, -30, -30, 0 },
    { "Bell Crush", 8, { 0, 0, 0 }, { 10, 10, 10 }, 0.003f, 0.003f, 0, 0, -6, -6, 1 },
    { "Phase Invert", 5, { 0, 0, 0 }, { -10, 10, 10 }, 0.003f, 0.003f, 0, 0, -30, -30, 0 },
    { "Slow Chords", 16, { 0, 0, 0 }, { 10, 10, 10 }, 0.25f, 0.25f, 0, 0, -30, -30, 0 },
    { "Slow Vowels", 4, { 0, 0, 0 }, { 10, 10, 10 }, 0.25f, 0.25f, 0, 0, -30, -30, 0 },
    { "Gate Tube", 11, { 0, 0, 0 }, { 10, 10, 10 }, 0.001f, 0.08f, 0, 0, -30, -30, 0 },
    { "Telephone", 9, { 100, 0, 0 }, { 10, 10, 10 }, 0.003f, 0.003f, 2, 2, -30, -30, 0 },
    { "Sub Crush", 15, { 0, 0, 0 }, { 10, 10, 10 }, 0.003f, 0.003f, 2, 2, 18, 18, 0 },
    { "High Metal", 13, { 0, 40, 0 }, { 10, 10, 10 }, 0.003f, 0.003f, 0, 0, -30, -30, 0 },
    { "Split Swirl", 12, { 0, 0, 0 }, { 10, 10, 10 }, 0.003f, 0.003f, 0, -2, -30, 6, 0 },
    { "Bypass", 0, { 0, 0, 0 }, { 10, 10, 10 }, 0.003f, 0.003f, 0, 0, -30, -30, 0 },
};
constexpr int factoryFilterCount = (int) (sizeof (factoryFilters) / sizeof (factoryFilters[0]));
static_assert (factoryFilterCount <= slotCount, "the factory's filters fit the bank");

// The factory's sequences, Sequence's choices 1..8: four that sort the filters by what they do (to pick from with the
// wheel), four to play.
struct FactorySequence
{
    uint8_t steps[maxSteps];
    int length;
};
inline constexpr FactorySequence factorySequences[] = {
    { { 1, 2, 10, 17, 18, 21, 22 }, 7 },                       // Lowpasses
    { { 3, 4, 7, 19, 20, 26 }, 6 },                            // Vowels
    { { 6, 8, 13, 16, 23, 25, 30 }, 7 },                       // Resonators
    { { 5, 9, 11, 12, 14, 15, 24, 27, 28, 29, 31, 32 }, 12 },  // Tone Shapers
    { { 17, 10, 18, 10, 17, 17, 18, 10 }, 8 },                 // Acid Steps
    { { stepFade + 3, 26, 3, 4, 26, 19, 7 }, 7 },              // Vowel Talk
    { { 27, 11, 27, 11, stepHalt }, 5 },                       // Bar Gate
    { { stepBumper, 1, 10, 17, 18, 21, stepBumper }, 7 },      // Up And Down
};
constexpr int factorySequenceCount = (int) (sizeof (factorySequences) / sizeof (factorySequences[0]));
static_assert (factorySequenceCount <= sequenceCount, "the factory's sequences fit");
static_assert (sizeof (sequenceChoices) / sizeof (sequenceChoices[0]) == sequenceCount + 1, "a name a sequence, and Off");

// --- The bank and the sequences ---------------------------------------------------------------------------------

class Store
{
public:
    Store() { factory(); }

    // Everything as the factory has it.
    void factory()
    {
        for (int s = 1; s <= slotCount; ++s)
            factorySlot (s);
        for (int q = 1; q <= sequenceCount; ++q)
            factorySequence (q);
    }
    // A filter emptied: every setting at its default, named after its cube, not in use (heard as Null).
    void clearSlot (int s)
    {
        if (! valid (s))
            return;
        Slot& slot = slots[s];
        slot.used.store (false, std::memory_order_relaxed);
        for (int f = 0; f < fieldCount; ++f)
            slot.field[f].store (fieldInfo (f).def, std::memory_order_relaxed);
        slot.name[0] = 0;
        slot.autoName.store (true, std::memory_order_relaxed);
        touch();
    }
    // One filter as the factory has it (a slot past the factory's: empty).
    void factorySlot (int s)
    {
        if (! valid (s))
            return;
        clearSlot (s);
        if (s > factoryFilterCount)
            return;
        Slot& slot = slots[s];
        const FactoryFilter& f = factoryFilters[s - 1];
        auto put = [&] (int field, float v) { slot.field[field].store (clampParam (fieldInfo (field), v), std::memory_order_relaxed); };
        put (fieldCube, (float) f.cube);
        for (int a = 0; a < 3; ++a)
            put (fieldMorphOffset + a, (float) f.offset[a]), put (fieldMorphGain + a, (float) f.gain[a]);
        put (fieldEnter, f.enter == fadeDefaultSeconds ? fadeDefault : fadeKnob (f.enter));
        put (fieldExit, f.exit == fadeDefaultSeconds ? fadeDefault : fadeKnob (f.exit));
        put (fieldGainLeft, (float) f.gainLeft), put (fieldGainRight, (float) f.gainRight);
        put (fieldDistLeft, (float) f.distLeft), put (fieldDistRight, (float) f.distRight);
        put (fieldTransformDist, (float) f.transformDist);
        if (f.name != nullptr)
        {
            std::snprintf (slot.name, sizeof (slot.name), "%s", f.name);
            slot.autoName.store (false, std::memory_order_relaxed);
        }
        slot.used.store (true, std::memory_order_relaxed);
        touch();
    }
    void factorySequence (int q)
    {
        Seq& seq = seqs[q];
        const FactorySequence* f = q <= factorySequenceCount ? &factorySequences[q - 1] : nullptr;
        for (int i = 0; i < maxSteps; ++i)
            seq.step[i].store (f != nullptr && i < f->length ? f->steps[i] : 0, std::memory_order_relaxed);
        seq.length.store ((uint8_t) (f != nullptr ? f->length : 0), std::memory_order_relaxed);
        touch();
    }

    // Filters (1..64).
    static bool valid (int s) { return s >= 1 && s <= slotCount; }
    float field (int s, int f) const { return valid (s) ? slots[s].field[f].load (std::memory_order_relaxed) : fieldInfo (f).def; }
    bool used (int s) const { return valid (s) && slots[s].used.load (std::memory_order_relaxed); }
    // A field set (from either thread): the filter is in use from then on.
    void setField (int s, int f, float v)
    {
        if (! valid (s) || f < 0 || f >= fieldCount)
            return;
        slots[s].field[f].store (clampParam (fieldInfo (f), v), std::memory_order_relaxed);
        slots[s].used.store (true, std::memory_order_relaxed);
        touch();
    }
    // The cube a filter sounds with: an index into cubes.h, or -1 for Null (an empty filter is Null too).
    int cubeOf (int s) const { return used (s) ? (int) field (s, fieldCube) - 1 : -1; }

    // A filter's name (main thread): its own, or its cube's; "--empty--" for an empty one.
    const char* name (int s) const
    {
        if (! valid (s))
            return "";
        if (! used (s))
            return "--empty--";
        if (! slots[s].autoName.load (std::memory_order_relaxed))
            return slots[s].name;
        const int cube = (int) field (s, fieldCube);
        return cubeChoices.names[std::clamp (cube, 0, cubeCount)];
    }
    bool ownName (int s) const { return valid (s) && ! slots[s].autoName.load (std::memory_order_relaxed); }
    void setName (int s, const char* text) // main thread; nullptr or "": named after its cube
    {
        if (! valid (s))
            return;
        const bool own = text != nullptr && text[0] != 0;
        std::snprintf (slots[s].name, sizeof (slots[s].name), "%.*s", nameLength, own ? text : "");
        slots[s].autoName.store (! own, std::memory_order_relaxed);
        touch();
    }
    // A copy of filter `from` into `to` (main thread): its settings and its name.
    void copy (int from, int to)
    {
        if (! valid (from) || ! valid (to) || from == to)
            return;
        for (int f = 0; f < fieldCount; ++f)
            slots[to].field[f].store (field (from, f), std::memory_order_relaxed);
        std::memcpy (slots[to].name, slots[from].name, sizeof (slots[to].name));
        slots[to].autoName.store (slots[from].autoName.load (std::memory_order_relaxed), std::memory_order_relaxed);
        slots[to].used.store (used (from), std::memory_order_relaxed);
        touch();
    }
    // The first empty filter after `s` (round the bank), or 0 if it is full.
    int emptyAfter (int s) const
    {
        for (int k = 1; k <= slotCount; ++k)
        {
            const int t = (s - 1 + k) % slotCount + 1;
            if (! used (t))
                return t;
        }
        return 0;
    }
    // The `n`th used filter from `s` onward (back for n < 0), not round the bank: `s` itself if none is that way.
    int usedFrom (int s, int n) const
    {
        int at = s;
        for (int t = s + (n > 0 ? 1 : -1); n != 0 && valid (t); t += n > 0 ? 1 : -1)
            if (used (t))
                at = t, n += n > 0 ? -1 : 1;
        return at;
    }

    // Sequences (1..16).
    static bool validSeq (int q) { return q >= 1 && q <= sequenceCount; }
    int length (int q) const { return validSeq (q) ? std::min<int> (seqs[q].length.load (std::memory_order_relaxed), maxSteps) : 0; }
    int step (int q, int i) const { return validSeq (q) && i >= 0 && i < maxSteps ? seqs[q].step[i].load (std::memory_order_relaxed) : 0; }
    // Main thread: a step changed, put in before step `i` (a Pause), taken out, or added at the end.
    void setStep (int q, int i, int code)
    {
        if (validSeq (q) && i >= 0 && i < length (q))
            seqs[q].step[i].store ((uint8_t) code, std::memory_order_relaxed), touch();
    }
    bool insertStep (int q, int i, int code)
    {
        const int n = length (q);
        if (! validSeq (q) || n >= maxSteps || i < 0 || i > n)
            return false;
        for (int k = n; k > i; --k)
            seqs[q].step[k].store (seqs[q].step[k - 1].load (std::memory_order_relaxed), std::memory_order_relaxed);
        seqs[q].step[i].store ((uint8_t) code, std::memory_order_relaxed);
        seqs[q].length.store ((uint8_t) (n + 1), std::memory_order_relaxed);
        touch();
        return true;
    }
    bool deleteStep (int q, int i)
    {
        const int n = length (q);
        if (! validSeq (q) || i < 0 || i >= n)
            return false;
        seqs[q].length.store ((uint8_t) (n - 1), std::memory_order_relaxed); // shorter first: the last step is never read twice
        for (int k = i; k + 1 < n; ++k)
            seqs[q].step[k].store (seqs[q].step[k + 1].load (std::memory_order_relaxed), std::memory_order_relaxed);
        seqs[q].step[n - 1].store (0, std::memory_order_relaxed);
        touch();
        return true;
    }
    void setSequence (int q, const uint8_t* steps, int n) // main thread (a state loaded)
    {
        if (! validSeq (q))
            return;
        n = std::clamp (n, 0, maxSteps);
        for (int i = 0; i < maxSteps; ++i)
            seqs[q].step[i].store (i < n ? steps[i] : 0, std::memory_order_relaxed);
        seqs[q].length.store ((uint8_t) n, std::memory_order_relaxed);
        touch();
    }

    // Counts every change, for the page (redraws when it moves).
    uint32_t changes() const { return edits.load (std::memory_order_relaxed); }

private:
    struct Slot
    {
        std::atomic<float> field[fieldCount] {};
        std::atomic<bool> used { false };
        std::atomic<bool> autoName { true };
        char name[nameLength + 4] {};
    };
    struct Seq
    {
        std::atomic<uint8_t> length { 0 };
        std::atomic<uint8_t> step[maxSteps] {};
    };
    Slot slots[slotCount + 1]; // [0] unused: filters count from 1
    Seq seqs[sequenceCount + 1];
    std::atomic<uint32_t> edits { 0 };
    void touch() { edits.fetch_add (1, std::memory_order_relaxed); }
};

// A step's name as the page shows it: "F07 BELL TOWER", "PAUSE", "FADE x1/4" (main thread).
inline void stepText (const Store& store, int code, char* text, size_t size)
{
    if (isFilterStep (code))
        std::snprintf (text, size, "F%02d %s", code, store.name (code));
    else if (code == stepPause)
        std::snprintf (text, size, "PAUSE");
    else if (code == stepHalt)
        std::snprintf (text, size, "HALT");
    else if (code == stepBumper)
        std::snprintf (text, size, "BUMPER");
    else if (isFadeStep (code))
        std::snprintf (text, size, "FADE %s", fadeScaleNames[code - stepFade]);
    else
        std::snprintf (text, size, "--");
}

// --- The sequencer ----------------------------------------------------------------------------------------------

// Where a sequence is and what it has loaded. The audio thread's; the page sees it through the feed.
struct Sequencer
{
    enum class Mode : uint8_t { normal, paused, halted };
    int sequence = 0; // 0: off
    int pos = 0;      // the step it is at
    Mode mode = Mode::normal;
    int fade = unityFade; // the Fade Time Scale in force
    int filter = 0;       // the filter it loaded last (0: none yet, the Filter parameter's is heard)

    bool on (const Store& s) const { return sequence > 0 && s.length (sequence) > 0; }

    // A sequence loaded (or started again): its first step, at once.
    void load (const Store& s, int q)
    {
        sequence = q, pos = 0, mode = Mode::normal, fade = unityFade, filter = 0;
        if (on (s))
            enter (s);
    }
    void inc (const Store& s) { move (s, 1); }
    void dec (const Store& s) { move (s, -1); }
    // Reset: out of a Pause (nothing else); otherwise back to the first step.
    void reset (const Store& s)
    {
        if (! on (s))
            return;
        if (mode == Mode::paused)
        {
            mode = Mode::normal;
            return;
        }
        pos = 0, mode = Mode::normal;
        enter (s);
    }
    // The wheel: to the `n`th filter step on (back for n < 0), round the sequence, past the commands without
    // their taking effect (as the module's encoder does). The length is read once: the page may take out the last
    // step meanwhile.
    void browse (const Store& s, int n)
    {
        const int length = s.length (sequence); // 0 for no sequence
        if (length <= 0 || n == 0)
            return;
        pos = std::min (pos, length - 1);
        int at = pos;
        for (int k = 0, left = std::abs (n); k < length && left > 0; ++k)
        {
            at = (at + (n > 0 ? 1 : -1) + length) % length;
            if (const int code = s.step (sequence, at); isFilterStep (code))
                --left, pos = at, filter = code;
        }
    }

private:
    void move (const Store& s, int by)
    {
        const int length = s.length (sequence); // read once, as in browse()
        if (length <= 0 || mode != Mode::normal)
            return;
        pos = std::min (pos, length - 1);
        const int next = (pos + by + length) % length;
        if (s.step (sequence, next) == stepBumper)
            return; // a wall: stays where it is
        pos = next;
        enter (s);
    }
    // The step now takes effect: a filter loads; Pause and Halt stop the sequence; a Fade scales what follows; a
    // Bumper (only ever entered at the first step, by a load or a Reset) is a Pause already ended.
    void enter (const Store& s)
    {
        const int code = s.step (sequence, pos);
        if (isFilterStep (code))
            filter = code;
        else if (code == stepPause)
            mode = Mode::paused;
        else if (code == stepHalt)
            mode = Mode::halted;
        else if (isFadeStep (code))
            fade = code - stepFade;
    }
};

// A filter's point on an axis: the knobs and their modulation (`v`, 0..1 with 0.5 the middle), scaled by the filter's
// gain around the middle and moved by its offset (a half axis is 1), held inside the cube. A filter that neither
// scales nor moves leaves the point exactly as the knobs put it.
inline float axisPoint (float v, float gain, float offset)
{
    if (gain == 1.0f && offset == 0.0f)
        return std::clamp (v, 0.0f, 1.0f);
    return std::clamp (0.5f + gain * (v - 0.5f) + 0.5f * offset, 0.0f, 1.0f);
}
} // namespace cortex
