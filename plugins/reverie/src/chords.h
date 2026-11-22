// Reverie's chords: the qualities, the progressions parsed, their names spelled in the key, and the voicings the
// pads play.
//
// A voicing is a bass note (the chord's root, or the slash's bass) and an upper structure: the chord's most telling
// tones (third, seventh, then the colour tones; the root left to the bass) placed in the style's register. Of every
// way to place them, the one chosen keeps the intervals open in the low register, avoids minor ninths, keeps the
// style's spacing and moves least from the chord before it (the progression is voiced in order, twice round, so the
// eighth chord also leads into the first). Pads 1-8 play the progression; pads 9-16 the same chords lifted: one
// colour tone more, a fifth or so higher.
#pragma once

#include "params.h"

#include <array>
#include <cstdint>
#include <cstring>

namespace reverie
{
struct Quality
{
    const char* suffix;
    int8_t tones[6]; // above the root, most telling first
    uint8_t count;
    bool flat9;      // minor ninths belong to it (b9 chords)
};

inline constexpr Quality qualities[] = {
    { "", { 4, 7, 0 }, 3, false },
    { "maj7", { 4, 11, 7, 0 }, 4, false },
    { "maj9", { 4, 11, 2, 7, 0 }, 5, false },
    { "maj13", { 4, 11, 2, 9, 7 }, 5, false },
    { "maj9#11", { 4, 11, 2, 6, 7 }, 5, false },
    { "maj7#11", { 4, 11, 6, 7, 0 }, 5, false },
    { "6/9", { 4, 9, 2, 7, 0 }, 5, false },
    { "add9", { 4, 2, 7, 0 }, 4, false },
    { "sus2", { 2, 7, 0 }, 3, false },
    { "m", { 3, 7, 0 }, 3, false },
    { "m7", { 3, 10, 7, 0 }, 4, false },
    { "m9", { 3, 10, 2, 7, 0 }, 5, false },
    { "m11", { 3, 10, 5, 2, 7 }, 5, false },
    { "m13", { 3, 10, 2, 9, 7 }, 5, false },
    { "m6/9", { 3, 9, 2, 7, 0 }, 5, false },
    { "mMaj7", { 3, 11, 7, 0 }, 4, false },
    { "mMaj9", { 3, 11, 2, 7 }, 4, false },
    { "madd9", { 3, 2, 7, 0 }, 4, false },
    { "m7b5", { 3, 10, 6, 0 }, 4, false },
    { "m11b5", { 3, 10, 6, 5 }, 4, false },
    { "7", { 4, 10, 7, 0 }, 4, false },
    { "9", { 4, 10, 2, 7, 0 }, 5, false },
    { "13", { 4, 10, 2, 9, 7 }, 5, false },
    { "7#9", { 4, 10, 3, 7 }, 4, false },
    { "7b9", { 4, 10, 1, 7 }, 4, true },
    { "7b13", { 4, 10, 8, 0 }, 4, false },
    { "9b13", { 4, 10, 2, 8 }, 4, false },
    { "7alt", { 4, 10, 3, 8 }, 4, false },
    { "13b9", { 4, 10, 1, 9 }, 4, true },
    { "9#11", { 4, 10, 2, 6 }, 4, false },
    { "13#11", { 4, 10, 2, 6, 9 }, 5, false },
    { "7sus", { 5, 10, 7, 0 }, 4, false },
    { "9sus", { 5, 10, 2, 7, 0 }, 5, false },
    { "13sus", { 5, 10, 2, 9, 7 }, 5, false },
    { "dim7", { 3, 6, 9, 0 }, 4, false },
    { "dim", { 3, 6, 0 }, 3, false },
    { "aug", { 4, 8, 0 }, 3, false },
};
constexpr int qualityCount = (int) (sizeof (qualities) / sizeof (qualities[0]));

// A chord as written (in C): its root and bass by letter (0 = C .. 6 = B) and pitch class.
struct ChordSymbol
{
    int8_t rootLetter = 0, rootPc = 0, bassLetter = -1, bassPc = -1;
    uint8_t quality = 0;
};

constexpr int naturalPc[7] = { 0, 2, 4, 5, 7, 9, 11 };
constexpr char letterName[] = "CDEFGAB";

// "Abmaj9#11/C" -> its symbol; false for anything else.
inline bool parseChord (const char* s, size_t len, ChordSymbol& out)
{
    auto note = [] (const char* t, size_t n, size_t& used, int8_t& letter, int8_t& pc) {
        if (n == 0) return false;
        const char* at = std::strchr (letterName, t[0]);
        if (at == nullptr || t[0] == 0) return false;
        letter = (int8_t) (at - letterName);
        int p = naturalPc[letter];
        used = 1;
        if (n > 1 && t[1] == '#') p += 1, used = 2;
        else if (n > 1 && t[1] == 'b') p -= 1, used = 2;
        pc = (int8_t) ((p + 12) % 12);
        return true;
    };
    size_t used = 0;
    if (! note (s, len, used, out.rootLetter, out.rootPc)) return false;
    size_t end = len;
    out.bassLetter = out.bassPc = -1;
    for (size_t i = len; i-- > used;)
        if (s[i] == '/' && i + 1 < len && std::strchr (letterName, s[i + 1]) != nullptr && s[i + 1] != 0)
        {
            size_t bassUsed = 0;
            if (! note (s + i + 1, len - i - 1, bassUsed, out.bassLetter, out.bassPc) || i + 1 + bassUsed != len) return false;
            end = i;
            break;
        }
    const size_t qlen = end - used;
    for (int q = 0; q < qualityCount; ++q)
        if (std::strlen (qualities[q].suffix) == qlen && std::strncmp (qualities[q].suffix, s + used, qlen) == 0)
        {
            out.quality = (uint8_t) q;
            return true;
        }
    return false;
}

struct ParsedProgression
{
    ChordSymbol chords[chordsPerProgression];
    bool ok = false;
};

// Every progression parsed once (the tests check each parses).
inline const ParsedProgression* parsedProgressions()
{
    static const auto table = [] {
        std::array<ParsedProgression, progressionCount> t {};
        for (int p = 0; p < progressionCount; ++p)
        {
            const char* s = progressionTable[p].chords;
            int n = 0;
            bool ok = true;
            while (*s)
            {
                while (*s == ' ') ++s;
                if (! *s) break;
                const char* e = s;
                while (*e && *e != ' ') ++e;
                if (n < chordsPerProgression) ok &= parseChord (s, (size_t) (e - s), t[p].chords[n]);
                ++n;
                s = e;
            }
            t[p].ok = ok && n == chordsPerProgression;
        }
        return t;
    }();
    return table.data();
}

// --- Names ------------------------------------------------------------------------------------------------------------

// The Key parameter's tonic as a letter: a major key as the parameter names it (C, Db, D, Eb, E, F, F#, G, Ab, A,
// Bb, B); a minor key as it is usually written (C#m and G#m, not Dbm and Abm).
constexpr int8_t keyLetter[2][12] = { { 0, 1, 1, 2, 2, 3, 3, 4, 5, 5, 6, 6 }, { 0, 0, 1, 2, 2, 3, 3, 4, 4, 5, 6, 6 } };
inline const char* keyName (int key, bool minor)
{
    static const char* const major[] = { "C", "Db", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" };
    static const char* const minorNames[] = { "Cm", "C#m", "Dm", "Ebm", "Em", "Fm", "F#m", "Gm", "G#m", "Am", "Bbm", "Bm" };
    return minor ? minorNames[((key % 12) + 12) % 12] : major[((key % 12) + 12) % 12];
}

// A note written in C as `letter` / `pc`, spelled in the key: its letter moved as far as the key's, its accidental
// whatever that makes, with double accidentals and Cb, Fb, E#, B# written the plain way.
inline void spell (int letter, int pc, int key, bool minor, char* out)
{
    int l = (letter + keyLetter[minor ? 1 : 0][key % 12]) % 7;
    const int p = (pc + key) % 12;
    auto accidental = [&] (int ll) { int a = p - naturalPc[ll]; a = ((a + 6) % 12 + 12) % 12 - 6; return a; };
    int a = accidental (l);
    if (a > 1 || a < -1 || (a == -1 && (l == 0 || l == 3)) || (a == 1 && (l == 2 || l == 6)))
    {
        // the nearest letter with the smallest accidental (a flat preferred to a sharp)
        int best = l, bestA = 99;
        for (int d : { -1, 1, 0 })
        {
            const int ll = (l + d + 7) % 7, aa = accidental (ll);
            const bool odd = (aa == -1 && (ll == 0 || ll == 3)) || (aa == 1 && (ll == 2 || ll == 6));
            const int cost = std::abs (aa) * 2 + (odd ? 3 : 0) + (aa > 0 ? 1 : 0);
            if (cost < bestA) bestA = cost, best = ll;
        }
        l = best, a = accidental (l);
    }
    int i = 0;
    out[i++] = letterName[l];
    if (a == 1) out[i++] = '#';
    if (a == -1) out[i++] = 'b';
    out[i] = 0;
}

// The chord's root (with its slash bass, "C/G") and its quality as the page shows them.
inline void chordRootText (const ChordSymbol& c, int key, bool minor, char* out, size_t size)
{
    char root[4], bass[4];
    spell (c.rootLetter, c.rootPc, key, minor, root);
    if (c.bassPc < 0) { std::snprintf (out, size, "%s", root); return; }
    spell (c.bassLetter, c.bassPc, key, minor, bass);
    std::snprintf (out, size, "%s/%s", root, bass);
}
inline const char* chordQualityText (const ChordSymbol& c) { return qualities[c.quality].suffix; }
inline void chordText (const ChordSymbol& c, int key, bool minor, char* out, size_t size)
{
    char root[4], bass[4];
    spell (c.rootLetter, c.rootPc, key, minor, root);
    if (c.bassPc < 0) { std::snprintf (out, size, "%s%s", root, qualities[c.quality].suffix); return; }
    spell (c.bassLetter, c.bassPc, key, minor, bass);
    std::snprintf (out, size, "%s%s/%s", root, qualities[c.quality].suffix, bass);
}

// --- Voicings ---------------------------------------------------------------------------------------------------------

constexpr int maxChordNotes = 8;
constexpr int padSlots = 16;
constexpr int bassLow = 34; // the bass between Bb1 and A2

struct ChordNotes
{
    int8_t note[maxChordNotes] {}; // ascending; note[0] is the bass when hasBass
    uint8_t count = 0;
    bool hasBass = false;
};

struct VoicingSet
{
    ChordNotes slot[padSlots];
};

namespace detail
{
struct Style
{
    int upper;               // tones above the bass
    int lo, hi;              // where they may go
    float centre;            // where their middle should sit
    int maxGap, maxSpan;     // hard limits (search)
    int softSpan, minSpan;   // the span it likes
    bool bass;
};

inline Style styleOf (int voicing, bool lift)
{
    Style s {};
    switch (voicing)
    {
        default:
        case Voicing::neoSoul: s = { 4, 50, 76, 63.0f, 9, 19, 14, 0, true }; break;
        case Voicing::open: s = { 5, 45, 86, 65.0f, 13, 34, 30, 15, true }; break;
        case Voicing::close: s = { 4, 52, 74, 62.0f, 7, 15, 12, 0, true }; break;
        case Voicing::air: s = { 4, 60, 91, 75.0f, 11, 24, 20, 0, true }; break;
        case Voicing::quartal: s = { 4, 50, 81, 64.0f, 9, 22, 19, 0, true }; break;
        case Voicing::rootless: s = { 5, 50, 79, 63.0f, 10, 21, 16, 0, false }; break;
    }
    if (lift)
    {
        s.upper += 1, s.lo += 5, s.hi += 8, s.centre += 6.0f;
        s.maxSpan += 4, s.softSpan += 4;
    }
    return s;
}

struct Search
{
    const Style* style;
    int voicing = 0;
    int cand[48], candPc[48], candCount = 0;
    int pcs[maxChordNotes], k = 0;
    int bassNote = -1;
    int root = 0;
    bool flat9 = false;
    const int* prev = nullptr;
    int prevCount = 0;
    int pick[maxChordNotes];
    int best[maxChordNotes];
    float bestScore = 1e30f;

    float score (const int* u) const
    {
        const Style& st = *style;
        float s = 0.0f;
        const int span = u[k - 1] - u[0];
        for (int i = 0; i + 1 < k; ++i)
        {
            const int d = u[i + 1] - u[i];
            if (d == 1) s += u[i] < 60 ? 14.0f : 10.0f;
            else if (d == 2) s += u[i] < 55 ? 4.0f : 0.4f;
            if (u[i] < 52 && d < 5) s += 4.0f;
            if (u[i] < 48 && d < 7) s += 4.0f;
            switch (voicing)
            {
                case Voicing::open:
                    if (d < 3) s += 3.0f;
                    if (d > 9) s += (float) (d - 9) * 0.7f;
                    break;
                case Voicing::air: if (d > 9) s += (float) (d - 9) * 0.5f; break;
                case Voicing::quartal: s += d == 5 ? -1.6f : (d == 6 || d == 7) ? -0.6f : (d <= 2 ? 2.5f : 1.0f); break;
                default: if (d > 7) s += (float) (d - 7) * 0.8f; break;
            }
        }
        for (int i = 0; i < k; ++i)
            for (int j = i + 1; j < k; ++j)
            {
                const int iv = u[j] - u[i];
                if (iv > 12 && iv % 12 == 1) s += flat9 ? 1.5f : 7.0f;
            }
        if (span > st.softSpan) s += (float) (span - st.softSpan) * 1.2f;
        if (span < st.minSpan) s += (float) (st.minSpan - span) * 0.8f;
        if (bassNote >= 0)
        {
            const int gap = u[0] - bassNote;
            if (gap < 3) s += 8.0f;
            if (u[0] < 55 && gap < 7) s += 4.0f;
            if (u[0] < 55 && (gap % 12 == 1 || gap % 12 == 2)) s += 3.0f;
        }
        float mean = 0.0f;
        for (int i = 0; i < k; ++i) mean += (float) u[i];
        mean /= (float) k;
        s += std::fabs (mean - st.centre) * 0.35f;
        if (prev != nullptr && prevCount > 0)
        {
            float vl = 0.0f;
            if (prevCount == k)
                for (int i = 0; i < k; ++i) vl += (float) std::abs (u[i] - prev[i]);
            else
            {
                for (int i = 0; i < k; ++i)
                {
                    int m = 99;
                    for (int j = 0; j < prevCount; ++j) m = std::min (m, std::abs (u[i] - prev[j]));
                    vl += (float) m;
                }
                for (int j = 0; j < prevCount; ++j)
                {
                    int m = 99;
                    for (int i = 0; i < k; ++i) m = std::min (m, std::abs (u[i] - prev[j]));
                    vl += (float) m;
                }
                vl *= (float) k / (float) (k + prevCount);
            }
            s += vl * 0.35f;
            s += (float) std::abs (u[k - 1] - prev[prevCount - 1]) * 0.25f;
        }
        const int top = ((u[k - 1] - root) % 12 + 12) % 12;
        if (top == 3 || top == 4) s += 0.8f;
        else if (top == 0) s += 0.6f;
        else if (top == 1) s += 1.5f;
        return s;
    }

    void walk (int from, int depth, unsigned used)
    {
        if (depth == k)
        {
            const float s = score (pick);
            if (s < bestScore)
            {
                bestScore = s;
                std::memcpy (best, pick, sizeof (int) * (size_t) k);
            }
            return;
        }
        for (int c = from; c < candCount; ++c)
        {
            const int n = cand[c];
            if (depth > 0)
            {
                if (n - pick[depth - 1] > style->maxGap) break;
                if (n - pick[0] > style->maxSpan) break;
            }
            if (used & (1u << candPc[c])) continue;
            pick[depth] = n;
            walk (c + 1, depth + 1, used | (1u << candPc[c]));
        }
    }
};

inline int mod12 (int x) { return ((x % 12) + 12) % 12; }

// One chord voiced; `prev` (the upper structure before it) may be empty.
inline ChordNotes voiceChord (const ChordSymbol& c, int voicing, int key, bool lift, const ChordNotes* prev)
{
    const Quality& q = qualities[c.quality];
    const Style st = styleOf (voicing, lift);
    const int root = mod12 (c.rootPc + key);
    const int bass = c.bassPc >= 0 ? mod12 (c.bassPc + key) : root;
    const bool slash = bass != root;
    int list[8], n = 0;
    for (int i = 0; i < q.count; ++i)
    {
        const int pc = mod12 (root + q.tones[i]);
        if (! slash && q.count > 3 && pc == root) continue; // the root is the bass's
        if (slash && pc == bass) continue;                   // the bass has it
        list[n++] = pc;
    }
    if (slash)
    {
        bool hasRoot = false;
        for (int i = 0; i < n; ++i) hasRoot |= list[i] == root;
        if (! hasRoot)
        {
            const int at = std::min (n, 2);
            for (int i = n; i > at; --i) list[i] = list[i - 1];
            list[at] = root;
            ++n;
        }
    }
    Search s;
    s.style = &st;
    s.voicing = voicing;
    s.k = std::min (st.upper, n);
    s.root = root;
    s.flat9 = q.flat9;
    for (int i = 0; i < s.k; ++i) s.pcs[i] = list[i];
    const int bassNote = bassLow + mod12 (bass - bassLow);
    s.bassNote = st.bass ? bassNote : -1;
    for (int note = st.lo; note <= st.hi && s.candCount < 48; ++note)
        for (int i = 0; i < s.k; ++i)
            if (mod12 (note) == s.pcs[i]) s.cand[s.candCount] = note, s.candPc[s.candCount++] = i;
    int prevUpper[maxChordNotes];
    if (prev != nullptr && prev->count > 0)
    {
        const int from = prev->hasBass ? 1 : 0;
        for (int i = from; i < prev->count; ++i) prevUpper[s.prevCount++] = prev->note[i];
        s.prev = prevUpper;
    }
    s.walk (0, 0, 0);
    if (s.bestScore >= 1e29f) // nothing within the limits (cannot happen with these ranges): close position from lo
    {
        int at = st.lo;
        for (int i = 0; i < s.k; ++i)
        {
            while (mod12 (at) != s.pcs[i]) ++at;
            s.best[i] = at;
        }
        std::sort (s.best, s.best + s.k);
    }
    ChordNotes out;
    if (st.bass) out.note[out.count++] = (int8_t) bassNote, out.hasBass = true;
    for (int i = 0; i < s.k; ++i) out.note[out.count++] = (int8_t) s.best[i];
    return out;
}
} // namespace detail

// The sixteen pads of a progression in a voicing style and key: 1-8 the chords, 9-16 lifted.
inline void voiceProgression (int progression, int voicing, int key, VoicingSet& out)
{
    const ParsedProgression& p = parsedProgressions()[std::clamp (progression, 0, progressionCount - 1)];
    voicing = std::clamp (voicing, 0, Voicing::count - 1);
    key = detail::mod12 (key);
    for (int lift = 0; lift < 2; ++lift)
    {
        ChordNotes chain[chordsPerProgression];
        const ChordNotes* prev = nullptr;
        for (int pass = 0; pass < 2; ++pass)
            for (int i = 0; i < chordsPerProgression; ++i)
            {
                chain[i] = detail::voiceChord (p.chords[i], voicing, key, lift != 0, prev);
                prev = &chain[i];
            }
        for (int i = 0; i < chordsPerProgression; ++i) out.slot[8 * lift + i] = chain[i];
    }
}

// Which pad slot a key plays, and how far it moves it: pads A01-A16 (keys 36-51) the sixteen; the banks above and
// below the same sixteen, an octave up a bank (at most two).
struct PadKey { int slot; int shift; };
inline PadKey padOf (int key)
{
    const int from = key - 36;
    const int bank = from >= 0 ? from / padSlots : -((-from + padSlots - 1) / padSlots);
    return { from - bank * padSlots, 12 * std::clamp (bank, -2, 2) };
}
} // namespace reverie
