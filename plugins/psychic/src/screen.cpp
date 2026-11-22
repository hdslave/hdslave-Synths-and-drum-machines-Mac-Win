#include "screen.h"
#include "ui/masthead.h"
#include "ui/orbit.h"

namespace psychic
{
namespace
{
using hdui::Ink;
using hdui::Raster;
using namespace ParamId;
constexpr Ink ground = 0xff0c0b1b, film = 0xff101023, panel = 0xff26213c, rule = 0xff615986;
constexpr Ink paper = 0xffd9d7f2, light = 0xfff4f1ff, muted = 0xffbdb6d8, lime = 0xffd6fa51;
constexpr Ink carbon = 0xff211a35, berry = 0xffa20d65, pencil = 0xff9c96b7, blueInk = 0xff285f83;
constexpr Ink pink = 0xffff72bc, ice = 0xffa9c6ff, violet = 0xff7e71c4;
constexpr Ink hitInk[] = {carbon, 0xff672d72, berry, 0xffc11c7e, 0xffde2087};
constexpr uint32_t actions[] = {bitShift1, bitShift2, save, recall};
constexpr const char* actionLabels[] = {"SHIFT A", "SHIFT B", "SAVE", "RECALL"};
constexpr int cellCount = 16, choiceCount = cellCount + int (std::size (actions));
constexpr int headerH = 42, topH = 60, bodyH = 86, footH = 30;
static_assert (headerH + topH + bodyH + footH == hdui::height);

uint32_t cvId (int cell)
{
    return (cell < 8 ? cv1First : cv2First) + cell % 8;
}

uint32_t bitsId (int lane)
{
    return lane ? bits2 : bits1;
}

const Param& parameter (uint32_t id)
{
    return paramTable[paramIndex (id)];
}

bool bit (int mask, int column)
{
    return (mask & (1 << column)) != 0;
}

int wrap (int value, int32_t delta, int n)
{
    return int (((int64_t (value) + delta) % n + n) % n);
}

void large (Raster& r, int x, int y, const char* text, Ink color)
{
    for (; *text; ++text, x += 14)
    {
        const auto& glyph = hdui::font::glyphs[static_cast<unsigned char> (*text) - 32];
        for (int j = 0; j < hdui::font::glyphHeight; ++j)
            for (int k = 0; k < hdui::font::glyphWidth; ++k)
                if (glyph[j] & (1 << k)) r.box (x + 2 * k, y + 2 * j, 2, 2, color);
    }
}

void diamond (Raster& r, int x, int y, Ink color)
{
    for (int j = -2; j <= 2; ++j)
        r.box (x - 2 + std::abs (j), y + j, 5 - 2 * std::abs (j), 1, color);
}

void cellLabel (char (&text)[16], int cell)
{
    std::snprintf (text, sizeof (text), "%c%02d", cell < 8 ? 'A' : 'B', cell % 8 + 1);
}

void voltage (char (&text)[32], float value)
{
    std::snprintf (text, sizeof (text), "%+.2f V", 5 * value);
}

void shortValue (uint32_t id, float value, char (&text)[48])
{
    const Param& p = parameter (id);
    if (p.unit == Unit::percent)
        std::snprintf (text, sizeof (text), "%.0f%%", value * 100);
    else
        formatParam (p, value, text, sizeof (text));
}

// A band's centre is the stored voltage. Join adjacent frames at their mid-voltage.
// The two reflective ridges print more brightly when a gate is open.
constexpr Ink negative[] = {0xff17152b, 0xff211b36, 0xff2d2548, 0xff3a315d, 0xff4d4275, 0xff625495,
                            0xff7569ab, 0xff8c7dc2, 0xffa595de, 0xffb8bafa, 0xffcadcff, 0xfff2f0ff};
constexpr Ink positive[] = {0xff191628, 0xff28203b, 0xff402850, 0xff62315e, 0xff834370, 0xffa85c86,
                            0xffc27d94, 0xffb49d7b, 0xffb8c968, 0xffd1ec61, 0xffe4ff7e, 0xfff4ffd4};

void exposure (Raster& r, int x, int y, float prev, float cv, float next, bool on, const Ink* inks)
{
    auto level = [] (float voltage) { return 7 - int (std::lround (voltage * 4)); };
    const int centre = level (cv);
    const int entry = level ((prev + cv) * .5f), exit = level ((next + cv) * .5f);
    constexpr int ridge[] = {1, 2, 4, 7, 11, 6, 3, 8, 2, 1};
    for (int dx = 0; dx < 16; ++dx)
    {
        const int point = dx < 8 ? ((8 - dx) * entry + dx * centre + 4) / 8 : ((15 - dx) * centre + (dx - 7) * exit + 4) / 8;
        for (int dy = 0; dy < 14; ++dy)
        {
            const int d = dy - point;
            int ink = d >= -4 && d <= 5 ? ridge[d + 4] : 0;
            if (!on) ink /= 2;
            if (!ink && (dx & 3) == 2 && (dy & 3) == 2) ink = 1;
            r.dot (x + dx, y + dy, inks[ink]);
        }
    }
}

// Ticket corners and the clipped tabs share a three-pixel diagonal at panel scale.
void clipped (Raster& r, int x, int y, int w, int h, Ink fill)
{
    r.box (x + 3, y, w - 3, 1, fill);
    r.box (x + 2, y + 1, w - 2, 1, fill);
    r.box (x + 1, y + 2, w - 1, 1, fill);
    r.box (x, y + 3, w, h - 6, fill);
    r.box (x, y + h - 3, w - 1, 1, fill);
    r.box (x, y + h - 2, w - 2, 1, fill);
    r.box (x, y + h - 1, w - 3, 1, fill);
}

void punch (Raster& r, int x, int y, bool on, bool enabled)
{
    const Ink outline = enabled ? pencil : 0xffbdb8ce;
    if (on)
    {
        r.box (x, y + 1, 8, 9, enabled ? carbon : pencil);
        r.box (x + 1, y, 6, 1, enabled ? carbon : pencil);
        r.box (x + 1, y + 10, 6, 1, light);
    }
    else
        r.frame (x, y, 8, 11, outline);
}

// Each frame replaces a few runs of pixels in the original eye tile. Frames can be
// visited in any order, including after a loop, a seek, or reopening the page.
void paintOrbit (Ink* dest, int stride, int frame)
{
    for (int y = 0; y < orbit::height; ++y)
    {
        const auto* base = masthead::pixels + (orbit::y + y) * masthead::width + orbit::x;
        for (int x = 0; x < orbit::width; ++x)
            dest[y * stride + x] = masthead::palette[base[x]];
    }
    const auto* run = orbit::spans + orbit::offsets[frame];
    const auto* end = orbit::spans + orbit::offsets[frame+1];
    while (run < end)
    {
        int pixel = run[0] | (int (run[1]) << 8);
        const int length = run[2];
        run += 3;
        for (int n = 0; n < length; ++n, ++pixel)
            dest[(pixel / orbit::width) * stride + pixel % orbit::width] = masthead::palette[*run++];
    }
}
} // namespace

void ScreenFeed::setTransport (double beats, double bpm)
{
    const double turns = beats / 4.0;
    const auto phase = uint32_t ((turns - std::floor (turns)) * ticksPerTurn);
    const auto milliBpm = uint64_t (std::lround (bpm * 1000.0));
    orbitClock.store (milliBpm * ticksPerTurn + phase, std::memory_order_relaxed);
}

int Screen::orbitFrame (const melodee_screen_canvas_t& canvas)
{
    if (!canvas.playing)
    {
        spinning = false;
        return 0;
    }
    const auto clock = feed.orbitClock.load (std::memory_order_relaxed);
    if (!spinning || clock != lastOrbitClock || canvas.now_ms < orbitAnchorMs)
    {
        lastOrbitClock = clock;
        orbitAnchorMs = canvas.now_ms;
    }
    spinning = true;
    const double phase = double (clock % ScreenFeed::ticksPerTurn) / ScreenFeed::ticksPerTurn;
    const double bpm = double (clock / ScreenFeed::ticksPerTurn) / 1000.0;
    // The screen clock carries the rotation between audio blocks and through the host's idle gate.
    const double turns = phase + double (canvas.now_ms - orbitAnchorMs) * bpm / 240000.0;
    return int ((turns - std::floor (turns)) * orbit::frames);
}

void Screen::setMemory (const std::array<float, seqStateCount>& savedValues, bool full)
{
    memoryChanged |= haveMemory != full || memory != savedValues;
    memory = savedValues;
    haveMemory = full;
}

bool Screen::cellChanged (int cell) const
{
    const uint32_t bits = bitsId (cell / 8), cv = cvId (cell);
    return haveMemory && (bit (int (live[bits]) ^ int (saved (bits)), cell % 8) || live[cv] != saved (cv));
}

void Screen::selectGroup (int n)
{
    group = n;
    rows = 0;
    if (group)
        for (const auto& p : paramTable)
            if (std::strcmp (p.module, groups[group]) == 0) fields[rows++] = p.id;
}

void Screen::knobs (clap_id ids[3]) const
{
    if (!group)
    {
        ids[0] = inspector ? cvId (selection) : ParamId::corrupt1;
        ids[1] = inspector ? (cellPage () ? ParamId::length2 : ParamId::length1) : ParamId::corrupt2;
        ids[2] = inspector ? (cellPage () ? ParamId::corrupt2 : ParamId::corrupt1) : ParamId::clockDiv;
        return;
    }
    for (int k = 0; k < 3; ++k)
    {
        const int n = row () + k;
        ids[k] = n < rows && parameter (fields[n]).kind != Kind::button ? fields[n] : CLAP_INVALID_ID;
    }
}

bool Screen::input (const melodee_screen_input_t& e, Edit& out)
{
    if (e.kind == MELODEE_SCREEN_NUDGE && !editing)
    {
        if (inspector && e.value < 0)
            inspector = false;
        else
        {
            const int next = std::clamp (group + (e.value > 0) - (e.value < 0), 0, int (std::size (groups)) - 1);
            if (next == group) return false;
            inspector = false;
            selectGroup (next);
        }
    }
    else if (!group)
    {
        if (e.kind == MELODEE_SCREEN_WHEEL)
            selection = wrap (selection, e.value, inspector ? cellCount : choiceCount);
        else if (e.kind == MELODEE_SCREEN_PUSH)
        {
            if (inspector)
            {
                const uint32_t id = bitsId (cellPage ());
                const int mask = int (feed.value (values, paramIndex (id)));
                out = {id, float (mask ^ (1 << (selection % 8))), false};
            }
            else if (selection < cellCount)
                inspector = true;
            else
                out = {actions[selection - cellCount], 0, true};
        }
        else
            return false;
    }
    else
    {
        const Param& p = parameter (fields[row ()]);
        if (e.kind == MELODEE_SCREEN_PUSH)
        {
            if (p.kind == Kind::button)
                out = {p.id, 0, true};
            else
                editing = !editing;
        }
        else if (!editing && e.kind == MELODEE_SCREEN_WHEEL)
            cursors[group] = int (std::clamp<int64_t> (int64_t (row ()) + e.value, 0, rows - 1));
        else if (editing && (e.kind == MELODEE_SCREEN_WHEEL || e.kind == MELODEE_SCREEN_NUDGE))
        {
            const float old = feed.value (values, paramIndex (p.id));
            const float step = p.kind == Kind::continuous ? (p.max - p.min) / (e.kind == MELODEE_SCREEN_NUDGE ? 16
                                                                               : e.shift                      ? 1024
                                                                                                              : 128)
                                                          : 1;
            const float next = clampParam (p, old + e.value * step);
            if (next != old) out = {p.id, next, false};
        }
        else
            return false;
    }
    changed = true;
    return true;
}

void Screen::header (Raster& r) const
{
    for (int y = 0; y < masthead::height; ++y)
        for (int x = 0; x < masthead::width; ++x)
            r.px[y * r.w + x] = masthead::palette[masthead::pixels[y * masthead::width + x]];

    if (ringFrame) paintOrbit (r.px + orbit::y*r.w + orbit::x, r.w, ringFrame);

    constexpr int tab = 218;
    r.text (tab + 3, 6, inspector ? "CELL EDITOR" : group ? groups[group] : "CONTACT SHEET", light);
    char focus[24];
    if (selection < cellCount && !group)
    {
        char name[16];
        cellLabel (name, selection);
        std::snprintf (focus, sizeof (focus), inspector ? "%s" : "PUSH %s", name);
    }
    else if (!group)
        std::snprintf (focus, sizeof (focus), "PUSH %s", actionLabels[selection - cellCount]);
    else
        std::snprintf (focus, sizeof (focus), "%s",
                       live[ParamId::chain] > .5f ? "CHAIN" : clockDivChoices[int (live[ParamId::clockDiv])]);
    clipped (r, tab, 19, 97, 15, inspector ? ice : lime);
    r.text (tab + 5, 23, focus, carbon);
    if (inspector)
        r.right (310, 23, live[ParamId::chain] > .5f ? "CHAIN" : clockDivChoices[int (live[ParamId::clockDiv])], carbon);
    r.box (5, 39, 310, 1, rule);
    r.box (5, 39, 38, 1, ice);
}

void Screen::films (Raster& r) const
{
    int diff = 0;
    for (int c = 0; c < cellCount; ++c)
        diff += cellChanged (c);
    for (int side = 0; side < 2; ++side)
    {
        const int left = 5 + side * 156;
        const Ink ink = side ? lime : ice;
        clipped (r, left, 0, side ? 38 : 45, 13, ink);
        r.text (left + 5, 3, side ? "LIVE" : "SAVED", carbon);
        char status[24];
        if (side && haveMemory)
            std::snprintf (status, sizeof (status), "%d CHANGED", diff);
        else if (side)
            std::snprintf (status, sizeof (status), "%d + %d", int (live[ParamId::length1]), int (live[ParamId::length2]));
        else
            std::snprintf (status, sizeof (status), "%s", haveMemory ? "NEGATIVE" : "EMPTY");
        r.right (left + 150, 3, status, side && diff ? pink : muted);
        r.box (left, 14, 153, 44, film);
        for (int x = left + 4; x < left + 148; x += 13)
        {
            r.box (x, 16, 5, 2, rule);
            r.box (x, 55, 5, 2, rule);
        }
        if (!side && !haveMemory)
        {
            r.text (left + 12, 25, "No saved pattern", light);
            r.text (left + 12, 39, "Select SAVE below", muted);
            continue;
        }
        for (int cell = 0; cell < cellCount; ++cell)
        {
            const int lane = cell / 8, k = cell % 8, x = left + 8 + 17 * k, y = 21 + 16 * lane;
            const auto bits = bitsId (lane);
            auto at = [&] (int column)
            {
                const auto id = cvId (lane * 8 + (column + 8) % 8);
                return side ? live[id] : saved (id);
            };
            exposure (r, x, y, at (k - 1), at (k), at (k + 1), bit (int (side ? live[bits] : saved (bits)), k),
                      side ? positive : negative);
            if (side && cellChanged (cell)) r.box (x, y + 13, 16, 2, pink);
        }
    }
}

void Screen::card (Raster& r) const
{
    clipped (r, 5, 1, 310, 63, paper);
    r.box (10, 4, 296, 12, carbon);
    r.text (16, 6, "BIT", lime);
    for (int y : {21, 35, 48, 57})
    {
        r.box (8, y, 3, 4, ground);
        r.box (309, y, 3, 4, ground);
    }
    for (int k = 0; k < 8; ++k)
    {
        const int x = 48 + k * 32;
        const char label[] = {char ('1' + k), 0};
        r.text (x + 9, 6, label, light);
        r.box (x - 2, 17, 1, 44, pencil);
        for (int lane = 0; lane < 2; ++lane)
        {
            const int cell = lane * 8 + k, y = 19 + 21 * lane;
            const bool enabled = k < int (live[lane ? ParamId::length2 : ParamId::length1]);
            punch (r, x + 4, y + 3, bit (int (live[bitsId (lane)]), k), enabled);
            // A bipolar voltage ruler next to the punched hole.
            r.box (x + 15, y + 9, 10, 1, pencil);
            const int bar = int (std::lround (live[cvId (cell)] * 6));
            r.box (x + 18, y + 9 - std::max (0, bar), 4, std::abs (bar) + 1, enabled ? blueInk : pencil);
            if (selection == cell)
            {
                r.frame (x, y, 28, 19, berry);
                r.box (x, y, 7, 2, berry);
                r.box (x + 21, y + 17, 7, 2, berry);
            }
            if (cellChanged (cell)) r.box (x + 23, y + 2, 3, 3, berry);
            for (int s = 0; s < 2; ++s)
            {
                if (head (s, false) == cell) r.box (x + 2 + s * 12, y + 16, 10, 2, hitInk[flash[s]]);
                if (head (s, true) == cell) diamond (r, x + 6 + s * 12, y + 1, blueInk);
            }
        }
    }
    clipped (r, 15, 21, 23, 14, carbon);
    clipped (r, 15, 42, 23, 14, carbon);
    r.text (22, 24, "A", lime);
    r.text (22, 45, "B", ice);
    for (int i = 0; i < int (std::size (actions)); ++i)
    {
        const int x = 5 + 78 * i;
        const bool selected = selection == cellCount + i;
        const Ink ink = selected ? carbon : (i < 2 ? ice : lime);
        clipped (r, x, 68, 75, 16, selected ? lime : panel);
        // Rotation, print, and return marks echo the action under the cursor.
        r.text (x + 4, 72, i < 2 ? ">" : i == 2 ? "+" : "<", ink);
        r.text (x + 16, 72, actionLabels[i], selected ? carbon : (i == 3 && !haveMemory ? muted : light));
    }
}

void Screen::cellValues (Raster& r) const
{
    const uint32_t bits = bitsId (cellPage ()), cv = cvId (selection);
    for (int side = 0; side < 2; ++side)
    {
        const int x = 5 + 156 * side;
        const Ink fill = side ? lime : panel;
        const Ink fg = side ? carbon : light, sub = side ? carbon : ice;
        clipped (r, x, 1, 153, 55, fill);
        r.text (x + 7, 7, side ? "LIVE" : "SAVED", sub);
        if (!side && !haveMemory)
        {
            r.text (x + 7, 31, "No saved print", muted);
            continue;
        }
        const bool on = bit (int (side ? live[bits] : saved (bits)), selection % 8);
        r.right (x + 146, 7, on ? "GATE ON" : "GATE OFF", sub);
        char text[32];
        voltage (text, side ? live[cv] : saved (cv));
        large (r, x + 7, 25, text, fg);
        r.box (x + 7, 48, 139, 1, side ? blueInk : violet);
        // The little eight-gate print beside the voltage identifies this step even with the gate closed.
        const int mask = int (side ? live[bits] : saved (bits));
        for (int j = 0; j < 8; ++j)
        {
            const int tx = x + 118 + (j % 4) * 7, ty = 25 + (j / 4) * 8;
            if (bit (mask, j))
                r.box (tx, ty, 5, 5, fg);
            else
                r.frame (tx, ty, 5, 5, side ? blueInk : violet);
            if (j == selection % 8) r.box (tx, ty + 6, 5, 1, fg);
        }
    }
}

void Screen::cellDetail (Raster& r) const
{
    const int lane = cellPage ();
    const uint32_t cv = cvId (selection);
    const int k = selection % 8;
    char text[48];
    r.text (9, 3, "STEP VOLTAGE", muted);
    if (haveMemory)
        std::snprintf (text, sizeof (text), "CHANGE  %+.2f V", 5 * (live[cv] - saved (cv)));
    else
        std::snprintf (text, sizeof (text), "NO BASELINE");
    r.right (311, 3, text, haveMemory && live[cv] != saved (cv) ? pink : lime);
    constexpr int left = 18, width = 284;
    for (int j = 0; j <= 20; ++j)
        r.box (left + j * width / 20, j % 2 ? 23 : 20, 1, j % 2 ? 4 : 10, rule);
    r.box (left, 25, width + 1, 1, muted);
    auto at = [&] (float v) { return left + int (std::lround ((v + 1) * .5f * width)); };
    const int x = at (live[cv]);
    if (haveMemory)
    {
        const int original = at (saved (cv));
        r.box (std::min (x, original), 24, std::abs (original - x) + 1, 3, violet);
        r.frame (original - 3, 16, 7, 7, ice);
    }
    for (int dy = 0; dy < 5; ++dy)
        r.box (x - dy, 27 + dy, 2 * dy + 1, 1, lime);
    r.text (8, 36, "-5 V", muted);
    r.text (150, 36, "0 V", muted);
    r.right (312, 36, "+5 V", muted);
    r.box (5, 49, 310, 1, rule);
    const int length = int (live[lane ? ParamId::length2 : ParamId::length1]);
    std::snprintf (text, sizeof (text), "LANE %c", 'A' + lane);
    r.text (9, 56, text, light);
    for (int j = 0; j < 8; ++j)
    {
        const int pos = 67 + 19 * j;
        if (bit (int (live[bitsId (lane)]), j))
            r.box (pos, 56, 14, 8, j < length ? lime : rule);
        else
            r.frame (pos, 56, 14, 8, j < length ? muted : rule);
        if (j == k) r.box (pos, 67, 14, 2, lime);
    }
    std::snprintf (text, sizeof (text), "%d/8 STEPS", length);
    r.right (311, 56, text, muted);
    r.text (9, 74, "PUSH GATE", light);
    r.text (111, 74, "WHEEL CELL", muted);
    r.right (311, 74, "- BACK", light);
}

void Screen::list (Raster& r) const
{
    const int start = std::clamp (row () - 6, 0, std::max (0, rows - 8));
    r.text (9, 4, editing ? "TURN TO EDIT" : "PARAMETERS", lime);
    r.right (311, 4, editing ? "PUSH TO FINISH" : "- / +  PAGE", muted);
    for (int i = start; i < std::min (rows, start + 8); ++i)
    {
        const auto& p = parameter (fields[i]);
        const bool active = i == row ();
        const int y = 19 + (i - start) * 16;
        if (active) clipped (r, 5, y - 2, 310, 16, paper);
        r.text (10, y + 1, active ? (editing ? "*" : ">") : " ", berry);
        r.text (24, y + 1, p.name, active ? carbon : light);
        char text[48];
        shortValue (p.id, live[p.id], text);
        r.right (310, y + 1, p.kind == Kind::button ? "PUSH" : text, active ? berry : muted);
    }
}

void Screen::footer (Raster& r) const
{
    r.box (0, 0, 320, footH, film);
    if (shift)
    {
        r.text (6, 3,
                group       ? "Wheel selects   Push edits or triggers"
                : inspector ? "Wheel selects cell   Push toggles gate"
                            : "Wheel selects   Push opens cell or acts",
                light);
        r.text (6, 17,
                group       ? "While editing: Shift + wheel fine steps"
                : inspector ? "K1 voltage   K2 length   K3 corruption"
                            : "+ parameters   K1/K2 corruption   K3 clock",
                muted);
        return;
    }
    clap_id bindings[3];
    knobs (bindings);
    for (int k = 0; k < 3; ++k)
    {
        const int x = 5 + 105 * k;
        if (k) r.box (x - 3, 3, 1, 25, panel);
        char label[20];
        std::snprintf (label, sizeof (label), "K%d", k + 1);
        clipped (r, x, 1, 18, 12, k == 1 ? ice : lime);
        r.text (x + 2, 3, label, carbon);
        if (bindings[k] == CLAP_INVALID_ID) continue;
        const Param& p = parameter (bindings[k]);
        const char* const home[] = {"CORRUPT A", "CORRUPT B", "CLOCK DIV"};
        const char* const cell[] = {"STEP CV", "LENGTH", "CORRUPT"};
        std::snprintf (label, sizeof (label), "%.11s", group ? p.name : inspector ? cell[k] : home[k]);
        r.text (x + 21, 3, label, light);
        char text[48];
        shortValue (p.id, live[p.id], text);
        text[14] = 0;
        r.text (x, 16, text, light);
        constexpr int width = 95;
        const float norm = (live[p.id] - p.min) / (p.max - p.min);
        r.box (x, 26, width, 1, rule);
        r.box (x, 26, int (std::lround (norm * width)), 1, k == 1 ? ice : lime);
        if (pots[k].position >= 0 && !pots[k].engaged)
            r.frame (x + pots[k].position * (width - 1) / 127 - 1, 24, 3, 4, light);
    }
}

bool Screen::paint (const melodee_screen_canvas_t& c)
{
    const bool whole = first || c.whole;
    bool params = false, knobsMoved = false;
    for (uint32_t i = 0; i < paramCount; ++i)
    {
        float& old = live[paramTable[i].id];
        const float value = feed.value (values, i);
        params |= old != value;
        old = value;
    }
    for (int k = 0; k < 3; ++k)
    {
        knobsMoved |= pots[k].engaged != c.knobs[k].engaged || pots[k].position != c.knobs[k].position;
        pots[k] = c.knobs[k];
    }
    const auto next = feed.heads.load (std::memory_order_acquire);
    bool moved = next != heads;
    heads = next;
    for (int s = 0; s < 2; ++s)
    {
        const auto serial = feed.hits[s].load (std::memory_order_acquire);
        if (serial != hits[s])
        {
            hits[s] = serial;
            hitAt[s] = c.now_ms;
        }
        const auto age = c.now_ms - hitAt[s];
        const int level = serial && age < 160 ? 4 - int (age / 40) : 0;
        moved |= flash[s] != level;
        flash[s] = level;
    }
    const bool content = whole || changed || params;
    const bool helpChanged = shift != c.shift;
    shift = c.shift;
    const int nextFrame = orbitFrame (c);
    const bool turned = nextFrame != ringFrame;
    ringFrame = nextFrame;
    bool drew = false;
    if (content) drew |= picture.region<320, headerH> (c.pixels, c.stride, whole, 0, 0, [&] (Raster& r) { header (r); });
    else if (turned)
        drew |= picture.region<orbit::width, orbit::height> (c.pixels, c.stride, false, orbit::x, orbit::y,
            [&](Raster& r) { paintOrbit (r.px, r.w, ringFrame); });
    if (!group)
    {
        if (content || memoryChanged)
            drew |= picture.region<320, topH> (c.pixels, c.stride, whole, 0, headerH,
                                               [&] (Raster& r)
                                               {
                                                   if (inspector)
                                                       cellValues (r);
                                                   else
                                                       films (r);
                                               });
        if (content || memoryChanged || (!inspector && moved))
            drew |= picture.region<320, bodyH> (c.pixels, c.stride, whole, 0, headerH + topH,
                                                [&] (Raster& r)
                                                {
                                                    if (inspector)
                                                        cellDetail (r);
                                                    else
                                                        card (r);
                                                });
    }
    else if (content)
        drew |= picture.region<320, topH + bodyH> (c.pixels, c.stride, whole, 0, headerH, [&] (Raster& r) { list (r); });
    if (content || knobsMoved || helpChanged)
        drew |= picture.region<320, footH> (c.pixels, c.stride, whole, 0, headerH + topH + bodyH,
                                            [&] (Raster& r) { footer (r); });
    first = changed = memoryChanged = false;
    return drew;
}
} // namespace psychic
