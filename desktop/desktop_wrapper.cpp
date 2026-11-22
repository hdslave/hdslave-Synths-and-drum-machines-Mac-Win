#include "desktop_wrapper.h"
#include "panel_ui.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <new>
#include <vector>

// Provided by compiling the inner MPC plugin's main .cpp with -Dclap_entry=hdslave_inner_clap_entry
extern "C" const clap_plugin_entry_t hdslave_inner_clap_entry;

namespace hdslave_desktop
{

namespace
{

DesktopPlugin& of (const clap_plugin_t* p) { return *static_cast<DesktopPlugin*> (p->plugin_data); }

// Proxy host callbacks ----------------------------------------------------------------------------------------

void CLAP_ABI proxyScreenRequestPaint (const clap_host_t* host)
{
    auto* dp = static_cast<DesktopPlugin*> (host->host_data);
    if (dp)
        dp->screenDirty.store (true, std::memory_order_release);
}

const clap_host_melodee_screen_t kProxyHostScreen { proxyScreenRequestPaint };

const void* CLAP_ABI proxyGetExtension (const clap_host_t* host, const char* extensionId)
{
    auto* dp = static_cast<DesktopPlugin*> (host->host_data);
    if (!dp || !extensionId) return nullptr;
    if (std::strcmp (extensionId, CLAP_EXT_MELODEE_SCREEN_HOST) == 0)
        return &kProxyHostScreen;
    if (dp->realHost && dp->realHost->get_extension)
        return dp->realHost->get_extension (dp->realHost, extensionId);
    return nullptr;
}

void CLAP_ABI proxyRequestRestart (const clap_host_t* host)
{
    auto* dp = static_cast<DesktopPlugin*> (host->host_data);
    if (dp && dp->realHost && dp->realHost->request_restart)
        dp->realHost->request_restart (dp->realHost);
}

void CLAP_ABI proxyRequestProcess (const clap_host_t* host)
{
    auto* dp = static_cast<DesktopPlugin*> (host->host_data);
    if (dp && dp->realHost && dp->realHost->request_process)
        dp->realHost->request_process (dp->realHost);
}

void CLAP_ABI proxyRequestCallback (const clap_host_t* host)
{
    auto* dp = static_cast<DesktopPlugin*> (host->host_data);
    if (dp && dp->realHost && dp->realHost->request_callback)
        dp->realHost->request_callback (dp->realHost);
}

// Preset discovery helper -------------------------------------------------------------------------------------

struct PresetCollector
{
    std::vector<PresetEntry> presets;
    bool inPreset = false;
    PresetEntry current;
};

void CLAP_ABI recvOnError (const clap_preset_discovery_metadata_receiver_t*, int32_t, const char*) {}

bool CLAP_ABI recvBeginPreset (const clap_preset_discovery_metadata_receiver_t* r, const char* name, const char* loadKey)
{
    auto* col = static_cast<PresetCollector*> (r->receiver_data);
    if (col->inPreset)
        col->presets.push_back (col->current);
    col->inPreset = true;
    col->current = { name ? name : "", loadKey ? loadKey : "" };
    return true;
}

void CLAP_ABI recvAddPluginId (const clap_preset_discovery_metadata_receiver_t*, const clap_universal_plugin_id_t*) {}
void CLAP_ABI recvSetSoundpackId (const clap_preset_discovery_metadata_receiver_t*, const char*) {}
void CLAP_ABI recvSetFlags (const clap_preset_discovery_metadata_receiver_t*, uint32_t) {}
void CLAP_ABI recvAddCreator (const clap_preset_discovery_metadata_receiver_t*, const char*) {}
void CLAP_ABI recvSetDescription (const clap_preset_discovery_metadata_receiver_t*, const char*) {}
void CLAP_ABI recvSetTimestamps (const clap_preset_discovery_metadata_receiver_t*, clap_timestamp, clap_timestamp) {}
void CLAP_ABI recvAddFeature (const clap_preset_discovery_metadata_receiver_t*, const char*) {}
void CLAP_ABI recvAddExtraInfo (const clap_preset_discovery_metadata_receiver_t*, const char*, const char*) {}

bool CLAP_ABI dummyDeclareFiletype (const clap_preset_discovery_indexer_t*, const clap_preset_discovery_filetype_t*) { return true; }
bool CLAP_ABI dummyDeclareLocation (const clap_preset_discovery_indexer_t*, const clap_preset_discovery_location_t*) { return true; }
bool CLAP_ABI dummyDeclareSoundpack (const clap_preset_discovery_indexer_t*, const clap_preset_discovery_soundpack_t*) { return true; }
const void* CLAP_ABI dummyIndexerExt (const clap_preset_discovery_indexer_t*, const char*) { return nullptr; }

void discoverFactoryPresets (DesktopPlugin& dp)
{
    dp.presets.clear();
    const auto* pf = static_cast<const clap_preset_discovery_factory_t*> (
        hdslave_inner_clap_entry.get_factory (CLAP_PRESET_DISCOVERY_FACTORY_ID));
    if (!pf)
        pf = static_cast<const clap_preset_discovery_factory_t*> (
            hdslave_inner_clap_entry.get_factory (CLAP_PRESET_DISCOVERY_FACTORY_ID_COMPAT));
    if (!pf || pf->count (pf) == 0)
        return;

    const auto* desc = pf->get_descriptor (pf, 0);
    if (!desc) return;

    clap_preset_discovery_indexer_t indexer {
        CLAP_VERSION, "hdslave-desktop", "hdslave", "", "1.0.0", nullptr,
        dummyDeclareFiletype, dummyDeclareLocation, dummyDeclareSoundpack, dummyIndexerExt
    };

    const auto* prov = pf->create (pf, &indexer, desc->id);
    if (!prov) return;

    if (prov->init (prov))
    {
        PresetCollector col;
        clap_preset_discovery_metadata_receiver_t recv {
            &col, recvOnError, recvBeginPreset, recvAddPluginId, recvSetSoundpackId,
            recvSetFlags, recvAddCreator, recvSetDescription, recvSetTimestamps, recvAddFeature, recvAddExtraInfo
        };
        prov->get_metadata (prov, CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN, nullptr, &recv);
        if (col.inPreset)
            col.presets.push_back (col.current);
        dp.presets = std::move (col.presets);
    }
    prov->destroy (prov);
}

// Allocation-free merged input event list for audio thread / flush --------------------------------------------

struct MergedInputEvents
{
    static constexpr uint32_t kMaxInjected = 512;

    const clap_input_events_t* hostIn = nullptr;
    uint32_t hostCount = 0;

    clap_event_param_value_t injected[kMaxInjected] {};
    uint32_t injectedCount = 0;

    void addInjected (uint32_t time, clap_id paramId, double value)
    {
        if (injectedCount >= kMaxInjected) return;
        auto& ev = injected[injectedCount++];
        ev.header.size = sizeof (clap_event_param_value_t);
        ev.header.time = time;
        ev.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        ev.header.type = CLAP_EVENT_PARAM_VALUE;
        ev.header.flags = 0;
        ev.param_id = paramId;
        ev.cookie = nullptr;
        ev.note_id = -1;
        ev.port_index = -1;
        ev.channel = -1;
        ev.key = -1;
        ev.value = value;
    }

    static uint32_t CLAP_ABI sizeCb (const clap_input_events_t* list)
    {
        const auto* self = static_cast<const MergedInputEvents*> (list->ctx);
        return self->hostCount + self->injectedCount;
    }

    static const clap_event_header_t* CLAP_ABI getCb (const clap_input_events_t* list, uint32_t index)
    {
        const auto* self = static_cast<const MergedInputEvents*> (list->ctx);
        if (index < self->injectedCount)
            return &self->injected[index].header;
        uint32_t hi = index - self->injectedCount;
        if (self->hostIn && hi < self->hostCount)
            return self->hostIn->get (self->hostIn, hi);
        return nullptr;
    }

    clap_input_events_t asClapList()
    {
        return clap_input_events_t { this, sizeCb, getCb };
    }
};

void pushHostParamValue (const clap_output_events_t* out, uint32_t time, clap_id id, double value)
{
    if (!out || !out->try_push) return;
    clap_event_param_value_t ev {};
    ev.header.size = sizeof (ev);
    ev.header.time = time;
    ev.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
    ev.header.type = CLAP_EVENT_PARAM_VALUE;
    ev.header.flags = 0;
    ev.param_id = id;
    ev.cookie = nullptr;
    ev.note_id = -1;
    ev.port_index = -1;
    ev.channel = -1;
    ev.key = -1;
    ev.value = value;
    out->try_push (out, &ev.header);
}

void pushHostParamGesture (const clap_output_events_t* out, uint32_t time, clap_id id, bool begin)
{
    if (!out || !out->try_push) return;
    clap_event_param_gesture_t ev {};
    ev.header.size = sizeof (ev);
    ev.header.time = time;
    ev.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
    ev.header.type = begin ? CLAP_EVENT_PARAM_GESTURE_BEGIN : CLAP_EVENT_PARAM_GESTURE_END;
    ev.header.flags = 0;
    ev.param_id = id;
    out->try_push (out, &ev.header);
}

void drainUiAndMidiEvents (DesktopPlugin& dp, const clap_input_events_t* in, const clap_output_events_t* out,
                           MergedInputEvents& merged)
{
    merged.hostIn = in;
    merged.hostCount = (in && in->size) ? in->size (in) : 0;
    merged.injectedCount = 0;

    // 1. Drain UI parameter events -> inject into inner plugin AND notify DAW host
    UiParamEvent uev {};
    while (dp.uiToAudio.pop (uev))
    {
        if (uev.kind == UiEventKind::GestureBegin)
        {
            pushHostParamGesture (out, 0, uev.paramId, true);
        }
        else if (uev.kind == UiEventKind::GestureEnd)
        {
            pushHostParamGesture (out, 0, uev.paramId, false);
        }
        else if (uev.kind == UiEventKind::Value)
        {
            merged.addInjected (0, uev.paramId, uev.value);
            pushHostParamValue (out, 0, uev.paramId, uev.value);
        }
    }

    // 2. Scan incoming MIDI CC messages for MIDI Learn and CC->Param mapping
    for (uint32_t i = 0; i < merged.hostCount; ++i)
    {
        const auto* h = in->get (in, i);
        if (!h || h->space_id != CLAP_CORE_EVENT_SPACE_ID || h->type != CLAP_EVENT_MIDI)
            continue;
        const auto* m = reinterpret_cast<const clap_event_midi_t*> (h);
        if ((m->data[0] & 0xF0u) != 0xB0u)
            continue;

        const uint8_t cc = m->data[1] & 0x7Fu;
        const uint8_t val = m->data[2] & 0x7Fu;
        dp.lastMidiCc.store (int (cc), std::memory_order_relaxed);

        // MIDI Learn binding
        uint32_t target = dp.midiLearnTarget.load (std::memory_order_acquire);
        if (target != CLAP_INVALID_ID && target != DesktopPlugin::kLearnArmedNoParam)
        {
            // Clear any previous CC mapped to this same param
            for (int c = 0; c < 128; ++c)
            {
                if (dp.ccMap[c].load (std::memory_order_relaxed) == target)
                    dp.ccMap[c].store (CLAP_INVALID_ID, std::memory_order_relaxed);
            }
            dp.ccMap[cc].store (target, std::memory_order_release);
            dp.midiLearnTarget.store (CLAP_INVALID_ID, std::memory_order_release);
            dp.uiDirty.store (true, std::memory_order_release);
        }

        // Apply mapped CC
        const clap_id mappedId = dp.ccMap[cc].load (std::memory_order_acquire);
        if (mappedId != CLAP_INVALID_ID)
        {
            int idx = dp.findParamIndex (mappedId);
            if (idx >= 0)
            {
                const auto& p = dp.params[size_t (idx)];
                double norm = double (val) / 127.0;
                double mappedVal = p.minVal + norm * (p.maxVal - p.minVal);
                if (p.isStepped())
                    mappedVal = std::clamp (std::round (mappedVal), p.minVal, p.maxVal);

                merged.addInjected (h->time, mappedId, mappedVal);
                pushHostParamValue (out, h->time, mappedId, mappedVal);
                dp.uiDirty.store (true, std::memory_order_release);
            }
        }
    }
}

// Memory stream helpers for CLAP_EXT_STATE envelope -----------------------------------------------------------

struct MemOStream
{
    std::vector<uint8_t> data;
    static int64_t CLAP_ABI writeCb (const clap_ostream_t* s, const void* buf, uint64_t size)
    {
        auto* self = static_cast<MemOStream*> (s->ctx);
        const auto* bytes = static_cast<const uint8_t*> (buf);
        self->data.insert (self->data.end(), bytes, bytes + size);
        return int64_t (size);
    }
};

struct MemIStream
{
    const uint8_t* data = nullptr;
    size_t size = 0;
    size_t pos = 0;
    static int64_t CLAP_ABI readCb (const clap_istream_t* s, void* buf, uint64_t count)
    {
        auto* self = static_cast<MemIStream*> (s->ctx);
        size_t avail = (self->pos < self->size) ? (self->size - self->pos) : 0;
        size_t n = std::min (avail, size_t (count));
        if (n > 0)
        {
            std::memcpy (buf, self->data + self->pos, n);
            self->pos += n;
        }
        return int64_t (n);
    }
};

bool writeAll (const clap_ostream_t* out, const void* buf, size_t size)
{
    const auto* p = static_cast<const uint8_t*> (buf);
    size_t done = 0;
    while (done < size)
    {
        int64_t n = out->write (out, p + done, uint64_t (size - done));
        if (n <= 0) return false;
        done += size_t (n);
    }
    return true;
}

// Wrapped CLAP extensions -------------------------------------------------------------------------------------

// 1. Params extension
uint32_t CLAP_ABI wrapParamsCount (const clap_plugin_t* p)
{
    auto& dp = of (p);
    return dp.innerParams ? dp.innerParams->count (dp.inner) : 0;
}

bool CLAP_ABI wrapParamsInfo (const clap_plugin_t* p, uint32_t idx, clap_param_info_t* info)
{
    auto& dp = of (p);
    return dp.innerParams && dp.innerParams->get_info (dp.inner, idx, info);
}

bool CLAP_ABI wrapParamsValue (const clap_plugin_t* p, clap_id id, double* out)
{
    auto& dp = of (p);
    return dp.innerParams && dp.innerParams->get_value (dp.inner, id, out);
}

bool CLAP_ABI wrapParamsValueToText (const clap_plugin_t* p, clap_id id, double v, char* buf, uint32_t size)
{
    auto& dp = of (p);
    return dp.innerParams && dp.innerParams->value_to_text (dp.inner, id, v, buf, size);
}

bool CLAP_ABI wrapParamsTextToValue (const clap_plugin_t* p, clap_id id, const char* text, double* out)
{
    auto& dp = of (p);
    return dp.innerParams && dp.innerParams->text_to_value (dp.inner, id, text, out);
}

void CLAP_ABI wrapParamsFlush (const clap_plugin_t* p, const clap_input_events_t* in, const clap_output_events_t* out)
{
    auto& dp = of (p);
    if (!dp.innerParams) return;
    MergedInputEvents merged;
    drainUiAndMidiEvents (dp, in, out, merged);
    clap_input_events_t inList = merged.asClapList();
    dp.innerParams->flush (dp.inner, &inList, out);
}

const clap_plugin_params_t kWrapParamsExt {
    wrapParamsCount, wrapParamsInfo, wrapParamsValue, wrapParamsValueToText, wrapParamsTextToValue, wrapParamsFlush
};

// 2. State extension (wraps inner state + 128-entry MIDI CC map with "HDSD" magic header)
bool CLAP_ABI wrapStateSave (const clap_plugin_t* p, const clap_ostream_t* stream)
{
    auto& dp = of (p);
    if (!dp.innerState) return false;

    MemOStream innerBuf;
    clap_ostream_t memStream { &innerBuf, MemOStream::writeCb };
    if (!dp.innerState->save (dp.inner, &memStream))
        return false;

    const uint8_t magic[4] = { 'H', 'D', 'S', 'D' };
    const uint32_t version = 1;
    const uint32_t innerSize = uint32_t (innerBuf.data.size());
    uint32_t ccTable[128];
    for (int i = 0; i < 128; ++i)
        ccTable[i] = dp.ccMap[i].load (std::memory_order_relaxed);

    if (!writeAll (stream, magic, 4)) return false;
    if (!writeAll (stream, &version, sizeof (version))) return false;
    if (!writeAll (stream, &innerSize, sizeof (innerSize))) return false;
    if (innerSize > 0 && !writeAll (stream, innerBuf.data.data(), innerSize)) return false;
    if (!writeAll (stream, ccTable, sizeof (ccTable))) return false;
    return true;
}

bool CLAP_ABI wrapStateLoad (const clap_plugin_t* p, const clap_istream_t* stream)
{
    auto& dp = of (p);
    if (!dp.innerState) return false;

    std::vector<uint8_t> raw;
    uint8_t chunk[4096];
    for (;;)
    {
        int64_t n = stream->read (stream, chunk, sizeof (chunk));
        if (n < 0) return false;
        if (n == 0) break;
        raw.insert (raw.end(), chunk, chunk + size_t (n));
    }

    if (raw.size() >= 12 && std::memcmp (raw.data(), "HDSD", 4) == 0)
    {
        uint32_t version = 0, innerSize = 0;
        std::memcpy (&version, raw.data() + 4, 4);
        std::memcpy (&innerSize, raw.data() + 8, 4);
        if (version == 1 && raw.size() >= size_t (12) + innerSize)
        {
            MemIStream innerIn { raw.data() + 12, innerSize, 0 };
            clap_istream_t memIn { &innerIn, MemIStream::readCb };
            if (!dp.innerState->load (dp.inner, &memIn))
                return false;

            const size_t ccOffset = size_t (12) + innerSize;
            if (raw.size() >= ccOffset + 128 * sizeof (uint32_t))
            {
                uint32_t ccTable[128];
                std::memcpy (ccTable, raw.data() + ccOffset, sizeof (ccTable));
                for (int i = 0; i < 128; ++i)
                    dp.ccMap[i].store (ccTable[i], std::memory_order_release);
            }
            dp.uiDirty.store (true, std::memory_order_release);
            dp.screenWholeNeeded = true;
            return true;
        }
    }

    // Raw MPC state fallback
    MemIStream innerIn { raw.data(), raw.size(), 0 };
    clap_istream_t memIn { &innerIn, MemIStream::readCb };
    bool ok = dp.innerState->load (dp.inner, &memIn);
    if (ok)
    {
        dp.uiDirty.store (true, std::memory_order_release);
        dp.screenWholeNeeded = true;
    }
    return ok;
}

const clap_plugin_state_t kWrapStateExt { wrapStateSave, wrapStateLoad };

// 3. Preset load extension
bool CLAP_ABI wrapPresetLoadFromLocation (const clap_plugin_t* p, uint32_t locationKind, const char* location, const char* loadKey)
{
    auto& dp = of (p);
    if (!dp.innerPresetLoad) return false;
    bool ok = dp.innerPresetLoad->from_location (dp.inner, locationKind, location, loadKey);
    if (ok)
    {
        dp.uiDirty.store (true, std::memory_order_release);
        dp.screenWholeNeeded = true;
    }
    return ok;
}

const clap_plugin_preset_load_t kWrapPresetLoadExt { wrapPresetLoadFromLocation };

// 4. Audio Ports / Note Ports / Note Name forwarding
uint32_t CLAP_ABI wrapAudioPortsCount (const clap_plugin_t* p, bool isInput)
{
    auto& dp = of (p);
    const auto* ext = static_cast<const clap_plugin_audio_ports_t*> (dp.inner->get_extension (dp.inner, CLAP_EXT_AUDIO_PORTS));
    return ext ? ext->count (dp.inner, isInput) : 0;
}

bool CLAP_ABI wrapAudioPortsGet (const clap_plugin_t* p, uint32_t idx, bool isInput, clap_audio_port_info_t* info)
{
    auto& dp = of (p);
    const auto* ext = static_cast<const clap_plugin_audio_ports_t*> (dp.inner->get_extension (dp.inner, CLAP_EXT_AUDIO_PORTS));
    return ext && ext->get (dp.inner, idx, isInput, info);
}

const clap_plugin_audio_ports_t kWrapAudioPortsExt { wrapAudioPortsCount, wrapAudioPortsGet };

uint32_t CLAP_ABI wrapNotePortsCount (const clap_plugin_t* p, bool isInput)
{
    auto& dp = of (p);
    const auto* ext = static_cast<const clap_plugin_note_ports_t*> (dp.inner->get_extension (dp.inner, CLAP_EXT_NOTE_PORTS));
    return ext ? ext->count (dp.inner, isInput) : 0;
}

bool CLAP_ABI wrapNotePortsGet (const clap_plugin_t* p, uint32_t idx, bool isInput, clap_note_port_info_t* info)
{
    auto& dp = of (p);
    const auto* ext = static_cast<const clap_plugin_note_ports_t*> (dp.inner->get_extension (dp.inner, CLAP_EXT_NOTE_PORTS));
    return ext && ext->get (dp.inner, idx, isInput, info);
}

const clap_plugin_note_ports_t kWrapNotePortsExt { wrapNotePortsCount, wrapNotePortsGet };

uint32_t CLAP_ABI wrapNoteNameCount (const clap_plugin_t* p)
{
    auto& dp = of (p);
    const auto* ext = static_cast<const clap_plugin_note_name_t*> (dp.inner->get_extension (dp.inner, CLAP_EXT_NOTE_NAME));
    return ext ? ext->count (dp.inner) : 0;
}

bool CLAP_ABI wrapNoteNameGet (const clap_plugin_t* p, uint32_t idx, clap_note_name_t* name)
{
    auto& dp = of (p);
    const auto* ext = static_cast<const clap_plugin_note_name_t*> (dp.inner->get_extension (dp.inner, CLAP_EXT_NOTE_NAME));
    return ext && ext->get (dp.inner, idx, name);
}

const clap_plugin_note_name_t kWrapNoteNameExt { wrapNoteNameCount, wrapNoteNameGet };

// 5. CLAP_EXT_GUI (macOS Cocoa / Windows Win32 expanded high-res control panel)
bool CLAP_ABI guiIsApiSupported (const clap_plugin_t*, const char* api, bool isFloating)
{
    if (isFloating || !api) return false;
    return std::strcmp (api, platformGuiApiName()) == 0;
}

bool CLAP_ABI guiGetPreferredApi (const clap_plugin_t*, const char** api, bool* isFloating)
{
    *api = platformGuiApiName();
    *isFloating = false;
    return true;
}

bool CLAP_ABI guiCreate (const clap_plugin_t* p, const char* api, bool isFloating)
{
    if (!guiIsApiSupported (p, api, isFloating))
        return false;
    auto& dp = of (p);
    if (!dp.ui)
        dp.ui = new (std::nothrow) PanelUI (&dp);
    if (!dp.platformGui)
        dp.platformGui = createPlatformGui (&dp);
    return dp.ui != nullptr && dp.platformGui != nullptr;
}

void CLAP_ABI guiDestroy (const clap_plugin_t* p)
{
    auto& dp = of (p);
    if (dp.platformGui)
    {
        destroyPlatformGui (dp.platformGui);
        dp.platformGui = nullptr;
    }
    delete dp.ui;
    dp.ui = nullptr;
}

bool CLAP_ABI guiSetScale (const clap_plugin_t*, double) { return false; }

bool CLAP_ABI guiGetSize (const clap_plugin_t* p, uint32_t* width, uint32_t* height)
{
    auto& dp = of (p);
    *width = dp.guiWidth;
    *height = dp.guiHeight;
    return true;
}

bool CLAP_ABI guiCanResize (const clap_plugin_t*) { return true; }

bool CLAP_ABI guiGetResizeHints (const clap_plugin_t*, clap_gui_resize_hints_t* hints)
{
    if (!hints) return false;
    hints->can_resize_horizontally = true;
    hints->can_resize_vertically = true;
    hints->preserve_aspect_ratio = false;
    hints->aspect_ratio_width = 16;
    hints->aspect_ratio_height = 10;
    return true;
}

bool CLAP_ABI guiAdjustSize (const clap_plugin_t*, uint32_t* width, uint32_t* height)
{
    if (!width || !height) return false;
    *width = std::clamp (*width, 900u, 2560u);
    *height = std::clamp (*height, 560u, 1600u);
    return true;
}

bool CLAP_ABI guiSetSize (const clap_plugin_t* p, uint32_t width, uint32_t height)
{
    auto& dp = of (p);
    dp.guiWidth = std::clamp (width, 900u, 2560u);
    dp.guiHeight = std::clamp (height, 560u, 1600u);
    if (dp.ui)
        dp.ui->rebuildLayout (int (dp.guiWidth), int (dp.guiHeight));
    if (dp.platformGui)
        return platformGuiSetSize (dp.platformGui, dp.guiWidth, dp.guiHeight);
    return true;
}

bool CLAP_ABI guiSetParent (const clap_plugin_t* p, const clap_window_t* window)
{
    auto& dp = of (p);
    return dp.platformGui && platformGuiSetParent (dp.platformGui, window);
}

bool CLAP_ABI guiSetTransient (const clap_plugin_t*, const clap_window_t*) { return false; }
void CLAP_ABI guiSuggestTitle (const clap_plugin_t*, const char*) {}

bool CLAP_ABI guiShow (const clap_plugin_t* p)
{
    auto& dp = of (p);
    return dp.platformGui && platformGuiShow (dp.platformGui);
}

bool CLAP_ABI guiHide (const clap_plugin_t* p)
{
    auto& dp = of (p);
    return dp.platformGui && platformGuiHide (dp.platformGui);
}

const clap_plugin_gui_t kWrapGuiExt {
    guiIsApiSupported, guiGetPreferredApi, guiCreate, guiDestroy, guiSetScale, guiGetSize,
    guiCanResize, guiGetResizeHints, guiAdjustSize, guiSetSize, guiSetParent, guiSetTransient,
    guiSuggestTitle, guiShow, guiHide
};

// Plugin lifecycle callbacks ----------------------------------------------------------------------------------

bool CLAP_ABI wrapInit (const clap_plugin_t* p)
{
    auto& dp = of (p);
    if (!dp.inner || !dp.inner->init (dp.inner))
        return false;

    dp.innerParams = static_cast<const clap_plugin_params_t*> (dp.inner->get_extension (dp.inner, CLAP_EXT_PARAMS));
    dp.innerState = static_cast<const clap_plugin_state_t*> (dp.inner->get_extension (dp.inner, CLAP_EXT_STATE));
    dp.innerPresetLoad = static_cast<const clap_plugin_preset_load_t*> (dp.inner->get_extension (dp.inner, CLAP_EXT_PRESET_LOAD));
    if (!dp.innerPresetLoad)
        dp.innerPresetLoad = static_cast<const clap_plugin_preset_load_t*> (dp.inner->get_extension (dp.inner, CLAP_EXT_PRESET_LOAD_COMPAT));
    dp.innerScreen = static_cast<const clap_plugin_melodee_screen_t*> (dp.inner->get_extension (dp.inner, CLAP_EXT_MELODEE_SCREEN));

    if (dp.realHost && dp.realHost->get_extension)
        dp.hostParams = static_cast<const clap_host_params_t*> (dp.realHost->get_extension (dp.realHost, CLAP_EXT_PARAMS));

    // Cache all parameter descriptors
    dp.params.clear();
    if (dp.innerParams)
    {
        const uint32_t count = dp.innerParams->count (dp.inner);
        dp.params.reserve (count);
        for (uint32_t i = 0; i < count; ++i)
        {
            clap_param_info_t info {};
            if (dp.innerParams->get_info (dp.inner, i, &info))
            {
                CachedParamInfo c;
                c.id = info.id;
                c.index = i;
                c.flags = info.flags;
                c.name = info.name;
                c.module = info.module;
                c.minVal = info.min_value;
                c.maxVal = info.max_value;
                c.defVal = info.default_value;
                dp.params.push_back (std::move (c));
            }
        }
    }

    discoverFactoryPresets (dp);

    if (dp.innerScreen)
        dp.screenPixels.assign (size_t (MELODEE_SCREEN_WIDTH * MELODEE_SCREEN_HEIGHT), 0xFF0A0C10u);

    return true;
}

void CLAP_ABI wrapDestroy (const clap_plugin_t* p)
{
    auto* dp = static_cast<DesktopPlugin*> (p->plugin_data);
    if (!dp) return;
    guiDestroy (p);
    if (dp->inner)
    {
        dp->inner->destroy (dp->inner);
        dp->inner = nullptr;
    }
    delete dp;
}

bool CLAP_ABI wrapActivate (const clap_plugin_t* p, double sr, uint32_t minF, uint32_t maxF)
{
    auto& dp = of (p);
    return dp.inner && dp.inner->activate (dp.inner, sr, minF, maxF);
}

void CLAP_ABI wrapDeactivate (const clap_plugin_t* p)
{
    auto& dp = of (p);
    if (dp.inner) dp.inner->deactivate (dp.inner);
}

bool CLAP_ABI wrapStartProcessing (const clap_plugin_t* p)
{
    auto& dp = of (p);
    return dp.inner && dp.inner->start_processing (dp.inner);
}

void CLAP_ABI wrapStopProcessing (const clap_plugin_t* p)
{
    auto& dp = of (p);
    if (dp.inner) dp.inner->stop_processing (dp.inner);
}

void CLAP_ABI wrapReset (const clap_plugin_t* p)
{
    auto& dp = of (p);
    if (dp.inner) dp.inner->reset (dp.inner);
}

clap_process_status CLAP_ABI wrapProcess (const clap_plugin_t* p, const clap_process_t* proc)
{
    auto& dp = of (p);
    if (!dp.inner) return CLAP_PROCESS_ERROR;

    if (proc->transport)
        dp.transportPlaying.store ((proc->transport->flags & CLAP_TRANSPORT_IS_PLAYING) != 0, std::memory_order_relaxed);

    MergedInputEvents merged;
    drainUiAndMidiEvents (dp, proc->in_events, proc->out_events, merged);

    clap_process_t wrapped = *proc;
    clap_input_events_t inList = merged.asClapList();
    wrapped.in_events = &inList;
    return dp.inner->process (dp.inner, &wrapped);
}

const void* CLAP_ABI wrapGetExtension (const clap_plugin_t* p, const char* id)
{
    auto& dp = of (p);
    if (!id) return nullptr;
    if (std::strcmp (id, CLAP_EXT_GUI) == 0) return &kWrapGuiExt;
    if (std::strcmp (id, CLAP_EXT_PARAMS) == 0) return &kWrapParamsExt;
    if (std::strcmp (id, CLAP_EXT_STATE) == 0) return &kWrapStateExt;
    if (std::strcmp (id, CLAP_EXT_AUDIO_PORTS) == 0) return &kWrapAudioPortsExt;
    if (std::strcmp (id, CLAP_EXT_NOTE_PORTS) == 0)
    {
        if (dp.inner && dp.inner->get_extension (dp.inner, CLAP_EXT_NOTE_PORTS))
            return &kWrapNotePortsExt;
        return nullptr;
    }
    if (std::strcmp (id, CLAP_EXT_NOTE_NAME) == 0)
    {
        if (dp.inner && dp.inner->get_extension (dp.inner, CLAP_EXT_NOTE_NAME))
            return &kWrapNoteNameExt;
        return nullptr;
    }
    if (std::strcmp (id, CLAP_EXT_PRESET_LOAD) == 0 || std::strcmp (id, CLAP_EXT_PRESET_LOAD_COMPAT) == 0)
    {
        if (dp.innerPresetLoad)
            return &kWrapPresetLoadExt;
        return nullptr;
    }
    return nullptr;
}

void CLAP_ABI wrapOnMainThread (const clap_plugin_t* p)
{
    auto& dp = of (p);
    if (dp.inner && dp.inner->on_main_thread)
        dp.inner->on_main_thread (dp.inner);
}

// Wrapped Factory ---------------------------------------------------------------------------------------------

const clap_plugin_factory_t* innerPluginFactory()
{
    return static_cast<const clap_plugin_factory_t*> (hdslave_inner_clap_entry.get_factory (CLAP_PLUGIN_FACTORY_ID));
}

uint32_t CLAP_ABI wrapFactoryCount (const clap_plugin_factory_t*)
{
    const auto* f = innerPluginFactory();
    return f ? f->get_plugin_count (f) : 0;
}

const clap_plugin_descriptor_t* CLAP_ABI wrapFactoryDescriptor (const clap_plugin_factory_t*, uint32_t idx)
{
    const auto* f = innerPluginFactory();
    return f ? f->get_plugin_descriptor (f, idx) : nullptr;
}

const clap_plugin_t* CLAP_ABI wrapFactoryCreate (const clap_plugin_factory_t*, const clap_host_t* host, const char* pluginId)
{
    if (!host || !clap_version_is_compatible (host->clap_version))
        return nullptr;
    const auto* f = innerPluginFactory();
    if (!f) return nullptr;

    const clap_plugin_descriptor_t* desc = nullptr;
    const uint32_t count = f->get_plugin_count (f);
    for (uint32_t i = 0; i < count; ++i)
    {
        const auto* d = f->get_plugin_descriptor (f, i);
        if (d && std::strcmp (d->id, pluginId) == 0)
        {
            desc = d;
            break;
        }
    }
    if (!desc) return nullptr;

    auto* dp = new (std::nothrow) DesktopPlugin();
    if (!dp) return nullptr;

    dp->realHost = host;
    dp->proxyHost = *host;
    dp->proxyHost.host_data = dp;
    dp->proxyHost.get_extension = proxyGetExtension;
    dp->proxyHost.request_restart = proxyRequestRestart;
    dp->proxyHost.request_process = proxyRequestProcess;
    dp->proxyHost.request_callback = proxyRequestCallback;

    dp->inner = f->create_plugin (f, &dp->proxyHost, pluginId);
    if (!dp->inner)
    {
        delete dp;
        return nullptr;
    }

    dp->theme = themeForPluginId (desc->id);
    dp->pluginName = desc->name ? desc->name : "hdslave";
    dp->pluginVersion = desc->version ? desc->version : "1.0.0";

    dp->clap = clap_plugin_t {
        desc, dp, wrapInit, wrapDestroy, wrapActivate, wrapDeactivate,
        wrapStartProcessing, wrapStopProcessing, wrapReset, wrapProcess,
        wrapGetExtension, wrapOnMainThread
    };
    return &dp->clap;
}

const clap_plugin_factory_t kWrapFactory { wrapFactoryCount, wrapFactoryDescriptor, wrapFactoryCreate };

bool CLAP_ABI wrapEntryInit (const char* path)
{
    return hdslave_inner_clap_entry.init (path);
}

void CLAP_ABI wrapEntryDeinit()
{
    hdslave_inner_clap_entry.deinit();
}

const void* CLAP_ABI wrapEntryGetFactory (const char* factoryId)
{
    if (!factoryId) return nullptr;
    if (std::strcmp (factoryId, CLAP_PLUGIN_FACTORY_ID) == 0)
        return &kWrapFactory;
    return hdslave_inner_clap_entry.get_factory (factoryId);
}

} // namespace

// DesktopPlugin methods ---------------------------------------------------------------------------------------

DesktopPlugin::DesktopPlugin()
{
    for (int i = 0; i < 128; ++i)
        ccMap[i].store (CLAP_INVALID_ID, std::memory_order_relaxed);
}

DesktopPlugin::~DesktopPlugin() = default;

int DesktopPlugin::findParamIndex (clap_id id) const
{
    for (size_t i = 0; i < params.size(); ++i)
    {
        if (params[i].id == id) return int (i);
    }
    return -1;
}

double DesktopPlugin::getParamValue (clap_id id) const
{
    double v = 0.0;
    if (innerParams && innerParams->get_value (inner, id, &v))
        return v;
    return 0.0;
}

std::string DesktopPlugin::formatParamValue (clap_id id, double value) const
{
    char buf[96] {};
    if (innerParams && innerParams->value_to_text (inner, id, value, buf, sizeof (buf)))
        return std::string (buf);
    std::snprintf (buf, sizeof (buf), "%.2f", value);
    return std::string (buf);
}

void DesktopPlugin::enqueueUiParam (UiEventKind kind, clap_id id, double value)
{
    uiToAudio.push ({ kind, id, value });
    if (hostParams && hostParams->request_flush)
        hostParams->request_flush (realHost);
    else if (realHost && realHost->request_process)
        realHost->request_process (realHost);
}

bool DesktopPlugin::loadPresetByIndex (int idx)
{
    if (!innerPresetLoad || idx < 0 || idx >= int (presets.size()))
        return false;
    bool ok = innerPresetLoad->from_location (inner, CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN,
                                              nullptr, presets[size_t (idx)].loadKey.c_str());
    if (ok)
    {
        currentPresetIndex = idx;
        screenWholeNeeded = true;
        uiDirty.store (true, std::memory_order_release);
        if (hostParams && hostParams->rescan)
            hostParams->rescan (realHost, CLAP_PARAM_RESCAN_VALUES);
    }
    return ok;
}

int DesktopPlugin::findCcForParam (clap_id id) const
{
    for (int c = 0; c < 128; ++c)
    {
        if (ccMap[c].load (std::memory_order_relaxed) == id)
            return c;
    }
    return -1;
}

void DesktopPlugin::clearCcForParam (clap_id id)
{
    for (int c = 0; c < 128; ++c)
    {
        if (ccMap[c].load (std::memory_order_relaxed) == id)
            ccMap[c].store (CLAP_INVALID_ID, std::memory_order_release);
    }
}

void DesktopPlugin::clearAllCc()
{
    for (int c = 0; c < 128; ++c)
        ccMap[c].store (CLAP_INVALID_ID, std::memory_order_release);
}

int DesktopPlugin::countMappedCc() const
{
    int n = 0;
    for (int c = 0; c < 128; ++c)
    {
        if (ccMap[c].load (std::memory_order_relaxed) != CLAP_INVALID_ID)
            ++n;
    }
    return n;
}

void DesktopPlugin::sendScreenInput (uint32_t kind, int32_t value, bool shift)
{
    if (!innerScreen || !innerScreen->input) return;
    melodee_screen_input_t inp { kind, value, shift };
    innerScreen->input (inner, &inp);
    screenDirty.store (true, std::memory_order_release);
}

void DesktopPlugin::updateEmbeddedScreen (uint64_t nowMs)
{
    if (!innerScreen || !innerScreen->paint) return;
    if (screenPixels.size() != size_t (MELODEE_SCREEN_WIDTH * MELODEE_SCREEN_HEIGHT))
        screenPixels.assign (size_t (MELODEE_SCREEN_WIDTH * MELODEE_SCREEN_HEIGHT), 0xFF0A0C10u);

    melodee_screen_canvas_t canvas {};
    canvas.pixels = screenPixels.data();
    canvas.width = MELODEE_SCREEN_WIDTH;
    canvas.height = MELODEE_SCREEN_HEIGHT;
    canvas.stride = MELODEE_SCREEN_WIDTH;
    canvas.now_ms = nowMs;
    canvas.accent = theme.accent;
    for (int i = 0; i < 3; ++i)
    {
        canvas.knobs[i].position = -1;
        canvas.knobs[i].engaged = true;
    }
    canvas.shift = false;
    canvas.playing = transportPlaying.load (std::memory_order_relaxed);
    canvas.whole = screenWholeNeeded;

    innerScreen->paint (inner, &canvas);
    screenWholeNeeded = false;
    screenDirty.store (false, std::memory_order_release);
}

} // namespace hdslave_desktop

extern "C" CLAP_EXPORT const clap_plugin_entry_t clap_entry = {
    CLAP_VERSION,
    hdslave_desktop::wrapEntryInit,
    hdslave_desktop::wrapEntryDeinit,
    hdslave_desktop::wrapEntryGetFactory,
};
