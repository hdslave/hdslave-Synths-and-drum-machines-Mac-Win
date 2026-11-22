// Reverie's chord progressions: eight chords each, written in C (major or minor) as chord symbols, moved to the Key
// parameter when they play. Pads A01-A08 play them in order; A09-A16 play the same chords lifted (chords.h). Each
// loops: the eighth chord leads back to the first. The order is fixed (a project saves the progression by its
// place): new progressions go at the end.
//
// Symbols: a root (C, Db, F#...), a quality (chords.h: maj9, m11, 13sus, 7#9, 6/9...) and an optional bass after a
// slash (Cmaj9/G). The tests parse every one.
#pragma once

namespace reverie
{
struct ProgressionText
{
    const char* name;
    bool minor;          // in C minor (the key's minor), else C major
    const char* chords;  // eight symbols
};

inline constexpr ProgressionText progressionTable[] = {
    // Major keys
    { "Velvet Night", false, "Fmaj9 Em9 Dm9 G13 Cmaj9 Am9 Dm9 G9sus" },
    { "Moonlit Two-Five", false, "Dm9 G13 Cmaj9 Am9 Dm9 Db9#11 Cmaj9 A7#9" },
    { "Sunday Sanctuary", false, "Fmaj9 F#dim7 Cmaj9/G A7b13 Dm9 G13sus G13 Cmaj9" },
    { "Tidal Hearts", false, "Fmaj9 E7#9 Am9 Gm9 C13 Fmaj9 E7alt Am9" },
    { "Backdoor Bliss", false, "Cmaj9 Fmaj9 Fm9 Bb13 Cmaj9 Am9 Dm9 G7b9" },
    { "Aurora", false, "Cmaj9#11 Emaj9 Abmaj9 Fm9 Dbmaj9#11 Ebmaj9 Bb13sus Bb13" },
    { "Starlight Lydian", false, "Cmaj9#11 D9/C Bm9 Em9 Am9 D13 Gmaj9 G9sus" },
    { "Rose Quartz", false, "Cmaj9 Bm7b5 E7b13 Am9 Gm9 C13 Fmaj9 Fm6/9" },
    { "Honey Drip", false, "Em9 A13 Dm9 G13 Cmaj9 Fmaj9#11 F#m7b5 B7#9" },
    { "Neon Dusk", false, "Fmaj9 G13 Em9 Am9 Dm9 G13 Cmaj9 C9" },
    { "Halo Ballad", false, "Cmaj9 Gadd9/B Am9 Am9/G Fmaj9 Cmaj9/E Dm9 G13sus" },
    { "Late Night Drive", false, "Dm9 Em9 Fmaj9 Em9 Dm9 G13 Cmaj9 A7b13" },
    { "Crystal Cave", false, "Am9 Fmaj9#11 Cmaj9 Gadd9/B Am9 Dm11 E7sus E7b9" },
    { "Golden Hour", false, "Cmaj9 Gm9 C13 Fmaj9 Fm9 Bb13 Ebmaj9 G7alt" },
    { "Cotton Clouds", false, "Cmaj9 Em9 Fmaj9 Fm6/9 Em7b5 A7b13 Dm9 G13sus" },
    { "Bossa Moon", false, "Cmaj9 C#dim7 Dm9 D#dim7 Em9 A7b13 Dm9 G13" },
    { "Celestial Pedal", false, "Cmaj9 Dadd9/C Bbmaj9/C Fmaj9/C Abmaj9#11/C Bbmaj9/C Fm6/9/C Cmaj9#11" },
    { "Silhouette", false, "Fmaj9 Fm9 Em9 Eb9#11 Dm9 Db9#11 Cmaj9 G9sus" },
    { "Neo Classic", false, "Cmaj9 A7#9 Dm9 G7b13 Em9 A13 Dm9 G13sus" },
    { "Prism", false, "Dm11 Em11 Fmaj9#11 G13sus Am11 G13sus Fmaj9#11 Em11" },
    { "Ocean Floor", false, "Cmaj9 Bbmaj9#11 Abmaj9 Bb13sus Cmaj9#11 Gm9 Fmaj9 Fm6/9" },
    { "Sanctuary Steps", false, "Cmaj9 C13 Fmaj9 F#dim7 Cmaj9/G A7#9 Dm9 G13b9" },
    { "Afterglow", false, "Cmaj9 G6/9/B Am9 Fmaj9#11 Cmaj9 Gadd9/B Am11 Fm6/9" },
    { "Firefly", false, "Cmaj9 E7b13 Am9 C13 Fmaj9 D13 Dm9 G7b9" },
    { "Dorian Drift", false, "Dm9 G13 Dm11 G13sus Em9 A13 Dm9 G13" },
    { "Glass Garden", false, "Fmaj7#11 Em9 Ebmaj9#11 Dm9 Dbmaj9#11 Cmaj9 Bm7b5 E7alt" },
    { "Silver Lining", false, "Fmaj9 Em9 A7b13 Dm9 G13sus Cmaj9/E A7#9 Dm11" },
    { "Velvet Rope", false, "Am9 Abmaj9#11 Gm9 C13 Fmaj9 E7#9 Am9 D9" },
    { "Heavenly Steps", false, "Cmaj9 Ebdim7 Dm9 G13 Em11 A7b13 Dm11 G13sus" },
    { "Paper Lanterns", false, "Cmaj9 Am11 Fmaj9 G13sus Em9 Am11 Dm9 Fm6/9" },
    { "Daydream Descent", false, "Dm9 Db13 Cmaj9 B7alt Bbmaj9#11 A7#9 Abmaj9 G13" },
    // Minor keys
    { "Amethyst", true, "Cm9 F13 Ebmaj9 Abmaj9#11 Dm7b5 G7b13 Cm11 Bb13sus" },
    { "Midnight Dorian", true, "Cm11 F13 Bbmaj9 Ebmaj9 Abmaj9#11 Dm7b5 G7#9 G7b9" },
    { "Smoke and Velvet", true, "Cm9 Abmaj9 Ebmaj9 Bb13sus Fm9 Gm9 Abmaj9 G7alt" },
    { "Obsidian", true, "Cm9 Dbmaj9#11 Bbm9 Abmaj9 Fm9 Dbmaj9 Dm7b5 G7b9" },
    { "Rainy Neo Soul", true, "Cm9 Fm9 Bb13 Ebmaj9 Abmaj9 Fm9 Dm7b5 G7#9" },
    { "Line Cliche", true, "Cmadd9 CmMaj9 Cm9 Cm6/9 Abmaj9 Fm9 Dm7b5 G7b9" },
    { "Nocturne", true, "Cm9 Abmaj9#11 Fm9 G7b13 Ebmaj9 Abmaj9 Dm7b5 G7#9" },
    { "Sapphire", true, "Dm7b5 G7b13 Cm9 C7#9 Fm9 Bb13 Ebmaj9 Abmaj9" },
    { "Witching Hour", true, "CmMaj9 Abmaj9#11 Fm9 Dbmaj9#11 Bbm9 Ebmaj9 Abmaj9 G7alt" },
    { "Rose Petal", true, "Cm9 Bb6/9 Abmaj9 G7#9 Cm9 Bb13sus Abmaj9#11 G7b13" },
    { "Lunar Tide", true, "Cm11 Abmaj9 Ebmaj9 Bb6/9 Fm9 Gm9 Abmaj9 Bb13sus" },
    { "Deep Indigo", true, "Cm9 Bbm9 Abmaj9 Gm9 Fm9 Gm9 Abmaj9 Bb13sus" },
    { "Phantom Waltz", true, "Cm9 G7b13/B Cm9/Bb Am7b5 Abmaj9#11 Fm9 Dm7b5 G7b9" },
    { "Echo Chamber", true, "Cm9 F13 Cm11 F13sus Abmaj9 Gm9 Fm9 Bb13" },
    { "Moonstone", true, "Fm9 Bb13 Ebmaj9 Abmaj9 Dm7b5 G7alt Cm9 C7#9" },
};
constexpr int progressionCount = (int) (sizeof (progressionTable) / sizeof (progressionTable[0]));
constexpr int chordsPerProgression = 8;

struct ProgressionNames
{
    const char* list[progressionCount];
    constexpr ProgressionNames() : list {}
    {
        for (int i = 0; i < progressionCount; ++i) list[i] = progressionTable[i].name;
    }
};
inline constexpr ProgressionNames progressionNameList {};
inline const char* const* const progressionNames = progressionNameList.list;
} // namespace reverie
