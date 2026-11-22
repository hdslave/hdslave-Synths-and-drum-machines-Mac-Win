// Voices every progression in every voicing style and key, the sixteen pads of each, and writes them as the table
// the plug-in reads (src/voicings.h): the search for a voicing (chords.h) runs here, when Reverie is built, never on
// the audio thread. The Makefile builds and runs it with the plug-in's own flags.
//   voicings > build/voicings.inc
#include "../src/chords.h"

#include <cstdio>

int main()
{
    using namespace reverie;
    std::printf ("// Made by tools/voicings.cpp from src/chords.h when Reverie is built (do not edit): for each progression,\n"
                 "// voicing and key, the sixteen pads' chords, eight bytes each: the note count (bit 7: the first is the\n"
                 "// bass), then the notes, ascending.\n");
    std::printf ("inline constexpr unsigned char voicingBytes[] = {\n");
    for (int p = 0; p < progressionCount; ++p)
        for (int v = 0; v < Voicing::count; ++v)
            for (int k = 0; k < 12; ++k)
            {
                VoicingSet set;
                voiceProgression (p, v, k, set);
                for (const ChordNotes& c : set.slot)
                {
                    if (c.count < 1 || c.count > 7)
                    {
                        std::fprintf (stderr, "voicings: progression %d voicing %d key %d has %d notes (1 to 7 fit)\n", p, v, k, c.count);
                        return 1;
                    }
                    std::printf ("%d,", c.count | (c.hasBass ? 0x80 : 0));
                    for (int i = 0; i < 7; ++i) std::printf ("%d,", i < c.count ? c.note[i] : 0);
                }
                std::printf ("\n");
            }
    std::printf ("};\n");
    return 0;
}
