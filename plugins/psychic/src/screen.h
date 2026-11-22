#pragma once

#include "melodee_screen.h"
#include "params.h"
#include "ui/page.h"

#include <array>
#include <atomic>
#include <limits>

namespace psychic
{
struct ScreenFeed
{
    // A queued page edit is visible to the host immediately. The engine still gets all edits and presses in order.
    static constexpr float noEdit = std::numeric_limits<float>::infinity ();
    std::atomic<float> preview[paramCount];

    float value (const std::atomic<float>* values, int i) const
    {
        const auto v = preview[i].load (std::memory_order_acquire);
        return v == noEdit ? values[i].load (std::memory_order_relaxed) : v;
    }

    // The audio thread publishes tempo and quarter-note position as one coherent observation.
    static constexpr uint32_t ticksPerTurn = 1u << 24;
    std::atomic<uint64_t> orbitClock {120000ull * ticksPerTurn};
    void setTransport (double beats, double bpm);

    // Five bits per physical cell: play A, write A, play B, write B. Cell 16 means no play head yet.
    std::atomic<uint32_t> heads{16u | (16u << 10) | (8u << 15)};
    std::atomic<uint32_t> hits[2]{};
};

class Screen
{
  public:
    struct Edit
    {
        clap_id id = CLAP_INVALID_ID;
        float value = 0;
        bool press = false;
    };

    Screen (const std::atomic<float>* values, const ScreenFeed& feed) : values (values), feed (feed) {}

    void setMemory (const std::array<float, seqStateCount>& saved, bool full);
    bool input (const melodee_screen_input_t& input, Edit& edit);
    void knobs (clap_id ids[3]) const;
    bool paint (const melodee_screen_canvas_t& canvas);

    bool animating () const { return spinning || (group == 0 && !inspector && (flash[0] || flash[1])); }

  private:
    static constexpr const char* groups[] = {"Contact sheet", "Sequencer", "Seq 1",   "Seq 1/CV", "Seq 2",
                                             "Seq 2/CV",      "VCO",       "Mod VCO", "Mixer",    "VCW",
                                             "VCF",           "EG",        "Output",  "Patch"};
    const std::atomic<float>* values;
    const ScreenFeed& feed;
    hdui::Picture picture;
    std::array<float, ParamId::count> live{};
    std::array<float, seqStateCount> memory{};
    uint32_t fields[paramCount]{}, hits[2]{}, heads = 0;
    uint64_t hitAt[2]{};
    int group = 0, rows = 0, cursors[std::size (groups)]{}, selection = 0, flash[2]{};
    bool first = true, changed = true, memoryChanged = true, haveMemory = false;
    uint64_t lastOrbitClock = 0, orbitAnchorMs = 0;
    int ringFrame = 0;
    bool spinning = false;
    bool inspector = false, editing = false, shift = false;
    melodee_screen_knob_t pots[3]{};

    int row () const { return cursors[group]; }

    int cellPage () const { return selection / 8; }

    float saved (uint32_t id) const { return memory[seqStateIndex (id)]; }

    int head (int lane, bool write) const { return int ((heads >> (10 * lane + (write ? 5 : 0))) & 31); }

    int orbitFrame (const melodee_screen_canvas_t& canvas);
    bool cellChanged (int cell) const;
    void selectGroup (int n);
    void header (hdui::Raster& r) const;
    void films (hdui::Raster& r) const;
    void card (hdui::Raster& r) const;
    void cellValues (hdui::Raster& r) const;
    void cellDetail (hdui::Raster& r) const;
    void list (hdui::Raster& r) const;
    void footer (hdui::Raster& r) const;
};
} // namespace psychic
