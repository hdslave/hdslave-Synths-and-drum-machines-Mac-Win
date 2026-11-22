#pragma once

#include <clap/clap.h>

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

#include "plugin_theme.h"

// Include the shared Melodee screen extension definition so the desktop wrapper
// can host every plugin's live 320x218 visual display inside the expanded panel.
#ifndef CLAP_EXT_MELODEE_SCREEN
static const char CLAP_EXT_MELODEE_SCREEN[] = "com.hdslave.melodee.screen/1";
static const char CLAP_EXT_MELODEE_SCREEN_HOST[] = "com.hdslave.melodee.screen-host/1";

enum
{
    MELODEE_SCREEN_WIDTH = 320,
    MELODEE_SCREEN_HEIGHT = 218,
};

typedef struct melodee_screen_knob
{
    int16_t position;
    bool engaged;
} melodee_screen_knob_t;

typedef struct melodee_screen_canvas
{
    uint32_t* pixels;
    uint32_t width;
    uint32_t height;
    uint32_t stride;
    uint64_t now_ms;
    uint32_t accent;
    melodee_screen_knob_t knobs[3];
    bool shift;
    bool playing;
    bool whole;
} melodee_screen_canvas_t;

enum
{
    MELODEE_SCREEN_WHEEL = 0,
    MELODEE_SCREEN_PUSH = 1,
    MELODEE_SCREEN_NUDGE = 2,
};

typedef struct melodee_screen_input
{
    uint32_t kind;
    int32_t value;
    bool shift;
} melodee_screen_input_t;

typedef struct clap_plugin_melodee_screen
{
    bool (CLAP_ABI* paint) (const clap_plugin_t* plugin, const melodee_screen_canvas_t* canvas);
    bool (CLAP_ABI* input) (const clap_plugin_t* plugin, const melodee_screen_input_t* input);
    void (CLAP_ABI* knobs) (const clap_plugin_t* plugin, clap_id ids[3]);
} clap_plugin_melodee_screen_t;

typedef struct clap_host_melodee_screen
{
    void (CLAP_ABI* request_paint) (const clap_host_t* host);
} clap_host_melodee_screen_t;
#endif

namespace hdslave_desktop
{

// Expanded high-resolution logical canvas dimensions
constexpr uint32_t kDefaultGuiWidth = 1360;
constexpr uint32_t kDefaultGuiHeight = 820;

enum class UiEventKind : uint8_t
{
    GestureBegin = 0,
    Value = 1,
    GestureEnd = 2,
};

struct UiParamEvent
{
    UiEventKind kind;
    clap_id paramId;
    double value;
};

// Lock-free SPSC queue from UI (main thread) -> Audio / Flush
template <size_t Capacity>
class SpscQueue
{
public:
    bool push (const UiParamEvent& item)
    {
        const size_t w = writeIdx_.load (std::memory_order_relaxed);
        const size_t next = (w + 1) % Capacity;
        if (next == readIdx_.load (std::memory_order_acquire))
            return false;
        buf_[w] = item;
        writeIdx_.store (next, std::memory_order_release);
        return true;
    }

    bool pop (UiParamEvent& out)
    {
        const size_t r = readIdx_.load (std::memory_order_relaxed);
        if (r == writeIdx_.load (std::memory_order_acquire))
            return false;
        out = buf_[r];
        readIdx_.store ((r + 1) % Capacity, std::memory_order_release);
        return true;
    }

private:
    UiParamEvent buf_[Capacity] {};
    std::atomic<size_t> readIdx_ { 0 };
    std::atomic<size_t> writeIdx_ { 0 };
};

struct CachedParamInfo
{
    clap_id id = CLAP_INVALID_ID;
    uint32_t index = 0;
    uint32_t flags = 0;
    std::string name;
    std::string module;
    double minVal = 0.0;
    double maxVal = 1.0;
    double defVal = 0.0;

    bool isStepped() const { return (flags & CLAP_PARAM_IS_STEPPED) != 0; }
    bool isEnum() const { return (flags & CLAP_PARAM_IS_ENUM) != 0; }
    bool isBool() const { return isStepped() && minVal == 0.0 && maxVal == 1.0 && !isEnum(); }
};

struct PresetEntry
{
    std::string name;
    std::string loadKey;
};

class PanelUI;
struct PlatformGui;

struct DesktopPlugin
{
    clap_plugin_t clap {};
    const clap_host_t* realHost = nullptr;
    clap_host_t proxyHost {};

    const clap_plugin_t* inner = nullptr;
    const clap_plugin_params_t* innerParams = nullptr;
    const clap_plugin_state_t* innerState = nullptr;
    const clap_plugin_preset_load_t* innerPresetLoad = nullptr;
    const clap_plugin_melodee_screen_t* innerScreen = nullptr;

    const clap_host_params_t* hostParams = nullptr;

    PluginTheme theme {};
    std::string pluginName;
    std::string pluginVersion;

    // Parameter metadata cache (built on main thread in init())
    std::vector<CachedParamInfo> params;
    // Fast lookup from clap_id -> index in params (or -1)
    int findParamIndex (clap_id id) const;

    // Factory presets discovered from inner preset discovery factory
    std::vector<PresetEntry> presets;
    int currentPresetIndex = -1;

    // MIDI CC mapping: 128 CCs -> clap_id (CLAP_INVALID_ID = unmapped)
    std::atomic<uint32_t> ccMap[128];
    // MIDI Learn state: CLAP_INVALID_ID = off, 0xFFFFFFFE = armed waiting for control click,
    // otherwise = waiting for incoming MIDI CC to bind to this param_id
    static constexpr uint32_t kLearnArmedNoParam = 0xFFFFFFFEu;
    std::atomic<uint32_t> midiLearnTarget { CLAP_INVALID_ID };
    std::atomic<int> lastMidiCc { -1 };

    // UI -> Audio parameter queue
    SpscQueue<1024> uiToAudio;

    // Live embedded 320x218 MPC screen state
    std::vector<uint32_t> screenPixels;
    bool screenWholeNeeded = true;
    std::atomic<bool> screenDirty { true };
    std::atomic<bool> uiDirty { true };
    std::atomic<bool> transportPlaying { false };

    // GUI state
    PanelUI* ui = nullptr;
    PlatformGui* platformGui = nullptr;
    uint32_t guiWidth = kDefaultGuiWidth;
    uint32_t guiHeight = kDefaultGuiHeight;

    DesktopPlugin();
    ~DesktopPlugin();

    // Helpers called by UI (main thread)
    double getParamValue (clap_id id) const;
    std::string formatParamValue (clap_id id, double value) const;
    void enqueueUiParam (UiEventKind kind, clap_id id, double value);
    bool loadPresetByIndex (int idx);
    int findCcForParam (clap_id id) const;
    void clearCcForParam (clap_id id);
    void clearAllCc();
    int countMappedCc() const;
    void sendScreenInput (uint32_t kind, int32_t value, bool shift);
    void updateEmbeddedScreen (uint64_t nowMs);
};

// Platform GUI hooks implemented in gui_macos.mm (Apple) or gui_win32.cpp (Windows)
PlatformGui* createPlatformGui (DesktopPlugin* plugin);
void destroyPlatformGui (PlatformGui* gui);
bool platformGuiSetParent (PlatformGui* gui, const clap_window_t* window);
bool platformGuiShow (PlatformGui* gui);
bool platformGuiHide (PlatformGui* gui);
bool platformGuiSetSize (PlatformGui* gui, uint32_t width, uint32_t height);
const char* platformGuiApiName();

} // namespace hdslave_desktop
