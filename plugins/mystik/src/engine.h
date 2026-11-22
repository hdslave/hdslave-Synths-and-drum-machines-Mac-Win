// Mystik's engine: the sequencer and its clock, notes, the voice and the effects.
//
// The clock is Anvil's: a step starts on each multiple of Rate from the song's top while the host plays (MPC swing
// moves every second one), so the same place in the song always plays the same step. Each step asks the sequencer's
// mode what to play (decide()), which is a pure function of the clock's number and the mode's state, so it can also be
// asked a step ahead (to hold the gate into a slide). Randomness is a hash of the clock's number, Seed and a salt.
#pragma once

#include "fx.h"
#include "params.h"
#include "voice.h"

#include <atomic>

namespace mystik
{
// A clock less than this many frames away is due now (CLAP's 2^-31-beat song position would otherwise make a step on a
// block's first frame come a frame late depending on the block size).
constexpr double clockSlack = 1.0e-3;

// What a step plays.
struct Decision
{
    bool play = false;
    int place = 0;      // the step (or timeline place) it is at, for the screen
    int degree = 0;
    int stroke = Stroke::open;
    bool accent = false, slide = false;
    float force = 1.0f;
};

// The modes' memory: the Brownian walk, the Turing register, the automaton's ring.
struct SeqState
{
    int walk = 0;
    uint32_t turing = 0;
    uint32_t cells = 0, born = 0;
    int64_t generation = 0;
    bool fresh = false; // just started: the automaton plays its first generation before evolving
};

// For the screen: what the audio thread last did, in atomics (relaxed: a torn frame of the scope is harmless).
struct Telemetry
{
    static constexpr int scopeSize = 2048, lightSize = 128, history = 16;
    std::atomic<float> light { 0.0f };
    std::atomic<int> place { -1 }, cycle { 16 }, mode { 0 };
    std::atomic<uint32_t> strikes { 0 }, turing { 0 }, cells { 0 }, steps { 0 };
    std::atomic<bool> running { false }, accent { false };
    std::atomic<float> force { 0.0f };
    std::atomic<uint32_t> scopeAt { 0 }, lightAt { 0 }, generationAt { 0 }, generationsSeen { 0 };
    std::atomic<uint32_t> renders { 0 }; // one more each render: the page sees the audio thread is still running
    std::atomic<float> scope[scopeSize];
    std::atomic<float> lightTrail[lightSize];
    std::atomic<uint32_t> generations[history]; // the automaton's last 16 rows (row g at g % 16)
    Telemetry()
    {
        for (auto& s : scope) s.store (0.0f, std::memory_order_relaxed);
        for (auto& s : lightTrail) s.store (0.0f, std::memory_order_relaxed);
        for (auto& g : generations) g.store (0, std::memory_order_relaxed);
    }
};

class Engine
{
public:
    explicit Engine (Telemetry* t = nullptr) : telemetry (t) {}

    void prepare (double hostRate, uint32_t)
    {
        rate = hostRate;
        voice.prepare (hostRate);
        fx.prepare (hostRate);
        reset();
    }

    void reset()
    {
        voice.reset();
        fx.reset();
        playing = fresh = false;
        lastStep = -1;
        now = 0;
        clockedAt = -(int64_t (1) << 40);
        clockedLoud = false;
        queueSize = 0;
        held = 0;
        transpose = 0.0f;
        st = SeqState {};
        lightCounter = 0;
        voiceDirty = fxDirty = true;
    }

    // A parameter's value, from the frame the next render starts.
    void set (uint32_t id, float v)
    {
        if (id >= ParamId::count || knob[id] == v)
            return;
        knob[id] = v;
        if (id <= ParamId::glide || id == ParamId::human)
            voiceDirty = true;
        else if (id <= ParamId::volume)
            fxDirty = true;
        else if (id == ParamId::rate)
            regridWanted = true;
        else if (id == ParamId::seed || id == ParamId::rule || id == ParamId::seqMode)
            reseed = true;
        else if (id == ParamId::seqOn && v < 0.5f)
            releaseGate();
    }

    // Regenerate: a new Seed (told to the host by the glue through takeSeed()).
    void press (uint32_t id)
    {
        if (id != ParamId::regenerate)
            return;
        const int next = (int) (bits32 ((int64_t) knob[ParamId::seed] * 7919 + ++regenerations, 0x51ed270bu) % 10000u);
        knob[ParamId::seed] = (float) (next == (int) knob[ParamId::seed] ? (next + 1) % 10000 : next);
        seedChanged = true;
        reseed = true;
    }
    bool takeSeed (float& value)
    {
        if (! seedChanged)
            return false;
        seedChanged = false;
        value = knob[ParamId::seed];
        return true;
    }

    // Keys. With the sequencer on and the host playing, a key transposes the pattern (latched, from C3); otherwise keys
    // play the drum as Notes says.
    void noteOn (int key, float velocity)
    {
        applyKnobs();
        if (seqOn() && playing)
        {
            transpose = (float) (key - 60);
            return;
        }
        // The same key again without a note off between (a sequencer's repeated note): it starts again, not a slide.
        for (int i = 0; i < held; ++i)
            if (heldKeys[i] == key)
            {
                for (int j = i; j + 1 < held; ++j)
                    heldKeys[j] = heldKeys[j + 1];
                --held, --i;
            }
        const int mode = (int) knob[ParamId::notes];
        Hit h;
        h.force = velocity;
        h.accent = velocity >= 100.0f / 127.0f - 1e-6f;
        h.hold = knob[ParamId::hold] > 0.0f;
        if (mode == 1) // Strokes: 36-43 the eight strokes at Tune, each 8 keys up an octave
        {
            const int rel = key - 36;
            const int octave = rel >= 0 ? rel / 8 : -((-rel + 7) / 8);
            h.stroke = rel - octave * 8;
            h.semis = 12.0f * (float) octave;
            strikeWithStroke (h);
        }
        else if (mode == 2) // Steps: 36-51 play steps 1-16
        {
            const int s = ((key - 36) % steps + steps) % steps;
            Decision d = stepDecision (s);
            d.force *= velocity;
            d.accent = d.accent || h.accent;
            d.slide = false;
            playDecision (d, -1);
        }
        else // Pitch: the key from C3, overlapping keys slide
        {
            h.stroke = Stroke::open;
            h.semis = (float) (key - 60);
            h.slide = held > 0;
            voice.strike (h);
        }
        if (held < (int) (sizeof (heldKeys) / sizeof (heldKeys[0])))
            heldKeys[held++] = key;
    }

    void noteOff (int key)
    {
        for (int i = 0; i < held; ++i)
            if (heldKeys[i] == key || key < 0)
            {
                for (int j = i; j + 1 < held; ++j)
                    heldKeys[j] = heldKeys[j + 1];
                --held, --i;
            }
        if (held > 0 && (int) knob[ParamId::notes] == 0 && ! (seqOn() && playing))
        {
            // Back to the key still held, gliding.
            Hit h;
            h.slide = true;
            h.hold = knob[ParamId::hold] > 0.0f;
            h.semis = (float) (heldKeys[held - 1] - 60);
            voice.strike (h);
        }
        else if (held == 0 && ! (seqOn() && playing))
            voice.release();
    }

    // The page plays a step (its pitch, trig and stroke; a rest plays as a hit). No key is held to end it: its gate ends
    // as a step's would, at Gate's share of the step a key plays (an eighth of a second), or Hold would never let go.
    void audition (int s)
    {
        applyKnobs();
        Decision d = stepDecision (s);
        d.slide = false;
        playDecision (d, -1);
        enqueue (now + (uint64_t) std::max (1.0, knob[ParamId::gate] * rate * 0.125), Queued::release, Hit {}, -1);
    }

    void choke() { voice.choke(), fx.cut(), held = 0, queueSize = 0; }
    void allNotesOff() { held = 0, voice.release(); }
    void allSoundOff() { choke(), fx.silence(); }
    void wheel (float bend, float mod) { voice.wheel (bend, mod); }

    // The host's transport at the start of a process call of `frames`.
    void transport (bool isPlaying, double beats, double bpm, uint32_t frames)
    {
        perFrame = bpm / 60.0 / rate;
        fx.tempo (bpm);
        const double window = (double) std::max (frames, lastBlock);
        lastBlock = frames;
        if (! isPlaying)
        {
            if (playing)
                releaseGate();
            playing = false;
            return;
        }
        const double moved = std::fabs (beats - position);
        if (! playing)
        {
            fresh = true; // a Play: the generative modes start from Seed
            relocate (beats, window, false);
        }
        else if (moved > 0.5 * perFrame && moved >= window * perFrame)
            relocate (beats, window, true);
        playing = true;
        position = beats;
    }

    // Where the sequence's own count is, for a host that gives no song position.
    double ownPosition() const { return playing ? position : 0.0; }

    void render (float* left, float* right, uint32_t frames)
    {
        applyKnobs();
        float* out = left;
        bool silentRun = true;
        while (frames > 0)
        {
            uint32_t run = frames;
            if (queueSize > 0)
            {
                if (now >= queue[0].at)
                {
                    fireQueued();
                    continue;
                }
                run = (uint32_t) std::min<uint64_t> (run, queue[0].at - now);
            }
            if (playing)
            {
                if (regridWanted)
                    regrid();
                const double next = clockBeat (lastStep + 1);
                const double ahead = (next - position) / perFrame;
                if (ahead <= clockSlack)
                {
                    ++lastStep;
                    clock (lastStep);
                    continue;
                }
                const double untilStep = std::ceil (ahead - clockSlack);
                if (untilStep < (double) run)
                    run = (uint32_t) untilStep;
            }
            silentRun &= voice.run (out, run);
            publishScope (out, run);
            now += run;
            if (playing)
                position += run * perFrame;
            out += run;
            frames -= run;
        }
        fx.process (left, right, (uint32_t) (out - left), silentRun);
        publish();
    }

    float knobValue (uint32_t id) const { return knob[id]; }
    uint32_t strikeCount() const { return voice.strikeCount(); }
    float light() const { return voice.lightLevel(); }
    bool silent() const { return voice.isIdle() && fx.quiet(); }
    int place() const { return shownPlace; }

private:
    bool seqOn() const { return knob[ParamId::seqOn] >= 0.5f; }
    int mode() const { return std::max (0, std::min ((int) knob[ParamId::seqMode], 5)); }
    double stepBeats() const { return rateBeats[std::max (0, std::min ((int) knob[ParamId::rate], 9))]; }
    int length() const
    {
        if (mode() == SeqMode::rhythm)
            return rhythmLength (rhythmIndex());
        return std::max (1, std::min ((int) knob[ParamId::length], steps));
    }
    int rhythmIndex() const { return std::max (0, std::min ((int) knob[ParamId::rhythm], rhythmCount - 1)); }
    uint32_t seed() const { return (uint32_t) knob[ParamId::seed] * 0x9e3779b9u + 0x7f4a7c15u; }
    int scale() const { return (int) knob[ParamId::scale]; }

    void applyKnobs()
    {
        if (voiceDirty)
        {
            VoiceKnobs k;
            k.tune = knob[ParamId::tune], k.source = knob[ParamId::source], k.body = (int) knob[ParamId::body];
            k.ring = knob[ParamId::ring], k.position = knob[ParamId::position], k.snap = knob[ParamId::snap];
            k.bend = knob[ParamId::bend], k.drift = knob[ParamId::drift], k.fmIndex = knob[ParamId::fmIndex];
            k.ratio = (int) knob[ParamId::fmRatio], k.feedback = knob[ParamId::feedback], k.timbre = knob[ParamId::timbre];
            k.symmetry = knob[ParamId::symmetry], k.bloom = knob[ParamId::bloom], k.gateMode = knob[ParamId::gateMode];
            k.resonance = knob[ParamId::resonance], k.strike = knob[ParamId::strike], k.decay = knob[ParamId::decay];
            k.open = knob[ParamId::open], k.hold = knob[ParamId::hold], k.memory = knob[ParamId::memory];
            k.accent = knob[ParamId::accent], k.glide = knob[ParamId::glide], k.human = knob[ParamId::human];
            voice.set (k);
            voiceDirty = false;
        }
        if (fxDirty)
        {
            FxKnobs k;
            k.drive = knob[ParamId::drive], k.crush = knob[ParamId::crush], k.echo = knob[ParamId::echo];
            k.echoTime = (int) knob[ParamId::echoTime], k.repeats = knob[ParamId::repeats], k.absorb = knob[ParamId::absorb];
            k.space = knob[ParamId::space], k.spaceDecay = knob[ParamId::spaceDecay], k.volume = knob[ParamId::volume];
            fx.set (k);
            fxDirty = false;
        }
    }

    // --- Clock (Anvil's) -------------------------------------------------------------------------------------------
    // Swing: the second clock of each pair comes later, at Swing's share of the pair; at 50 % exactly on the grid.
    double clockBeat (int64_t k) const
    {
        const double grid = stepBeats(), swing = knob[ParamId::swing];
        if (swing == 0.5 || (k & 1) == 0)
            return (double) k * grid;
        return (double) (k - 1) * grid + 2.0 * grid * swing;
    }
    int64_t clockAtOrBefore (double beats) const
    {
        const double grid = stepBeats();
        const int64_t b = (int64_t) std::floor (beats / grid + 1.0e-9);
        if ((b & 1) == 0 || knob[ParamId::swing] == 0.5f)
            return b;
        return beats / grid + 1.0e-9 < clockBeat (b) / grid ? b - 1 : b;
    }

    void relocate (double beats, double window, bool jumped)
    {
        const int64_t b = clockAtOrBefore (beats);
        const double late = (beats - clockBeat (b)) / perFrame;
        position = beats;
        regridWanted = false;
        // A loop gone round inside a block (the host tells only at the next): the step played on the frame its step b
        // began was b. Line up with b; play b only if the step played then was silent.
        if (jumped && std::fabs ((double) ((int64_t) now - clockedAt) - late) <= 2.0)
        {
            lastStep = b;
            if (! clockedLoud)
                clock (b);
            return;
        }
        lastStep = b - (late < window ? 1 : 0);
    }

    void regrid()
    {
        lastStep = clockAtOrBefore (position - perFrame * (1.0 - clockSlack));
        regridWanted = false;
    }

    // --- Steps -----------------------------------------------------------------------------------------------------
    // The place clock k reads, by Direction and Rotate (Brownian walks the state's own place).
    int order (int64_t k, SeqState& s, int len) const
    {
        const int rot = (int) knob[ParamId::rotate];
        int p;
        switch ((int) knob[ParamId::direction])
        {
            case 1: p = len - 1 - (int) (((k % len) + len) % len); break;
            case 2:
            {
                if (len == 1) { p = 0; break; }
                const int64_t period = 2 * len - 2, at = ((k % period) + period) % period;
                p = (int) (at < len ? at : period - at);
                break;
            }
            case 3:
            {
                const float r = unit (k, seed() ^ 0x1b873593u);
                s.walk = ((s.walk + (r < 0.25f ? -1 : (r < 0.4f ? 0 : 1))) % len + len) % len;
                p = s.walk;
                break;
            }
            case 4: p = std::min (len - 1, (int) (unit (k, seed() ^ 0x2545f491u) * (float) len)); break;
            default: p = (int) (((k % len) + len) % len); break;
        }
        return (p + rot) % len;
    }

    Decision stepDecision (int p) const
    {
        Decision d;
        p = ((p % steps) + steps) % steps;
        const int trig = (int) knob[ParamId::trig1 + p];
        d.place = p;
        d.play = true;
        d.degree = (int) knob[ParamId::pitch1 + p];
        d.stroke = (int) knob[ParamId::stroke1 + p];
        d.accent = trig == Trig::accent || trig == Trig::accentSlide;
        d.slide = trig == Trig::slide || trig == Trig::accentSlide;
        return d;
    }

    // What clock k plays, the mode's state moved on as playing it would.
    Decision decide (int64_t k, SeqState& s) const
    {
        const int len = length();
        const uint32_t sd = seed();
        const float density = knob[ParamId::density], mutate = knob[ParamId::mutate], accentShare = knob[ParamId::accents];
        Decision d;
        switch (mode())
        {
            case SeqMode::pattern:
            {
                const int p = order (k, s, len);
                d = stepDecision (p);
                const int trig = (int) knob[ParamId::trig1 + p];
                if (trig == Trig::rest)
                {
                    // Density over 50 % fills rests with ghost notes.
                    d.play = density > 0.5f && unit (k, sd ^ 0x68e31da4u) < (density - 0.5f) * 2.0f;
                    d.stroke = Stroke::ghost, d.accent = d.slide = false;
                }
                else
                {
                    d.play = unit (k, sd ^ 0x5bd1e995u) < knob[ParamId::chance1 + p] || knob[ParamId::chance1 + p] >= 1.0f;
                    if (density < 0.5f && unit (k, sd ^ 0x1656667bu) < (0.5f - density) * 2.0f)
                        d.play = false;
                }
                // Mutate: some steps move by a degree or two each pass, a fresh choice each pass.
                const int64_t pass = k >= 0 ? k / len : (k - len + 1) / len;
                if (mutate > 0.0f && unit (pass * steps + p, sd ^ 0x9e3779b1u) < mutate)
                    d.degree += unit (pass * steps + p, sd ^ 0x85ebca6bu) < 0.5f ? -1 - (mutate > 0.6f) : 1 + (mutate > 0.6f);
                break;
            }
            case SeqMode::euclid:
            {
                const int p = order (k, s, len);
                d = stepDecision (p);
                const int pulses = (int) std::lround (density * (float) len);
                bool hit = pulses > 0 && ((p * pulses) % len) < pulses;
                if (mutate > 0.0f && unit (k, sd ^ 0xc2b2ae35u) < mutate * 0.5f)
                    hit = ! hit;
                d.play = hit && (unit (k, sd ^ 0x5bd1e995u) < knob[ParamId::chance1 + p] || knob[ParamId::chance1 + p] >= 1.0f);
                if (hit && pulses > 0)
                {
                    int before = 0;
                    for (int i = 0; i < p; ++i)
                        before += ((i * pulses) % len) < pulses;
                    const int accented = (int) std::lround (accentShare * (float) pulses);
                    d.accent = d.accent || (accented > 0 && ((before * accented) % pulses) < accented);
                }
                break;
            }
            case SeqMode::turing:
            {
                const uint32_t mask = len >= 32 ? 0xffffffffu : (1u << len) - 1u;
                const uint32_t out = (s.turing >> (len - 1)) & 1u;
                const uint32_t flip = unit (k, sd ^ 0x27d4eb2fu) < mutate * 0.5f ? 1u : 0u;
                s.turing = ((s.turing << 1) | (out ^ flip)) & mask;
                const uint32_t r = s.turing;
                const float pitchShare = (float) ((r * 2654435761u) >> 24) / 255.0f;
                const float gateShare = (float) ((r * 0x85ebca6bu + 0x1234567u) >> 24) / 256.0f;
                const float accentRoll = (float) ((r * 0xc2b2ae35u + 0x9e3779bu) >> 24) / 256.0f;
                const int p = (int) (((k % len) + len) % len);
                const Decision step = stepDecision (p);
                const int span = (int) std::lround (rangeOctaves (knob[ParamId::range]) * (float) scales[std::max (0, std::min (scale(), 11))].count);
                d.place = p;
                d.play = gateShare < density;
                d.degree = (int) std::lround ((pitchShare - 0.5f) * (float) span);
                d.stroke = step.stroke;
                d.accent = accentRoll < accentShare * 0.6f;
                d.slide = ((r ^ (r >> 3)) & 7u) == 7u;
                break;
            }
            case SeqMode::automaton:
            {
                const int p = order (k, s, len);
                const int64_t at = ((k % len) + len) % len;
                if (at == 0 && ! s.fresh)
                    evolve (s, len, k);
                d = stepDecision (p);
                d.play = ((s.cells >> p) & 1u) != 0
                      && (unit (k, sd ^ 0x5bd1e995u) < knob[ParamId::chance1 + p] || knob[ParamId::chance1 + p] >= 1.0f);
                d.accent = d.accent || (((s.born >> p) & 1u) != 0 && unit (k, sd ^ 0x7feb352du) < accentShare);
                break;
            }
            case SeqMode::bounce:
            {
                // The ball is dropped at each cycle's start (bounces queued by playDecision); other clocks rest.
                const int64_t at = ((k % len) + len) % len;
                d = stepDecision (0);
                d.place = (int) at;
                d.play = at == 0;
                d.slide = false;
                break;
            }
            case SeqMode::rhythm:
            {
                const int r = rhythmIndex();
                const int p = order (k, s, len);
                const int stroke = rhythmStroke (r, p);
                d = stepDecision (p);
                d.place = p;
                d.slide = false;
                if (stroke < 0)
                {
                    d.play = mutate > 0.0f && unit (k, sd ^ 0x68e31da4u) < mutate * 0.5f;
                    d.stroke = Stroke::ghost, d.accent = false;
                }
                else
                {
                    d.play = true;
                    d.stroke = stroke;
                    d.accent = stroke == Stroke::bass && unit (k, sd ^ 0x7feb352du) < accentShare;
                }
                break;
            }
            default: break;
        }
        return d;
    }

    // The automaton's next generation: each cell from itself and its two neighbours by Rule; Mutate flips a few; a dead
    // ring comes back from Seed.
    void evolve (SeqState& s, int len, int64_t k) const
    {
        const uint32_t rule = (uint32_t) knob[ParamId::rule];
        const uint32_t mask = (1u << len) - 1u;
        uint32_t next = 0;
        for (int i = 0; i < len; ++i)
        {
            const uint32_t l = (s.cells >> ((i + 1) % len)) & 1u, c = (s.cells >> i) & 1u, r = (s.cells >> ((i + len - 1) % len)) & 1u;
            next |= ((rule >> (l * 4 + c * 2 + r)) & 1u) << i;
        }
        const float mutate = knob[ParamId::mutate];
        if (mutate > 0.0f)
            for (int i = 0; i < len; ++i)
                if (unit (k * 16 + i, seed() ^ 0x165667b1u) < mutate * 0.25f)
                    next ^= 1u << i;
        next &= mask;
        ++s.generation;
        if (next == 0)
            next = seedCells (len, s.generation);
        s.born = next & ~s.cells;
        s.cells = next;
    }
    uint32_t seedCells (int len, int64_t generation) const
    {
        uint32_t cells = 0;
        for (int i = 0; i < len; ++i)
            if (unit (generation * 16 + i, seed() ^ 0x3c6ef372u) < std::max (0.1f, knob[ParamId::density]))
                cells |= 1u << i;
        return cells != 0 ? cells : 1u;
    }

    // The state a fresh start (Play, a new Seed or Rule) begins from.
    SeqState initialState (int64_t k) const
    {
        SeqState s;
        const int len = length();
        s.walk = (int) (((k % len) + len) % len) - 1;
        s.turing = bits32 ((int64_t) knob[ParamId::seed], 0x8f1bbcdcu) & (len >= 32 ? 0xffffffffu : (1u << len) - 1u);
        s.cells = seedCells (len, 0);
        s.born = s.cells;
        s.generation = 0;
        s.fresh = true;
        return s;
    }

    // A step starts on this frame (clock k).
    void clock (int64_t k)
    {
        clockedAt = (int64_t) now, clockedLoud = false;
        if (! seqOn())
            return;
        if (fresh || reseed)
        {
            st = initialState (k);
            fresh = reseed = false;
            shownGeneration = ~0u; // the screen's generations start again
            if (telemetry != nullptr)
                for (auto& g : telemetry->generations) g.store (0, std::memory_order_relaxed);
        }
        const Decision d = decide (k, st);
        st.fresh = false;
        shownPlace = d.place;
        shownCycle = length();
        if (! d.play)
            return;
        clockedLoud = true;
        playDecision (d, k);
    }

    // A step's sound: its strike (or its slide), the flam's or roll's further hits, a bounce's ball, and the gate's end.
    void playDecision (const Decision& d, int64_t k)
    {
        Hit h;
        h.force = d.force;
        h.stroke = d.stroke;
        h.accent = d.accent;
        h.slide = d.slide;
        h.hold = knob[ParamId::hold] > 0.0f;
        h.semis = transpose + degreeSemis (d.degree, scale());
        const double stepFrames = k >= 0 && perFrame > 0.0 ? (clockBeat (k + 1) - clockBeat (k)) / perFrame : rate * 0.125;
        if (mode() == SeqMode::bounce && k >= 0)
        {
            bounce (h, k);
            return;
        }
        strikeWithStroke (h, stepFrames);
        if (k >= 0)
            enqueue (now + (uint64_t) std::max (1.0, knob[ParamId::gate] * stepFrames), Queued::release, Hit {}, k);
    }

    // Flam and Roll are further hits; the rest strike once.
    void strikeWithStroke (const Hit& h, double stepFrames = 0.0)
    {
        if (stepFrames <= 0.0)
            stepFrames = rate * 0.125;
        if (h.stroke == Stroke::flam && ! h.slide)
        {
            Hit grace = h;
            grace.stroke = Stroke::open, grace.force *= 0.45f, grace.accent = false;
            voice.strike (grace);
            Hit main = h;
            main.stroke = Stroke::open, main.follow = true;
            enqueue (now + (uint64_t) std::lround (0.022 * rate), Queued::hit, main, -1);
            return;
        }
        if (h.stroke == Stroke::roll && ! h.slide)
        {
            // Four hits speeding up (each gap 0.7 of the one before) over 85 % of the step, fading.
            const double first = 0.85 * stepFrames / (1.0 + 0.7 + 0.49);
            double at = 0.0, gap = first;
            const float force[4] = { 1.0f, 0.8f, 0.65f, 0.55f };
            for (int i = 0; i < 4; ++i)
            {
                Hit r = h;
                r.stroke = Stroke::open, r.force = h.force * force[i], r.accent = h.accent && i == 0, r.follow = i > 0;
                if (i == 0)
                    voice.strike (r);
                else
                    enqueue (now + (uint64_t) std::lround (at), Queued::hit, r, -1);
                at += gap, gap *= 0.7;
            }
            return;
        }
        voice.strike (h);
    }

    // Bounce: the ball dropped now; Density bounces in 90 % of the cycle, each Curve of the one before and softer.
    void bounce (const Hit& first, int64_t k)
    {
        const int len = length();
        const double cycle = (clockBeat (k + len) - clockBeat (k)) / perFrame;
        const int count = 2 + (int) std::lround (knob[ParamId::density] * 14.0f);
        const double r = bounceRatio (knob[ParamId::curve]);
        const double gap0 = 0.9 * cycle * (1.0 - r) / (1.0 - std::pow (r, count));
        double at = 0.0, gap = gap0;
        for (int j = 0; j < count; ++j)
        {
            const Decision step = stepDecision (j % len);
            Hit h = first;
            h.semis = transpose + degreeSemis (step.degree, scale());
            h.stroke = step.stroke == Stroke::flam || step.stroke == Stroke::roll ? Stroke::open : step.stroke;
            h.force = std::pow ((float) r, 0.6f * (float) j);
            h.accent = j == 0 && knob[ParamId::accents] > 0.0f;
            h.slide = false;
            h.follow = j > 0;
            double jitter = 0.0;
            if (knob[ParamId::mutate] > 0.0f && j > 0)
                jitter = (unit (k * 32 + j, seed() ^ 0x94d049bbu) - 0.5f) * knob[ParamId::mutate] * 0.6 * gap;
            if (j == 0)
                voice.strike (h);
            else
                enqueue (now + (uint64_t) std::max (1.0, at + jitter), Queued::hit, h, -1);
            at += gap, gap *= r;
        }
        enqueue (now + (uint64_t) std::max (1.0, 0.95 * cycle), Queued::release, Hit {}, -1);
    }

    // --- The queue of hits and gate ends still to come (time order) -------------------------------------------------
    struct Queued
    {
        enum Kind : uint8_t { hit, release };
        uint64_t at;
        Kind kind;
        Hit h;
        int64_t clock;
    };
    void enqueue (uint64_t at, Queued::Kind kind, const Hit& h, int64_t k)
    {
        if (queueSize == queueCapacity)
            return;
        int i = queueSize++;
        while (i > 0 && queue[i - 1].at > at)
            queue[i] = queue[i - 1], --i;
        queue[i] = { at, kind, h, k };
    }
    void fireQueued()
    {
        const Queued q = queue[0];
        for (int i = 1; i < queueSize; ++i)
            queue[i - 1] = queue[i];
        --queueSize;
        if (q.kind == Queued::hit)
        {
            voice.strike (q.h);
            return;
        }
        // A gate's end: held on if the next step slides into its note.
        if (q.clock >= 0 && playing && seqOn())
        {
            SeqState ahead = st;
            const Decision next = decide (q.clock + 1, ahead);
            if (next.play && next.slide && mode() != SeqMode::bounce)
                return;
        }
        if (held == 0 || (seqOn() && playing))
            voice.release();
    }
    void releaseGate()
    {
        for (int i = 0; i < queueSize; ++i)
            if (queue[i].kind == Queued::release)
            {
                for (int j = i; j + 1 < queueSize; ++j)
                    queue[j] = queue[j + 1];
                --queueSize, --i;
            }
        voice.release();
    }

    // --- For the screen --------------------------------------------------------------------------------------------
    void publishScope (const float* out, uint32_t n)
    {
        if (telemetry == nullptr)
            return;
        uint32_t at = scopeWrite;
        for (uint32_t i = 0; i < n; ++i)
        {
            if (out[i] == 0.0f && scopeZeros >= (uint32_t) Telemetry::scopeSize)
                continue; // the ring is all silence already: nothing to show moving
            scopeZeros = out[i] == 0.0f ? scopeZeros + 1 : 0;
            telemetry->scope[at].store (out[i], std::memory_order_relaxed), at = (at + 1) & (Telemetry::scopeSize - 1);
        }
        scopeWrite = at;
        lightCounter += n;
        // The light's trail: a value every 10.7 ms at 48 kHz (512 frames).
        while (lightCounter >= 512)
        {
            lightCounter -= 512;
            telemetry->lightTrail[lightWrite].store (voice.lightLevel(), std::memory_order_relaxed);
            lightWrite = (lightWrite + 1) & (Telemetry::lightSize - 1);
            telemetry->lightAt.store (lightWrite, std::memory_order_release);
        }
    }
    void publish()
    {
        if (telemetry == nullptr)
            return;
        auto& t = *telemetry;
        t.scopeAt.store (scopeWrite, std::memory_order_release);
        t.light.store (voice.lightLevel(), std::memory_order_relaxed);
        t.place.store (seqOn() && playing ? shownPlace : -1, std::memory_order_relaxed);
        t.cycle.store (shownCycle, std::memory_order_relaxed);
        t.mode.store (mode(), std::memory_order_relaxed);
        t.turing.store (st.turing, std::memory_order_relaxed);
        t.cells.store (st.cells, std::memory_order_relaxed);
        if ((uint32_t) st.generation != shownGeneration || st.cells != shownCells)
        {
            shownGeneration = (uint32_t) st.generation, shownCells = st.cells;
            t.generations[shownGeneration % Telemetry::history].store (st.cells, std::memory_order_relaxed);
            t.generationAt.store (shownGeneration, std::memory_order_release);
            t.generationsSeen.store (std::min<uint32_t> (shownGeneration + 1, Telemetry::history), std::memory_order_relaxed);
        }
        t.running.store (seqOn() && playing, std::memory_order_relaxed);
        t.force.store (voice.lastStrikeForce(), std::memory_order_relaxed);
        t.accent.store (voice.lastStrikeAccented(), std::memory_order_relaxed);
        t.strikes.store (voice.strikeCount(), std::memory_order_release);
        t.renders.store (++renders, std::memory_order_relaxed);
    }

    Telemetry* telemetry = nullptr;
    Voice voice;
    Effects fx;
    double rate = 48000.0, perFrame = 0.0, position = 0.0;
    float knob[ParamId::count] {};
    bool voiceDirty = true, fxDirty = true;
    bool playing = false, fresh = false, reseed = false, regridWanted = false, seedChanged = false;
    int64_t lastStep = -1, clockedAt = 0;
    bool clockedLoud = false;
    uint64_t now = 0;
    uint32_t lastBlock = 0, regenerations = 0;
    SeqState st;
    float transpose = 0.0f;
    int heldKeys[16] {};
    int held = 0;
    static constexpr int queueCapacity = 48;
    Queued queue[queueCapacity] {};
    int queueSize = 0;
    int shownPlace = -1, shownCycle = 16;
    uint32_t shownGeneration = 0, shownCells = 0, scopeWrite = 0, scopeZeros = 0, lightWrite = 0, lightCounter = 0, renders = 0;
};
} // namespace mystik
