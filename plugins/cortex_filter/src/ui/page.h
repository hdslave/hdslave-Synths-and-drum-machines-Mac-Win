// hdslave's plug-in pages on Melodee's screen (melodee_screen.h): a few drawing calls, a page of parameters in
// groups, K1-K3 cards and the controls. A plug-in derives its page from Page: its groups, how a parameter reads, and
// the pictures only it can draw. Pure drawing and logic: no threads, no heap, nothing but the standard library; the
// plug-in calls it from the main thread. Anvil's and TB-MPC-3-P-0's page, with one addition for Cortex Filter: a picture
// group may take the whole body (tallCustom), K1-K3 then shown as a slim strip under it.
//
// The controls, as on hdslave Machine's page: the wheel moves the cursor a row; Push edits the row (the wheel then
// changes it: a choice a step, a knob 1/128 of its range, 1/1024 with Shift; Push again leaves) or presses it if it
// is a button; Minus / Plus go to the group before / after, or while editing move the value by a sixteenth (a choice
// a step). K1-K3 turn the cursor's row and the two under it. Holding Shift shows these. A picture group takes the
// wheel and Push itself, and Minus / Plus too while it says it is editing (customEditing).
#pragma once

#include "ui/font.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace hdui
{
constexpr int width = 320, height = 218; // the canvas: Melodee's screen under its soft keys
constexpr uint32_t noParam = 0xffffffffu;

using Ink = uint32_t;

// a over b, weight 0..256 of b.
inline Ink mix (Ink a, Ink b, int weight)
{
    Ink c = 0xff000000u;
    for (int shift : { 0, 8, 16 })
    {
        const uint32_t av = (a >> shift) & 255u, bv = (b >> shift) & 255u;
        c |= (((av * (uint32_t) (256 - weight) + bv * (uint32_t) weight) >> 8) & 255u) << shift;
    }
    return c;
}

struct Palette
{
    Ink bg, panel, card, rule, text, muted, dim, accent, onAccent;
};

// Drawing into a width x height picture, clipped to it.
struct Raster
{
    uint32_t* px;
    int stride;

    void box (int x, int y, int w, int h, Ink c)
    {
        const int x0 = std::max (x, 0), x1 = std::min (x + w, width), y0 = std::max (y, 0), y1 = std::min (y + h, height);
        for (int j = y0; j < y1; ++j)
            std::fill (px + j * stride + x0, px + j * stride + std::max (x0, x1), c);
    }
    void dot (int x, int y, Ink c)
    {
        if (x >= 0 && x < width && y >= 0 && y < height)
            px[y * stride + x] = c;
    }
    void frame (int x, int y, int w, int h, Ink c)
    {
        box (x, y, w, 1, c), box (x, y + h - 1, w, 1, c), box (x, y, 1, h, c), box (x + w - 1, y, 1, h, c);
    }
    void line (int x0, int y0, int x1, int y1, Ink c, int dash = 0)
    {
        int n = 0;
        const int dx = std::abs (x1 - x0), dy = -std::abs (y1 - y0), sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
        int e = dx + dy;
        for (;;)
        {
            if (dash == 0 || (n++ / dash) % 2 == 0)
                dot (x0, y0, c);
            if (x0 == x1 && y0 == y1)
                break;
            const int e2 = 2 * e;
            if (e2 >= dy)
                e += dy, x0 += sx;
            if (e2 <= dx)
                e += dx, y0 += sy;
        }
    }
    // A filled round spot of radius r (pixels) around (cx, cy), blended over what is there by `weight` (0..256).
    void disc (int cx, int cy, int radius, Ink c, int weight = 256)
    {
        for (int y = -radius; y <= radius; ++y)
            for (int x = -radius; x <= radius; ++x)
                if (x * x + y * y <= radius * radius + radius)
                {
                    const int px0 = cx + x, py0 = cy + y;
                    if (px0 >= 0 && px0 < width && py0 >= 0 && py0 < height)
                    {
                        uint32_t& at = px[py0 * stride + px0];
                        at = weight >= 256 ? c : blendInk (at, c, weight);
                    }
                }
    }
    static Ink blendInk (Ink a, Ink b, int weight)
    {
        Ink out = 0xff000000u;
        for (int shift : { 0, 8, 16 })
        {
            const uint32_t av = (a >> shift) & 255u, bv = (b >> shift) & 255u;
            out |= (((av * (uint32_t) (256 - weight) + bv * (uint32_t) weight) >> 8) & 255u) << shift;
        }
        return out;
    }

    // Text in the 7 x 12 font (scale 2: 14 x 24), at most `limit` characters; returns where it ends.
    int text (int x, int y, const char* s, Ink c, int limit = 64, int scale = 1)
    {
        for (int i = 0; i < limit && s[i] != 0; ++i)
        {
            const unsigned ch = (unsigned char) s[i];
            const int g = (ch >= 32 && ch <= 127 ? (int) ch : '?') - 32;
            for (int row = 0; row < font::glyphHeight; ++row)
                for (uint32_t bits = font::glyphs[g][row], k = 0; bits != 0; bits >>= 1, ++k)
                    if (bits & 1u)
                    {
                        if (scale == 1)
                            dot (x + (int) k, y + row, c);
                        else
                            box (x + (int) k * scale, y + row * scale, scale, scale, c);
                    }
            x += font::advance[g] * scale;
        }
        return x;
    }
};

inline int textWidth (const char* s, int limit = 64, int scale = 1)
{
    int n = 0;
    while (n < limit && s[n] != 0)
        ++n;
    return n * font::glyphWidth * scale;
}

// What a page shows of a parameter now.
enum class Kind : uint8_t { continuous, stepped, choice, button };
struct Value
{
    float value = 0.0f, min = 0.0f, max = 1.0f, def = 0.0f;
    Kind kind = Kind::continuous;
    bool bipolar = false;
    char text[24] = {};
    float unit() const { return max > min ? std::clamp ((value - min) / (max - min), 0.0f, 1.0f) : 0.0f; }
};

struct Field
{
    uint32_t id;
    const char* label; // up to 11 characters, as shown
};
struct Group
{
    const char* name;   // the tab's name
    const Field* fields;
    int count;          // 0: a picture of the plug-in's own (Page::drawCustom), with its own controls
};

// What Melodee's canvas says, each paint.
struct Host
{
    uint64_t nowMs = 0;
    Ink accent = 0;     // the track's colour (0: none given)
    int16_t pot[3] = { -1, -1, -1 };
    bool engaged[3] = { true, true, true };
    bool shift = false, playing = false;
};

enum class Key : uint8_t { wheel = 0, push = 1, nudge = 2 }; // as MELODEE_SCREEN_WHEEL, _PUSH, _NUDGE

// What an input did: parameters set (plain values) and buttons pressed.
struct Edits
{
    struct Edit { uint32_t id; float value; bool press; };
    Edit list[4];
    int count = 0;
    void set (uint32_t id, float v) { if (count < 4) list[count++] = { id, v, false }; }
    void press (uint32_t id) { if (count < 4) list[count++] = { id, 0.0f, true }; }
};

class Page
{
public:
    virtual ~Page() = default;

    bool input (Key key, int amount, bool shift, Edits& out)
    {
        const Group& g = group();
        if (g.count == 0 && (key != Key::nudge || customEditing()))
            return customInput (current, key, amount, shift, out);
        if (key == Key::nudge && ! editing)
        {
            const int n = groupCount();
            const int next = std::clamp (current + (amount < 0 ? -1 : 1), 0, n - 1);
            const bool moved = next != current;
            current = next;
            return moved;
        }
        if (g.count == 0)
            return false;
        int& row = cursor[current];
        const Field& field = g.fields[row];
        Value v;
        read (field.id, v);
        if (key == Key::push)
        {
            if (v.kind == Kind::button)
                return pressed (field.id, out), true;
            editing = ! editing;
            return true;
        }
        if (key == Key::wheel && ! editing)
        {
            row = std::clamp (row + amount, 0, g.count - 1);
            return true;
        }
        // Editing: the wheel a fine step, Minus / Plus a coarse one; a choice or a whole number a step either way.
        if (v.kind == Kind::button)
            return false;
        float next;
        if (v.kind != Kind::continuous)
            next = std::clamp (v.value + (float) amount, v.min, v.max);
        else
        {
            const float range = v.max - v.min;
            const float stepSize = key == Key::nudge ? range / 16.0f : range / (shift ? 1024.0f : 128.0f);
            next = std::clamp (v.value + stepSize * (float) amount, v.min, v.max);
        }
        if (next != v.value)
            out.set (field.id, next);
        return true;
    }

    // K1-K3: the cursor's row and the two under it (a button turns nothing), or what a picture page says.
    void knobs (uint32_t ids[3]) const
    {
        ids[0] = ids[1] = ids[2] = noParam;
        const Group& g = group();
        if (g.count == 0)
            return customKnobs (current, ids);
        for (int k = 0; k < 3; ++k)
        {
            const int row = cursor[current] + k;
            if (row >= g.count)
                break;
            Value v;
            read (g.fields[row].id, v);
            if (v.kind != Kind::button)
                ids[k] = g.fields[row].id;
        }
    }

    // Draws the page into `pixels`; false (and nothing touched) when it would look as it did. `whole`: the canvas
    // holds someone else's picture, so it is all drawn.
    bool paint (const Host& host, uint32_t* pixels, int stride, bool whole)
    {
        Raster r { frame, width };
        draw (r, host);
        // The canvas holds the last picture drawn (unless `whole`): unchanged rows compared there, not hashed.
        bool same = ! whole;
        for (int y = 0; same && y < height; ++y)
            same = std::memcmp (pixels + (size_t) y * (size_t) stride, frame + y * width, sizeof (uint32_t) * width) == 0;
        if (same)
            return false;
        for (int y = 0; y < height; ++y)
            std::memcpy (pixels + (size_t) y * (size_t) stride, frame + y * width, sizeof (uint32_t) * width);
        return true;
    }

    // The body needs another full paint for its own animation.
    virtual bool animating (const Host&) const { return false; }

    int groupIndex() const { return current; }
    bool isEditing() const { return editing; }

protected:
    // The plug-in's side.
    virtual int groupCount() const = 0;
    virtual const Group& groupAt (int index) const = 0;
    virtual void read (uint32_t id, Value& out) const = 0;
    virtual void drawTitle (Raster&, const Host&) const = 0;
    virtual void drawStatus (Raster&, const Host&, int /* right */) {}                          // the header's right
    virtual void drawPanel (Raster&, const Host&, int /* x */, int /* y */, int /* w */, int /* h */) {}  // beside a list
    virtual void drawCustom (Raster&, const Host&, int /* x */, int /* y */, int /* w */, int /* h */) {} // a picture group
    virtual bool customInput (int, Key, int, bool, Edits&) { return false; }
    virtual bool customEditing() const { return false; }               // a picture group wants Minus / Plus now
    virtual void pressed (uint32_t id, Edits& out) { out.press (id); } // a button row pushed
    virtual bool tallCustom() const { return false; } // a picture group takes the body and the cards' room
    virtual void customKnobs (int, uint32_t[3]) const {}
    virtual void legend (const char* lines[6]) const
    {
        lines[0] = "WHEEL  ROW", lines[1] = "PUSH   EDIT", lines[2] = "-/+  GROUP", lines[3] = "EDIT: -/+ STEP", lines[4] = "K1-K3",
        lines[5] = "TURN ROWS";
    }

    const Group& group() const { return groupAt (current); }
    Ink accent (const Host& h) const { return h.accent != 0 ? (h.accent | 0xff000000u) : colours.accent; }

    Palette colours {};
    int current = 0;
    int cursor[12] {};
    bool editing = false;

    // Layout.
    static constexpr int headerY = 3, tabsY = 30, bodyY = 45, bodyH = 104, cardsY = 152, cardsH = 64, rowH = 13, rows = 8;
    static constexpr int listW = 186;
    static constexpr int stripY = 198, stripH = 19, tallH = stripY - bodyY - 2; // the tall picture and its K1-K3 strip

    void drawBar (Raster& r, int x, int y, int w, const Value& v, Ink on, Ink off) const
    {
        r.box (x, y, w, 1, off);
        if (v.kind == Kind::choice || v.kind == Kind::stepped)
        {
            const int n = (int) std::lround (v.max - v.min);
            for (int i = 0; i <= n && n <= 32; ++i)
                r.box (x + (w - 1) * i / std::max (n, 1), y - 1, 1, 3, off);
        }
        const int at = (int) std::lround (v.unit() * (float) (w - 1));
        if (v.bipolar)
        {
            const int mid = (w - 1) / 2;
            r.box (x + std::min (mid, at), y, std::abs (at - mid) + 1, 1, on);
            r.box (x + mid, y - 1, 1, 3, off);
        }
        else if (v.kind == Kind::continuous)
            r.box (x, y, at + 1, 1, on);
        r.box (x + at - 1, y - 2, 3, 5, on);
    }

private:
    uint32_t frame[width * height] {};

    void draw (Raster& r, const Host& host)
    {
        const Palette& c = colours;
        const Ink hi = accent (host);
        r.box (0, 0, width, height, c.bg);
        // Header: the name, large; the plug-in's status at the right.
        drawTitle (r, host);
        drawStatus (r, host, width - 6);
        // Tabs.
        int x = 4;
        for (int i = 0; i < groupCount(); ++i)
        {
            const char* name = groupAt (i).name;
            const int w = textWidth (name) + 6;
            if (i == current)
            {
                r.box (x, tabsY - 1, w, 13, hi);
                r.box (x + w - 3, tabsY - 1, 3, 1, c.bg);
                r.box (x + w - 2, tabsY, 2, 1, c.bg);
                r.dot (x + w - 1, tabsY + 1, c.bg);
                r.text (x + 3, tabsY, name, c.onAccent);
            }
            else
                r.text (x + 3, tabsY, name, c.muted);
            x += w + 2;
        }
        r.box (0, tabsY + 12, width, 1, c.rule);
        // Body.
        const Group& g = group();
        const bool tall = g.count == 0 && tallCustom();
        if (g.count == 0)
            drawCustom (r, host, 0, bodyY, width, tall ? tallH : bodyH);
        else
            drawList (r, host, g);
        // K1-K3, or the controls while Shift is held.
        if (host.shift)
            drawLegend (r, host);
        else if (tall)
            drawStrip (r, host);
        else
            drawCards (r, host);
    }

    void drawList (Raster& r, const Host& host, const Group& g)
    {
        const Palette& c = colours;
        const Ink hi = accent (host);
        const int row = cursor[current];
        const int top = std::clamp (row - 3, 0, std::max (0, g.count - rows));
        for (int i = 0; i < rows && top + i < g.count; ++i)
        {
            const Field& f = g.fields[top + i];
            Value v;
            read (f.id, v);
            const int y = bodyY + i * rowH;
            const bool at = top + i == row, k = top + i > row && top + i <= row + 2;
            if (at)
            {
                r.box (0, y, listW, rowH, c.card);
                r.box (0, y, 3, rowH, hi);
            }
            else if (k)
                r.box (0, y, 2, rowH, mix (c.bg, hi, 90));
            r.text (7, y + 1, f.label, at ? c.text : c.muted, 11);
            const bool edit = at && editing;
            const int room = std::clamp ((150 - 14 - textWidth (f.label, 11)) / font::glyphWidth, 4, 12); // characters
            const int vw = textWidth (v.text, room);
            if (edit)
                r.frame (150 - vw - 3, y, vw + 6, rowH, hi);
            r.text (150 - vw, y + 1, v.text, edit ? hi : (at ? c.text : c.muted), room);
            if (v.kind != Kind::button)
                drawBar (r, 157, y + 6, 26, v, at ? hi : c.muted, c.rule);
            else
                r.frame (158, y + 2, 24, 9, at ? hi : c.rule);
        }
        if (g.count > rows) // where the list is
        {
            const int h = bodyH * rows / g.count, y = bodyY + (bodyH - h) * top / std::max (1, g.count - rows);
            r.box (listW + 1, y, 1, h, c.rule);
        }
        drawPanel (r, host, listW + 6, bodyY, width - listW - 8, bodyH);
    }

    // A shallow chamfer catches the light at the end of each control panel.
    void cardFace (Raster& r, int x, int y, int w, int h) const
    {
        r.box (x, y, w, h, colours.panel);
        r.box (x + 17, y, w - 21, 1, colours.rule);
        for (int row = 0; row < 4; ++row)
        {
            r.box (x + w - 4 + row, y + row, 4 - row, 1, colours.bg);
            r.dot (x + w - 5 + row, y + row, colours.dim);
        }
    }

    void drawCards (Raster& r, const Host& host)
    {
        const Palette& c = colours;
        const Ink hi = accent (host);
        uint32_t ids[3];
        knobs (ids);
        const int w = (width - 8 - 8) / 3;
        for (int k = 0; k < 3; ++k)
        {
            const int x = 4 + k * (w + 4), y = cardsY;
            cardFace (r, x, y, w, cardsH - 2);
            char tag[3] = { 'K', (char) ('1' + k), 0 };
            r.box (x, y, 17, 13, ids[k] != noParam ? hi : c.rule);
            r.text (x + 2, y + 1, tag, c.onAccent);
            if (ids[k] == noParam)
            {
                // A button under the cursor (or the row after it): Push presses the cursor's.
                const Group& g = group();
                const int row = cursor[current] + k;
                if (g.count > 0 && row < g.count)
                {
                    r.text (x + 21, y + 1, g.fields[row].label, c.muted, 11);
                    r.text (x + 4, y + 20, k == 0 ? "PUSH" : "-", k == 0 ? hi : c.dim);
                }
                continue;
            }
            Value v;
            read (ids[k], v);
            r.text (x + 21, y + 1, labelOf (ids[k]), c.text, 11);
            r.text (x + 4, y + 20, v.text, hi, 13, 1);
            const int barY = y + 46, barW = w - 12;
            drawBar (r, x + 6, barY, barW, v, hi, c.rule);
            // Where the pot sits while it has yet to meet the value (takeover): a hollow mark to move it to.
            if (host.pot[k] >= 0 && ! host.engaged[k])
            {
                const int px = x + 6 + host.pot[k] * (barW - 1) / 127;
                r.frame (px - 2, barY + 4, 5, 6, c.text);
            }
        }
    }

    // K1-K3 under a tall picture: each its tag, label and value on one line, a bar under them.
    void drawStrip (Raster& r, const Host& host)
    {
        const Palette& c = colours;
        const Ink hi = accent (host);
        uint32_t ids[3];
        knobs (ids);
        const int w = (width - 8 - 8) / 3;
        for (int k = 0; k < 3; ++k)
        {
            const int x = 4 + k * (w + 4), y = stripY;
            cardFace (r, x, y, w, stripH);
            char tag[3] = { 'K', (char) ('1' + k), 0 };
            r.box (x, y, 17, 13, ids[k] != noParam ? hi : c.rule);
            r.text (x + 2, y + 1, tag, c.onAccent);
            if (ids[k] == noParam)
                continue;
            Value v;
            read (ids[k], v);
            const int vw = textWidth (v.text, 7);
            const char* label = customLabel (ids[k]); // the strip is narrow: the page's short name, if it has one
            r.text (x + 20, y + 1, label[0] != 0 ? label : labelOf (ids[k]), c.muted, std::max (0, (w - 26 - vw) / font::glyphWidth));
            r.text (x + w - 3 - vw, y + 1, v.text, hi, 7);
            drawBar (r, x + 3, y + stripH - 3, w - 6, v, hi, c.rule);
            if (host.pot[k] >= 0 && ! host.engaged[k])
            {
                const int px = x + 3 + host.pot[k] * (w - 7) / 127;
                r.box (px - 1, y + stripH - 6, 3, 2, c.text);
            }
        }
    }

    void drawLegend (Raster& r, const Host& host)
    {
        const char* lines[6];
        legend (lines);
        const int w = (width - 8 - 8) / 3;
        for (int k = 0; k < 3; ++k)
        {
            const int x = 4 + k * (w + 4);
            cardFace (r, x, cardsY, w, cardsH - 2);
            r.text (x + 5, cardsY + 10, lines[2 * k], accent (host), 14);
            r.text (x + 5, cardsY + 30, lines[2 * k + 1], colours.text, 14);
        }
    }

    // A parameter's label as the list shows it (from whichever group lists it).
    const char* labelOf (uint32_t id) const
    {
        for (int i = 0; i < groupCount(); ++i)
        {
            const Group& g = groupAt (i);
            for (int f = 0; f < g.count; ++f)
                if (g.fields[f].id == id)
                    return g.fields[f].label;
        }
        return customLabel (id);
    }

protected:
    virtual const char* customLabel (uint32_t) const { return ""; } // a short name for K1-K3's strip, "" for the list's
};
} // namespace hdui
