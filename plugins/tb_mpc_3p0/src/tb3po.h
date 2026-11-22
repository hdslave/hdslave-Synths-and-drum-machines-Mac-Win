// TB-3PO: the random acid-pattern generator and sequencer of the Hemisphere applet of that name (Ornament & Crime
// module, Benisphere Suite firmware), ported to run inside this plug-in. The pattern generator, its random numbers,
// the quantizer and the step, gate and slide logic are the applet's, line for line; the parts that the module's
// hardware did (clock and reset inputs, the timer) are the plug-in's Engine's. Where this file differs from the
// applet, it says so (search for "Differs").
//
// The applet: Copyright (c) 2020, Logarhythm. The quantizer: Copyright 2015 Emilie Gillet, re-implemented by Bryan
// Head. Both MIT licensed, see licenses/MIT-oc-firmware.txt; this port is under the same terms.
#pragma once

#include "scales.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>

namespace tbmpc
{
// --- random() and randomSeed() as the module's Teensy 3 core has them ---------------------------------------------
// Park and Miller's "minimal standard" generator: x = 16807 x mod (2^31 - 1), by Schrage's method so 32-bit integers
// suffice; a state of 0 is taken as 123459876, and randomSeed(0) is ignored.
class TeensyRandom
{
public:
    void randomSeed (uint32_t s)
    {
        if (s > 0)
            state = s;
    }
    int32_t next()
    {
        int32_t x = (int32_t) state;
        if (x == 0)
            x = 123459876;
        const int32_t hi = x / 127773, lo = x % 127773;
        x = 16807 * lo - 2836 * hi;
        if (x < 0)
            x += 0x7fffffff;
        state = (uint32_t) x;
        return x;
    }
    // random(howbig): 0 .. howbig - 1.
    uint32_t below (uint32_t n) { return n == 0 ? 0u : (uint32_t) next() % n; }
    // random(howsmall, howbig): howsmall .. howbig - 1.
    int32_t between (int32_t lo, int32_t hi)
    {
        if (lo >= hi)
            return lo;
        return (int32_t) (below ((uint32_t) (hi - lo)) + (uint32_t) lo);
    }

private:
    uint32_t state = 0;
};

// --- The firmware's quantizer (braids::Quantizer, the codebook-free one) -----------------------------------------
// Pitches in 1/128 semitone. Process() snaps a CV to the scale with hysteresis; Lookup() gives a note index's pitch,
// index 64 being 0 V.
class Quantizer
{
public:
    void init()
    {
        enabled = true;
        codeword = 0;
        transposeHeld = 0;
        previousBoundary = 0;
        nextBoundary = 0;
    }

    // Configure(scale, 0xffff). Only the scale's own notes are written: the rest of the array keeps what an earlier,
    // longer scale left there, which Lookup() can read (below).
    void configure (const ScaleDef& scale)
    {
        count = 0;
        uint16_t mask = 0xffff;
        for (uint16_t i = 0; i < scale.count; ++i)
        {
            if (mask & 1)
                notes[count++] = scale.notes[i];
            mask >>= 1;
        }
        span = scale.span;
        enabled = count != 0 && span != 0;
    }

    int32_t process (int32_t pitch, int32_t root, int32_t transpose)
    {
        if (! enabled)
            return pitch;
        pitch -= root;
        pitch -= (12 << 7) << 1;
        if (! requantize && pitch >= previousBoundary && pitch <= nextBoundary && transpose == transposeHeld)
            pitch = codeword; // still in the cell of the note it chose last
        else
        {
            requantize = false;
            int16_t octave = (int16_t) (pitch / span - (pitch < 0 ? 1 : 0));
            const int16_t relPitch = (int16_t) (pitch - span * octave);
            int16_t best = 16384;
            int16_t q = -1;
            for (int16_t i = 0; i < count; i++)
            {
                const int16_t distance = (int16_t) std::abs (relPitch - notes[i]);
                if (distance < best)
                {
                    best = distance;
                    q = i;
                }
            }
            if (std::abs (pitch - (octave + 1) * span - notes[0]) < best)
            {
                octave++;
                q = 0;
            }
            else if (std::abs (pitch - (octave - 1) * span - notes[count - 1]) <= best)
            {
                octave--;
                q = (int16_t) (count - 1);
            }
            q = (int16_t) (q + transpose);
            octave = (int16_t) (octave + q / count);
            q = (int16_t) (q % count);
            if (q < 0)
            {
                q = (int16_t) (q + count);
                octave--;
            }
            codeword = notes[q] + octave * span;
            previousBoundary = q == 0 ? notes[count - 1] + (octave - 1) * span : notes[q - 1] + octave * span;
            previousBoundary = (neighbourWeight * previousBoundary + (16 - neighbourWeight) * codeword) >> 4;
            nextBoundary = q == count - 1 ? notes[0] + (octave + 1) * span : notes[q + 1] + octave * span;
            nextBoundary = (neighbourWeight * nextBoundary + (16 - neighbourWeight) * codeword) >> 4;
            transposeHeld = transpose;
            pitch = codeword;
        }
        pitch += root;
        pitch += (12 << 7) << 1;
        return pitch;
    }

    // As the firmware: for an index an exact number of octaves below 64, its octave comes out one too low and its
    // place in the scale one past the last note, so it reads the array beyond the scale (what a longer scale left,
    // else 0) and lands about an octave lower than the octave asked for. TB-3PO hits this on every octave-down root
    // note. Past the 16th entry the module reads the count that follows the array in memory; so does this.
    int32_t lookup (int32_t index) const
    {
        index -= 64;
        const int16_t octave = (int16_t) (index / count - (index < 0 ? 1 : 0));
        const int16_t rel = (int16_t) (index - octave * count);
        const int32_t note = rel < 16 ? notes[rel] : (int32_t) count;
        return note + octave * span;
    }

    int noteCount() const { return count; }

private:
    static constexpr int32_t neighbourWeight = 10; // out of 16
    bool enabled = false, requantize = false;
    int32_t codeword = 0, transposeHeld = 0, previousBoundary = 0, nextBoundary = 0, span = 0;
    int16_t notes[16] {};
    uint8_t count = 0;
};

// --- The applet --------------------------------------------------------------------------------------------------
// Settings are set as the module's encoder and CV inputs would set them; reset() is a pulse on its Regen input and
// clock() is a pulse on its Clock input (with the step it takes after the ADC settles, at once: see Engine). The
// outputs: pitch (with the slide's ends) and gate in the module's units, 1/128 semitone and 3 V / 5 V.
class Tb3po
{
public:
    static constexpr int32_t gateAccent = 7680, gateNormal = 4608; // HEMISPHERE_MAX_CV (5 V), HEMISPHERE_3V_CV
    static constexpr int halfSteps = 16, maxSteps = 32;

    // Start(): the applet's first settings (the plug-in sets its own from the parameters right after).
    void start (uint32_t entropy)
    {
        rootSetting = 0;
        octaveOffset = 0;
        quantizer.init();
        setScale (appletDefaultScale);
        density = 12;
        numSteps = 16;
        gateCv = 0;
        pitch = slideStart = slideEnd = 0;
        locked = false;
        reseed (entropy);
        regenerate();
    }

    // --- Settings ---
    void setScale (int index)
    {
        scale = index < 0 ? 0 : (index >= scaleCount ? scaleCount - 1 : index);
        quantizer.configure (scaleTable[scale]);
        scaleSize = (uint8_t) scaleTable[scale].count;
    }
    // Root: the applet constrains it to the scale (a short scale has fewer roots). Differs: the plug-in keeps the
    // knob where it is and uses the constrained value; the applet moves its setting (so going back to a longer scale
    // keeps the lower root).
    void setRoot (int r) { rootSetting = r < 0 ? 0 : (r > 11 ? 11 : r); }
    void setOctave (int o) { octaveOffset = (int8_t) (o < -3 ? -3 : (o > 3 ? 3 : o)); }
    void setDensity (int d) { density = (uint8_t) (d < 0 ? 0 : (d > 14 ? 14 : d)); } // 0..14, shown as -7..+7
    void setSteps (int n) { numSteps = (uint8_t) (n < 1 ? 1 : (n > 32 ? 32 : n)); }
    void setSeed (uint16_t s) { seed = s; }
    void setLocked (bool on) { locked = on; }
    // CV 1 (transpose), in 1/128 semitone: DetentedIn() then quantized to the scale, always from C.
    void setTransposeInput (int32_t cv) { transposeCv = (cv > 64 || cv < -64) ? quantizer.process (cv, 0, 0) : 0; }

    // --- Inputs ---
    // The Regen input (or the encoder turned past the die or the lock): a new seed unless it is locked, the pattern
    // made again from the seed, and back to step 0 (the next clock plays step 1).
    //
    // Mutate (not the applet's; 0 is the applet): with a new seed, only this share of the steps take the new seed's
    // pattern and the rest keep the one playing, so a line changes a little at each reset. Which steps change comes
    // from the new seed, so the same seeds give the same result.
    void reset (uint32_t entropy, float mutate = 0.0f)
    {
        if (locked || mutate <= 0.0f)
        {
            if (! locked)
                reseed (entropy);
            regenerate();
            step = 0;
            return;
        }
        const Pattern before = pattern();
        reseed (entropy);
        regenerate();
        TeensyRandom pick;
        pick.randomSeed ((uint32_t) seed + 5u); // the applet seeds passes 1 to 4: this one is its own
        const int threshold = (int) std::lround (mutate * 1000.0f);
        for (int s = 0; s < maxSteps; ++s)
            if (pick.between (0, 1000) >= threshold)
                restoreStep (before, s);
        step = 0;
    }

    // One pattern's steps, to keep while another is made.
    struct Pattern
    {
        uint32_t gates, slides, accents, octUps, octDowns;
        uint8_t notes[maxSteps];
    };
    Pattern pattern() const
    {
        Pattern p { gates, slides, accents, octUps, octDowns, {} };
        std::copy (notes, notes + maxSteps, p.notes);
        return p;
    }

    // A clock: the pattern made again if what it is made from changed, then the next step.
    void clock()
    {
        // The applet regenerates when density or the scale's size changed. Differs: a new seed does too (on the
        // module a seed typed in waits for the next Regen pulse); the module's later firmware does the same.
        if (density != patternDensity || scaleSize != patternScaleSize || seed != patternSeed)
            regenerate();
        advance();
    }

    // Where the pattern would be `clocks` clocks after a reset (a Play or a jump between bar resets).
    void place (int64_t clocks) { step = (uint8_t) (clocks < 0 ? 0 : clocks % numSteps); }

    // The gate's timer, half a clock after a step opened it: the gate closes unless this step slides into the next.
    void halfStepPassed()
    {
        if (gateCv > 0 && ! slid (step))
            gateCv = 0;
    }
    // Differs: the transport stopping closes the gate (the module holds a sliding step's gate until a clock comes).
    void stop() { gateCv = 0; }

    // --- Outputs ---
    int32_t pitch = 0;                      // the pitch CV at the step (the slide's start when it slides)
    int32_t slideStart = 0, slideEnd = 0;   // the slide from the step before (equal: none)
    int32_t gateCv = 0;                     // 0, gateNormal or gateAccent
    bool opened = false;                    // this step set the gate (its half-step timer starts)

    // The applet's slide after `ticks` ticks of the module's 16.67 kHz update (the step's own tick is the first). It
    // was meant as an exponential glide; its integer arithmetic (the difference >> 18, times 3, each tick) moves down
    // by 3/128 semitone a tick and never up: an upward slide holds the step before's pitch. tests/reference.cpp holds
    // this against the applet's own code tick by tick.
    static int32_t slideAfter (int32_t start, int32_t end, int64_t ticks)
    {
        if (end >= start)
            return start; // up: nothing moves (equal: no slide)
        const int64_t at = (int64_t) start - 3 * ticks;
        return at < end ? end : (int32_t) at;
    }

    // --- What the pattern is (for the tests and the screen) ---
    int currentStep() const { return step; }
    uint16_t currentSeed() const { return seed; }
    uint16_t playingSeed() const { return patternSeed; } // the seed the pattern now playing was made from
    bool gated (int s) const { return (gates >> s) & 1u; }
    bool slid (int s) const { return (slides >> s) & 1u; }
    bool accented (int s) const { return (accents >> s) & 1u; }
    bool octaveUp (int s) const { return (octUps >> s) & 1u; }
    bool octaveDown (int s) const { return (octDowns >> s) & 1u; }
    int note (int s) const { return notes[s]; }
    int stepCount() const { return numSteps; }
    int32_t pitchOfStep (int s) const { return pitchFor (s); } // its pitch CV (1/128 semitone), for the screen
    int effectiveRoot() const
    {
        const int maxRoot = scaleSize > 12 ? 12 : scaleSize;
        return rootSetting > maxRoot - 1 ? maxRoot - 1 : rootSetting;
    }

    // The pattern from the seed: the applet's four passes (it spreads them over four updates), each seeding random()
    // with the seed plus the pass's number.
    void regenerate()
    {
        for (int phase = 1; phase <= 4; ++phase)
        {
            random.randomSeed ((uint32_t) seed + (uint32_t) phase);
            if (phase == 1 || phase == 3)
                regeneratePitches (phase);
            else
                applyDensity (phase);
        }
        patternSeed = seed;
    }

private:
    void reseed (uint32_t entropy)
    {
        random.randomSeed (entropy); // the module: micros()
        seed = (uint16_t) random.between (0, 65535);
    }

    int nextStep (int s) const { return ++s >= numSteps ? 0 : s; }

    void restoreStep (const Pattern& p, int s)
    {
        const uint32_t bit = 1u << s;
        auto keep = [bit] (uint32_t now, uint32_t was) { return (now & ~bit) | (was & bit); };
        gates = keep (gates, p.gates), slides = keep (slides, p.slides), accents = keep (accents, p.accents);
        octUps = keep (octUps, p.octUps), octDowns = keep (octDowns, p.octDowns);
        notes[s] = p.notes[s];
    }

    void advance()
    {
        const int prev = step;
        step = (uint8_t) nextStep (step);
        if (slid (prev))
        {
            // The slide goes from the step before's pitch to this one's.
            slideStart = pitchFor (prev);
            pitch = slideStart;
            slideEnd = pitchFor (step);
        }
        else
        {
            pitch = pitchFor (step);
            slideStart = pitch;
            slideEnd = pitch;
        }
        // The gate opens on a gated step, and stays open into the step after a slide.
        opened = false;
        if (gated (step) || slid (prev))
        {
            gateCv = accented (step) ? gateAccent : gateNormal;
            opened = true;
        }
    }

    int32_t pitchFor (int s) const
    {
        int quantNote = 64 + int (notes[s]) + effectiveRoot();
        quantNote += int (octaveOffset) * int (scaleSize);
        // An octave up or down is one span of the scale's notes, whatever the scale.
        if (octaveUp (s))
            quantNote += scaleSize;
        else if (octaveDown (s))
            quantNote -= scaleSize;
        const int outNote = quantNote < 0 ? 0 : (quantNote > 127 ? 127 : quantNote);
        return quantizer.lookup (outNote) + transposeCv;
    }

    // Proportion(): numerator / denominator * max in the applet's 18.14 fixed point, truncated as it truncates.
    static int proportion (int numerator, int denominator, int max)
    {
        const int32_t fraction = (int32_t) ((uint32_t) numerator << 14) / denominator;
        return (fraction * max) >> 14;
    }

    // A percentage chance: random(1, 100) is 1..99.
    int randBit (int probability) { return random.between (1, 100) <= probability ? 1 : 0; }

    // Density 0..14 is -7..+7: its distance from 0 sets how many steps play...
    int onOffDensity() const { return std::abs (int (density) - 7); }
    // ...and its left half how many pitches are used and how often a pitch repeats.
    int pitchChangeDensity() const { return density > 8 ? 8 : density; }

    void regeneratePitches (int phase)
    {
        const bool firstHalf = phase < 3;
        const int pitchChangeDens = pitchChangeDensity();
        int available = 0;
        if (scaleSize > 0)
        {
            if (pitchChangeDens > 7)
                available = scaleSize - 1;
            else if (pitchChangeDens < 2)
                available = pitchChangeDens; // the root alone, then the root and the next note (303 half steps)
            else
            {
                int rangeFromScale = scaleSize - 3;
                if (rangeFromScale < 4)
                    rangeFromScale = 4;
                available = 3 + proportion (pitchChangeDens - 3, 4, rangeFromScale);
                available = available < 1 ? 1 : (available > scaleSize - 1 ? scaleSize - 1 : available);
            }
        }
        if (firstHalf)
            octUps = octDowns = 0;
        const int maxStep = firstHalf ? halfSteps : maxSteps;
        for (int s = firstHalf ? 0 : halfSteps; s < maxStep; s++)
        {
            // The lower the pitch density, the likelier a step repeats the note before.
            const int forceRepeatNoteProb = 50 - pitchChangeDens * 6;
            if (s > 0 && randBit (forceRepeatNoteProb))
                notes[s] = notes[s - 1];
            else
            {
                notes[s] = (uint8_t) random.between (0, available + 1);
                // The octave bits move on only for a new note, so they do not line up with the steps they are read
                // at: the applet reads bit s for step s all the same.
                octUps <<= 1;
                octDowns <<= 1;
                if (randBit (40))
                {
                    if (randBit (50))
                        octUps |= 0x1;
                    else
                        octDowns |= 0x1;
                }
            }
        }
        if (scaleSize == 0)
            scaleSize = 12;
        patternScaleSize = scaleSize;
    }

    void applyDensity (int phase)
    {
        int latestSlide = 0, latestAccent = 0;
        const int onOffDens = onOffDensity();
        const int densProb = 10 + onOffDens * 14; // 10 % .. 108 %
        if (phase < 3)
            gates = slides = accents = 0;
        for (int i = 0; i < halfSteps; ++i)
        {
            gates <<= 1;
            gates |= (uint32_t) randBit (densProb);
            // Two slides, or two accents, running are less likely.
            slides <<= 1;
            latestSlide = randBit (latestSlide ? 10 : 18);
            slides |= (uint32_t) latestSlide;
            accents <<= 1;
            latestAccent = randBit (latestAccent ? 7 : 16);
            accents |= (uint32_t) latestAccent;
        }
        patternDensity = density;
    }

    TeensyRandom random;
    Quantizer quantizer;
    bool locked = false;
    uint16_t seed = 0, patternSeed = 0;
    int scale = 0;
    int rootSetting = 0;
    int8_t octaveOffset = 0;
    uint8_t density = 12, patternDensity = 255;
    uint8_t numSteps = 16;
    uint8_t step = 0;
    int32_t transposeCv = 0;
    uint32_t gates = 0, slides = 0, accents = 0, octUps = 0, octDowns = 0;
    uint8_t notes[maxSteps] {};
    uint8_t scaleSize = 0, patternScaleSize = 0;
};
} // namespace tbmpc
