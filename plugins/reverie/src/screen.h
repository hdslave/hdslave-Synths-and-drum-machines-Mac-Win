// Reverie's page on Melodee's screen (melodee_screen.h): 320 x 218 under the soft keys.
//
//   CHORDS  the sixteen pads as they sit on the MPC (pad 1 bottom left), each with the chord it plays in this key
//           and lit while held; the progression, key, voicing and the rest of the chord section; a keyboard
//           showing the notes that sound (or the last chord's).
//   MOTION  the X / Y field: the path, its centre (X, Y), a light for every note on it; the path's controls and
//           the four destinations.
//   OSC     both oscillators, each drawn at the place its Position (and Motion) has it; width, air and drift.
//   FILTER  the filter's response with the cutoff where the newest note has it, and the filter envelope.
//   AMP     the amplitude envelope and the LFO.
//   FX      ensemble, delay, reverb and shimmer, with an aquarium output meter.
// The wheel moves between controls, Push edits one (and flips a switch at once), Minus and Plus turn the pages;
// K1-K3 hold the control picked and the next two (on MOTION: X, Y and Depth to begin with). Shift shows the
// controls. Nothing is redrawn while nothing changes; while notes sound the page moves at 25 frames a second.
#pragma once

#include "chords.h"
#include "engine.h"
#include "params.h"
#include "ui/page.h"
#include "waves.h"

#include <array>
#include <atomic>
#include <cmath>
#include <iterator>

namespace reverie
{
class Screen final
{
    using Raster = hdui::Raster;
    using Host = hdui::Host;
    using Ink = hdui::Ink;

public:
    Screen (const std::atomic<float>* values, const Telemetry& telemetry) : values (values), tel (telemetry)
    {
        for (int i = 0; i < paramCount; ++i) seen[i] = paramTable[i].def;
    }

    enum Page { chordsPage, motionPage, oscPage, filterPage, ampPage, fxPage, pageCount };

    void knobs (uint32_t ids[3]) const;

    bool animating() const { return animated; }
    int currentPage() const { return page; }
    void showPage (int p) { page = std::clamp (p, 0, pageCount - 1); touched = true; }

    bool input (hdui::Key key, int amount, bool shift, hdui::Edits& out);

    bool paint (const Host& h, uint32_t* pixels, int stride, bool whole);

private:
    static constexpr int headH = 59, bodyH = 124, dockH = hdui::height - headH - bodyH;

    // --- Layout -----------------------------------------------------------------------------------------------------
    struct Cell { uint32_t id; int16_t x, y, w, h; const char* label; };
    struct Layout { const Cell* cells; int count; };

    static Layout layout (int p);

    float live (uint32_t id) const { return values[id].load (std::memory_order_relaxed); }
    int choice (uint32_t id) const { return (int) std::lround (live (id)); }
    void formatted (uint32_t id, char* s, uint32_t n) const;

    bool moving (uint32_t* pixels, int stride, uint32_t sounding);
    void header (const Raster& r) const;
    static void arrow (const Raster& r, int x, int y, int dir, Ink color);
    void plots (const Raster& r, uint32_t sounding) const;
    static void section (const Raster& r, int x, int y, int end, const char* title);
    void body (const Raster& r, uint32_t sounding, uint32_t held);
    void cell (const Raster& r, const Cell& c, bool focused, bool edited) const;
    void padGrid (const Raster& r, uint32_t held) const;
    void keyboard (const Raster& r, int x0, int y0, int w, int h, uint32_t sounding) const;
    void field (const Raster& r, int x0, int y0, int w, int h, uint32_t sounding) const;
    static void well (const Raster& r, int x, int y, int w, int h);
    void scope (const Raster& r, int x0, int y0, int w, int h, int o) const;
    void response (const Raster& r, int x0, int y0, int w, int h) const;
    void envelope (const Raster& r, int x0, int y0, int w, int h, uint32_t first, float now, Ink c, bool sounding) const;
    void lfoPicture (const Raster& r, int x0, int y0, int w, int h) const;
    void outputMeter (const Raster& r, int x, int y) const;
    static void knob (const Raster& r, int x, int y, float unit, Ink trackColor);
    void dock (const Raster& r, const Host& h) const;

    const std::atomic<float>* values;
    const Telemetry& tel;
    float seen[paramCount] {};
    hdui::Picture picture;
    int page = chordsPage;
    int focus[pageCount] {};
    bool editing = false, first = true, touched = false, animated = false, wasAnimated = false;
    int shownPage = -1;
    uint32_t shownSounding = 0, shownHeld = 0, shownStrikes = 0;
    uint64_t frameAt = 0;
    Host lastHost {};
    Ink accent = hdui::ink::foam;
};
} // namespace reverie
