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

// The events the inner plug-in sees: the host's, plus the panel's edits and mapped CCs, in time order (CLAP's rule;
// the plug-ins cut their blocks at each event). Lives in the DesktopPlugin; process and flush never overlap.
struct MergedInputEvents
{
    static constexpr uint32_t kMaxInjected = 1024;
    static constexpr uint32_t kMaxTotal = 8192;

    const clap_input_events_t* hostIn = nullptr;
    uint32_t hostCount = 0;
    clap_event_param_value_t injected[kMaxInjected] {};
    uint32_t injectedCount = 0;
    const clap_event_header_t* order[kMaxTotal] {};
    uint32_t orderCount = 0;
    bool passthrough = false; // too many events to merge: the host's alone

    void reset (const clap_input_events_t* in)
    {
        hostIn = in;
        hostCount = (in && in->size) ? in->size (in) : 0;
        injectedCount = 0;
        orderCount = 0;
        passthrough = false;
    }

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

    // Merge by time: injected events go just after host events of the same time (a CC lands after its own MIDI
    // event), and the panel's (time 0) before the block's first host event at time > 0.
    void finish()
    {
        if (injectedCount == 0) { passthrough = true; return; }
        if (hostCount + injectedCount > kMaxTotal) { passthrough = true; return; }
        // Injected events arrive in time order already (UI at 0, then CCs as the host's events are scanned).
        uint32_t h = 0, j = 0;
        while (h < hostCount || j < injectedCount)
        {
            const clap_event_header_t* he = h < hostCount ? hostIn->get (hostIn, h) : nullptr;
            if (h < hostCount && he == nullptr) { ++h; continue; }
            if (j < injectedCount && (he == nullptr || injected[j].header.time < he->time))
                order[orderCount++] = &injected[j++].header;
            else
                order[orderCount++] = he, ++h;
        }
    }

    static uint32_t CLAP_ABI sizeCb (const clap_input_events_t* list)
    {
        const auto* self = static_cast<const MergedInputEvents*> (list->ctx);
        return self->passthrough ? self->hostCount : self->orderCount;
    }

    static const clap_event_header_t* CLAP_ABI getCb (const clap_input_events_t* list, uint32_t index)
    {
        const auto* self = static_cast<const MergedInputEvents*> (list->ctx);
        if (self->passthrough)
            return (self->hostIn && index < self->hostCount) ? self->hostIn->get (self->hostIn, index) : nullptr;
        return index < self->orderCount ? self->order[index] : nullptr;
    }

    clap_input_events_t asClapList() { return clap_input_events_t { this, sizeCb, getCb }; }
};

namespace
{

DesktopPlugin& of (const clap_plugin_t* p) { return *static_cast<DesktopPlugin*> (p->plugin_data); }

// Proxy host callbacks ----------------------------------------------------------------------------------------

// The inner plug-in only ever sees proxyHost. Every host extension it asks for is a trampoline here that calls the
// real host's function with the real clap_host: handing the host's own vtable out with our proxy pointer would make
// the host read our struct as its own (FL Studio crashed in Fruity Wrapper on the first preset load that way).

DesktopPlugin* fromHost (const clap_host_t* host) { return host ? static_cast<DesktopPlugin*> (host->host_data) : nullptr; }

void CLAP_ABI proxyScreenRequestPaint (const clap_host_t* host)
{
    if (auto* dp = fromHost (host))
        dp->screenRequested.store (true, std::memory_order_release);
}
const clap_host_melodee_screen_t kProxyHostScreen { proxyScreenRequestPaint };

void scheduleMainThreadFlush (DesktopPlugin* dp)
{
    if (!dp || !dp->realHost) return;
    if (dp->realHost->request_callback)
    {
        dp->realHost->request_callback (dp->realHost);
    }
    else
    {
        // Minimal hosts without request_callback: flush immediately on main thread.
        const uint32_t flags = dp->pendingRescanFlags.exchange (0, std::memory_order_acq_rel);
        if (flags != 0 && dp->hostParams && dp->hostParams->rescan)
            dp->hostParams->rescan (dp->realHost, flags);
        if (dp->pendingStateDirty.exchange (false, std::memory_order_acq_rel) && dp->hostState && dp->hostState->mark_dirty)
            dp->hostState->mark_dirty (dp->realHost);
    }
}

void CLAP_ABI tParamsRescan (const clap_host_t* h, clap_param_rescan_flags flags)
{
    auto* dp = fromHost (h);
    if (!dp) return;
    dp->markAllDirty();
    // During live screen input, the inner plug-in also queues p.outgoing for out_events; skip redundant main-thread
    // rescan(VALUES) here so it never races against the audio thread's out_events in FL Studio.
    if (dp->inScreenInput && flags == CLAP_PARAM_RESCAN_VALUES)
        return;
    dp->pendingRescanFlags.fetch_or (flags, std::memory_order_acq_rel);
    scheduleMainThreadFlush (dp);
}
void CLAP_ABI tParamsClear (const clap_host_t* h, clap_id id, clap_param_clear_flags flags)
{
    auto* dp = fromHost (h);
    if (dp && dp->hostParams && dp->hostParams->clear) dp->hostParams->clear (dp->realHost, id, flags);
}
void CLAP_ABI tParamsRequestFlush (const clap_host_t* h)
{
    auto* dp = fromHost (h);
    if (!dp) return;
    // Only request a host flush when inactive/sleeping; when actively processing audio, process() drains right away.
    if (dp->processing.load (std::memory_order_acquire) && (nowMs() - dp->lastProcessMs.load (std::memory_order_relaxed)) <= 40)
        return;
    if (dp->hostParams && dp->hostParams->request_flush) dp->hostParams->request_flush (dp->realHost);
}
const clap_host_params_t kProxyHostParams { tParamsRescan, tParamsClear, tParamsRequestFlush };

void CLAP_ABI tPresetOnError (const clap_host_t* h, uint32_t kind, const char* location, const char* key, int32_t err, const char* msg)
{
    auto* dp = fromHost (h);
    if (!dp || dp->inInternalPresetLoad) return;
    if (dp->hostPresetLoad && dp->hostPresetLoad->on_error)
        dp->hostPresetLoad->on_error (dp->realHost, kind, location ? location : "", key ? key : "", err, msg ? msg : "");
}
void CLAP_ABI tPresetLoaded (const clap_host_t* h, uint32_t kind, const char* location, const char* key)
{
    auto* dp = fromHost (h);
    // Never call host->loaded(..., nullptr, ...) during an internal GUI preset change: FL Studio did not initiate
    // the load and can crash dereferencing a null location pointer. rescan(VALUES) + mark_dirty already sync the host.
    if (!dp || dp->inInternalPresetLoad) return;
    if (dp->hostPresetLoad && dp->hostPresetLoad->loaded)
        dp->hostPresetLoad->loaded (dp->realHost, kind, location ? location : "", key ? key : "");
}
const clap_host_preset_load_t kProxyHostPresetLoad { tPresetOnError, tPresetLoaded };

void CLAP_ABI tStateMarkDirty (const clap_host_t* h)
{
    auto* dp = fromHost (h);
    if (!dp) return;
    dp->pendingStateDirty.store (true, std::memory_order_release);
    scheduleMainThreadFlush (dp);
}
const clap_host_state_t kProxyHostState { tStateMarkDirty };

void CLAP_ABI tLog (const clap_host_t* h, clap_log_severity severity, const char* msg)
{
    auto* dp = fromHost (h);
    if (dp && dp->hostLog && dp->hostLog->log) dp->hostLog->log (dp->realHost, severity, msg);
}
const clap_host_log_t kProxyHostLog { tLog };

bool CLAP_ABI tIsMainThread (const clap_host_t* h)
{
    auto* dp = fromHost (h);
    return dp && dp->hostThreadCheck && dp->hostThreadCheck->is_main_thread ? dp->hostThreadCheck->is_main_thread (dp->realHost) : true;
}
bool CLAP_ABI tIsAudioThread (const clap_host_t* h)
{
    auto* dp = fromHost (h);
    return dp && dp->hostThreadCheck && dp->hostThreadCheck->is_audio_thread ? dp->hostThreadCheck->is_audio_thread (dp->realHost) : true;
}
const clap_host_thread_check_t kProxyHostThreadCheck { tIsMainThread, tIsAudioThread };

const void* CLAP_ABI proxyGetExtension (const clap_host_t* host, const char* extensionId)
{
    auto* dp = fromHost (host);
    if (!dp || !extensionId) return nullptr;
    dp->resolveHostExtensions();
    if (std::strcmp (extensionId, CLAP_EXT_MELODEE_SCREEN_HOST) == 0) return &kProxyHostScreen;
    if (std::strcmp (extensionId, CLAP_EXT_PARAMS) == 0) return dp->hostParams ? &kProxyHostParams : nullptr;
    if (std::strcmp (extensionId, CLAP_EXT_PRESET_LOAD) == 0 || std::strcmp (extensionId, CLAP_EXT_PRESET_LOAD_COMPAT) == 0)
        return dp->hostPresetLoad ? &kProxyHostPresetLoad : nullptr;
    if (std::strcmp (extensionId, CLAP_EXT_STATE) == 0) return dp->hostState ? &kProxyHostState : nullptr;
    if (std::strcmp (extensionId, CLAP_EXT_LOG) == 0) return dp->hostLog ? &kProxyHostLog : nullptr;
    if (std::strcmp (extensionId, CLAP_EXT_THREAD_CHECK) == 0) return dp->hostThreadCheck ? &kProxyHostThreadCheck : nullptr;
    return nullptr; // anything else: not offered (never the real host's vtable)
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

// Wraps the host's clap_output_events so every CLAP_EVENT_PARAM_VALUE sent to FL Studio (from our UI, MIDI CC
// mapper, or any inner plug-in's tellHost/takeChange/button-release) has a valid param_id, finite clamped value,
// monotonic timestamp, and is ALWAYS bracketed by CLAP_EVENT_PARAM_GESTURE_BEGIN / END.
struct SafeOutputEvents
{
    DesktopPlugin* dp = nullptr;
    const clap_output_events_t* hostOut = nullptr;
    uint32_t maxTime = 0;
    uint32_t lastTime = 0;

    bool pushRaw (const clap_event_header_t* h)
    {
        if (!hostOut || !hostOut->try_push || !h) return false;
        return hostOut->try_push (hostOut, h);
    }

    uint32_t clampTime (uint32_t t)
    {
        if (t > maxTime) t = maxTime;
        if (t < lastTime) t = lastTime;
        lastTime = t;
        return t;
    }

    bool pushGesture (uint32_t time, clap_id id, bool begin)
    {
        if (!dp || !hostOut || !hostOut->try_push) return false;
        if (begin)
        {
            if (dp->openGestureParamId == id) return true;
            if (dp->openGestureParamId != CLAP_INVALID_ID)
                pushGesture (time, dp->openGestureParamId, false);
        }
        else
        {
            if (dp->openGestureParamId != id) return true;
        }
        clap_event_param_gesture_t ev {};
        ev.header.size = sizeof (ev);
        ev.header.time = clampTime (time);
        ev.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        ev.header.type = begin ? CLAP_EVENT_PARAM_GESTURE_BEGIN : CLAP_EVENT_PARAM_GESTURE_END;
        ev.header.flags = 0;
        ev.param_id = id;
        const bool ok = pushRaw (&ev.header);
        if (ok) dp->openGestureParamId = begin ? id : CLAP_INVALID_ID;
        return ok;
    }

    bool pushParamValue (uint32_t time, clap_id id, double value)
    {
        if (!dp || !hostOut || !hostOut->try_push) return false;
        const int idx = dp->findParamIndex (id);
        if (idx < 0) return false;
        const auto& p = dp->params[size_t (idx)];
        if (!std::isfinite (value)) value = p.defVal;
        value = std::clamp (value, p.minVal, p.maxVal);

        const uint32_t t = clampTime (time);
        const bool alreadyOpen = (dp->openGestureParamId == id);
        if (!alreadyOpen)
        {
            if (dp->openGestureParamId != CLAP_INVALID_ID)
            {
                // Keep another active UI drag clean: don't interleave a bare background edit mid-gesture.
                return true;
            }
            if (!pushGesture (t, id, true)) return false;
        }

        clap_event_param_value_t ev {};
        ev.header.size = sizeof (ev);
        ev.header.time = t;
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
        const bool ok = pushRaw (&ev.header);

        if (!alreadyOpen)
            pushGesture (t, id, false);
        return ok;
    }

    static bool CLAP_ABI tryPushCb (const clap_output_events_t* list, const clap_event_header_t* h)
    {
        auto* self = static_cast<SafeOutputEvents*> (list->ctx);
        if (!self || !self->hostOut || !self->hostOut->try_push || !h) return false;
        if (h->space_id == CLAP_CORE_EVENT_SPACE_ID)
        {
            if (h->type == CLAP_EVENT_PARAM_VALUE)
            {
                const auto* pv = reinterpret_cast<const clap_event_param_value_t*> (h);
                return self->pushParamValue (h->time, pv->param_id, pv->value);
            }
            if (h->type == CLAP_EVENT_PARAM_GESTURE_BEGIN)
            {
                const auto* pg = reinterpret_cast<const clap_event_param_gesture_t*> (h);
                return self->pushGesture (h->time, pg->param_id, true);
            }
            if (h->type == CLAP_EVENT_PARAM_GESTURE_END)
            {
                const auto* pg = reinterpret_cast<const clap_event_param_gesture_t*> (h);
                return self->pushGesture (h->time, pg->param_id, false);
            }
        }
        return self->pushRaw (h);
    }

    clap_output_events_t asClapList() { return clap_output_events_t { this, tryPushCb }; }
};

void drainUiAndMidiEvents (DesktopPlugin& dp, MergedInputEvents& merged, const clap_input_events_t* in, SafeOutputEvents& out)
{
    merged.reset (in);

    // 1. The panel's edits: to the inner plug-in, and to the host with their gestures (automation, Last Tweaked).
    UiParamEvent uev {};
    while (dp.uiToAudio.pop (uev))
    {
        if (uev.kind == UiEventKind::GestureBegin)
            out.pushGesture (0, uev.paramId, true);
        else if (uev.kind == UiEventKind::GestureEnd)
            out.pushGesture (0, uev.paramId, false);
        else
        {
            merged.addInjected (0, uev.paramId, uev.value);
            out.pushParamValue (0, uev.paramId, uev.value);
        }
    }

    // 2. MIDI CCs: learn, then mapped CC -> parameter at the CC's own frame.
    for (uint32_t i = 0; i < merged.hostCount; ++i)
    {
        const auto* h = in->get (in, i);
        if (!h || h->space_id != CLAP_CORE_EVENT_SPACE_ID || h->type != CLAP_EVENT_MIDI) continue;
        const auto* m = reinterpret_cast<const clap_event_midi_t*> (h);
        if ((m->data[0] & 0xF0u) != 0xB0u) continue;

        const uint8_t cc = m->data[1] & 0x7Fu;
        const uint8_t val = m->data[2] & 0x7Fu;
        dp.lastMidiCc.store (int (cc), std::memory_order_relaxed);

        const uint32_t target = dp.midiLearnTarget.load (std::memory_order_acquire);
        if (target != CLAP_INVALID_ID && target != DesktopPlugin::kLearnArmedNoParam)
        {
            for (int c = 0; c < 128; ++c)
                if (dp.ccMap[c].load (std::memory_order_relaxed) == target) dp.ccMap[c].store (CLAP_INVALID_ID, std::memory_order_relaxed);
            dp.ccMap[cc].store (target, std::memory_order_release);
            dp.midiLearnTarget.store (CLAP_INVALID_ID, std::memory_order_release);
        }

        const clap_id mappedId = dp.ccMap[cc].load (std::memory_order_acquire);
        if (mappedId == CLAP_INVALID_ID) continue;
        const int idx = dp.findParamIndex (mappedId);
        if (idx < 0) continue;
        const auto& p = dp.params[size_t (idx)];
        double v = p.minVal + (double (val) / 127.0) * p.range();
        if (p.isStepped()) v = std::clamp (std::round (v), p.minVal, p.maxVal);
        merged.addInjected (h->time, mappedId, v);
        out.pushParamValue (h->time, mappedId, v);
    }

    merged.finish();
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
    // Never race a main-thread flush against an in-flight audio-thread process() call.
    if (dp.inAudioCall.exchange (true, std::memory_order_acq_rel))
        return;
    SafeOutputEvents safeOut { &dp, out, 0, 0 };
    drainUiAndMidiEvents (dp, *dp.mergedFlush, in, safeOut);
    clap_input_events_t inList = dp.mergedFlush->asClapList();
    clap_output_events_t outList = safeOut.asClapList();
    dp.innerParams->flush (dp.inner, &inList, out ? &outList : nullptr);
    dp.inAudioCall.store (false, std::memory_order_release);
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
            dp.markAllDirty();
            return true;
        }
    }

    // Raw MPC state fallback
    MemIStream innerIn { raw.data(), raw.size(), 0 };
    clap_istream_t memIn { &innerIn, MemIStream::readCb };
    bool ok = dp.innerState->load (dp.inner, &memIn);
    if (ok)
        dp.markAllDirty();
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
        dp.markAllDirty();
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
    *width = std::clamp (*width, kMinGuiWidth, kMaxGuiWidth);
    *height = std::clamp (*height, kMinGuiHeight, kMaxGuiHeight);
    return true;
}

bool CLAP_ABI guiSetSize (const clap_plugin_t* p, uint32_t width, uint32_t height)
{
    auto& dp = of (p);
    dp.guiWidth = std::clamp (width, kMinGuiWidth, kMaxGuiWidth);
    dp.guiHeight = std::clamp (height, kMinGuiHeight, kMaxGuiHeight);
    if (dp.ui)
        dp.ui->setSize (int (dp.guiWidth), int (dp.guiHeight), dp.ui->scale());
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

    dp.resolveHostExtensions();

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
    dp.uiValue.assign (dp.params.size(), 0.0);
    dp.uiHoldUntil.assign (dp.params.size(), 0);
    for (size_t i = 0; i < dp.params.size(); ++i)
        dp.uiValue[i] = dp.getParamValue (dp.params[i].id);

    discoverFactoryPresets (dp);

    if (dp.innerScreen)
    {
        dp.screenPixels.assign (size_t (MELODEE_SCREEN_WIDTH * MELODEE_SCREEN_HEIGHT), 0xFF0A0C10u);
        dp.refreshScreenKnobs();
    }

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
    const bool ok = dp.inner && dp.inner->activate (dp.inner, sr, minF, maxF);
    dp.active.store (ok, std::memory_order_release);
    return ok;
}

void CLAP_ABI wrapDeactivate (const clap_plugin_t* p)
{
    auto& dp = of (p);
    dp.processing.store (false, std::memory_order_release);
    dp.active.store (false, std::memory_order_release);
    dp.openGestureParamId = CLAP_INVALID_ID;
    if (dp.inner) dp.inner->deactivate (dp.inner);
}

bool CLAP_ABI wrapStartProcessing (const clap_plugin_t* p)
{
    auto& dp = of (p);
    const bool ok = dp.inner && dp.inner->start_processing (dp.inner);
    dp.processing.store (ok, std::memory_order_release);
    return ok;
}

void CLAP_ABI wrapStopProcessing (const clap_plugin_t* p)
{
    auto& dp = of (p);
    dp.processing.store (false, std::memory_order_release);
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

    dp.inAudioCall.store (true, std::memory_order_release);
    dp.lastProcessMs.store (nowMs(), std::memory_order_relaxed);

    if (proc->transport)
        dp.transportPlaying.store ((proc->transport->flags & CLAP_TRANSPORT_IS_PLAYING) != 0, std::memory_order_relaxed);

    const uint32_t maxTime = proc->frames_count > 0 ? proc->frames_count - 1 : 0;
    SafeOutputEvents safeOut { &dp, proc->out_events, maxTime, 0 };
    drainUiAndMidiEvents (dp, *dp.mergedProcess, proc->in_events, safeOut);

    clap_process_t wrapped = *proc;
    clap_input_events_t inList = dp.mergedProcess->asClapList();
    clap_output_events_t outList = safeOut.asClapList();
    wrapped.in_events = &inList;
    wrapped.out_events = proc->out_events ? &outList : nullptr;
    const clap_process_status st = dp.inner->process (dp.inner, &wrapped);
    dp.inAudioCall.store (false, std::memory_order_release);
    return st;
}

// Test hook (com.hdslave.desktop.debug/1) for off-screen rendering & input tests in tests/host_test.cpp
bool debugUiOpen (const clap_plugin_t* p)
{
    auto& dp = of (p);
    if (!dp.ui) dp.ui = new (std::nothrow) PanelUI (&dp);
    return dp.ui != nullptr;
}
void debugUiClose (const clap_plugin_t* p)
{
    auto& dp = of (p);
    delete dp.ui;
    dp.ui = nullptr;
}
const uint32_t* debugUiRender (const clap_plugin_t* p, int w, int h, int scale, int* physW, int* physH)
{
    auto& dp = of (p);
    if (!dp.ui) return nullptr;
    dp.ui->setSize (w, h, scale);
    dp.ui->update (nowMs());
    if (physW) *physW = dp.ui->physWidth();
    if (physH) *physH = dp.ui->physHeight();
    return dp.ui->pixels();
}
void debugUiMouse (const clap_plugin_t* p, int kind, float x, float y, int button, uint32_t mods, float delta, int clicks)
{
    auto& dp = of (p);
    if (!dp.ui) return;
    if (kind == 0) dp.ui->mouseDown (x, y, button, mods, clicks);
    else if (kind == 1) dp.ui->mouseMove (x, y, mods);
    else if (kind == 2) dp.ui->mouseUp (x, y, button, mods);
    else if (kind == 3) dp.ui->wheel (x, y, 0.0f, delta, false, mods);
}
int debugPresetCount (const clap_plugin_t* p) { return int (of (p).presets.size()); }
bool debugPresetLoad (const clap_plugin_t* p, int index) { return of (p).loadPresetByIndex (index); }
double debugMsPerRender (const clap_plugin_t* p, int w, int h, int scale)
{
    auto& dp = of (p);
    if (!dp.ui) return 0.0;
    dp.ui->setSize (w, h, scale);
    dp.ui->invalidateAll();
    dp.ui->update (nowMs());
    return dp.ui->lastRenderMs();
}
const hdslave_desktop_debug_api_t kDebugApi {
    1, debugUiOpen, debugUiClose, debugUiRender, debugUiMouse, debugPresetCount, debugPresetLoad, debugMsPerRender
};

const void* CLAP_ABI wrapGetExtension (const clap_plugin_t* p, const char* id)
{
    auto& dp = of (p);
    if (!id) return nullptr;
    if (std::strcmp (id, "com.hdslave.desktop.debug/1") == 0) return &kDebugApi;
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

    // Deliver any deferred host notifications safely on the host's own main-thread callback tick.
    const uint32_t rescanFlags = dp.pendingRescanFlags.exchange (0, std::memory_order_acq_rel);
    if (rescanFlags != 0 && dp.hostParams && dp.hostParams->rescan)
        dp.hostParams->rescan (dp.realHost, rescanFlags);
    if (dp.pendingStateDirty.exchange (false, std::memory_order_acq_rel) && dp.hostState && dp.hostState->mark_dirty)
        dp.hostState->mark_dirty (dp.realHost);
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
    dp->pluginId = desc->id ? desc->id : "";
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

uint64_t nowMs()
{
    using namespace std::chrono;
    return uint64_t (duration_cast<milliseconds> (steady_clock::now().time_since_epoch()).count());
}

DesktopPlugin::DesktopPlugin()
{
    mergedProcess = new MergedInputEvents();
    mergedFlush = new MergedInputEvents();
    for (int i = 0; i < 128; ++i)
        ccMap[i].store (CLAP_INVALID_ID, std::memory_order_relaxed);
}

DesktopPlugin::~DesktopPlugin()
{
    delete mergedProcess;
    mergedProcess = nullptr;
    delete mergedFlush;
    mergedFlush = nullptr;
}

void DesktopPlugin::resolveHostExtensions()
{
    if (hostExtsResolved || !realHost || !realHost->get_extension)
        return;
    hostExtsResolved = true;
    hostParams = static_cast<const clap_host_params_t*> (realHost->get_extension (realHost, CLAP_EXT_PARAMS));
    hostPresetLoad = static_cast<const clap_host_preset_load_t*> (realHost->get_extension (realHost, CLAP_EXT_PRESET_LOAD));
    if (!hostPresetLoad)
        hostPresetLoad = static_cast<const clap_host_preset_load_t*> (realHost->get_extension (realHost, CLAP_EXT_PRESET_LOAD_COMPAT));
    hostState = static_cast<const clap_host_state_t*> (realHost->get_extension (realHost, CLAP_EXT_STATE));
    hostLog = static_cast<const clap_host_log_t*> (realHost->get_extension (realHost, CLAP_EXT_LOG));
    hostThreadCheck = static_cast<const clap_host_thread_check_t*> (realHost->get_extension (realHost, CLAP_EXT_THREAD_CHECK));
}

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

double DesktopPlugin::displayValue (int index) const
{
    if (index < 0 || index >= int (params.size()))
        return 0.0;
    if (size_t (index) < uiHoldUntil.size() && nowMs() < uiHoldUntil[size_t (index)])
        return uiValue[size_t (index)];
    return getParamValue (params[size_t (index)].id);
}

std::string DesktopPlugin::formatParamValue (clap_id id, double value) const
{
    char buf[96] {};
    if (innerParams && innerParams->value_to_text (inner, id, value, buf, sizeof (buf)))
        return std::string (buf);
    std::snprintf (buf, sizeof (buf), "%.2f", value);
    return std::string (buf);
}

void DesktopPlugin::enqueue (UiEventKind kind, clap_id id, double value)
{
    uiToAudio.push ({ kind, id, value });
    // When the audio thread is actively running process(), it drains uiToAudio on the very next audio block;
    // avoid spamming request_flush() mid-stream (which can trigger concurrent main-thread flush in some hosts).
    const bool audioTicking = processing.load (std::memory_order_acquire)
                           && (nowMs() - lastProcessMs.load (std::memory_order_relaxed)) <= 40;
    if (!audioTicking)
    {
        if (hostParams && hostParams->request_flush)
            hostParams->request_flush (realHost);
        else if (realHost && realHost->request_process)
            realHost->request_process (realHost);
    }
}

void DesktopPlugin::beginEdit (int index)
{
    if (index < 0 || index >= int (params.size())) return;
    const double cur = displayValue (index);
    if (size_t (index) < uiValue.size())
    {
        uiValue[size_t (index)] = cur;
        uiHoldUntil[size_t (index)] = nowMs() + 250;
    }
    enqueue (UiEventKind::GestureBegin, params[size_t (index)].id, cur);
}

void DesktopPlugin::setEdit (int index, double value)
{
    if (index < 0 || index >= int (params.size())) return;
    const auto& p = params[size_t (index)];
    value = std::clamp (value, p.minVal, p.maxVal);
    if (p.isStepped()) value = std::clamp (std::round (value), p.minVal, p.maxVal);
    if (size_t (index) < uiValue.size())
    {
        uiValue[size_t (index)] = value;
        uiHoldUntil[size_t (index)] = nowMs() + 250;
    }
    enqueue (UiEventKind::Value, p.id, value);
    screenRequested.store (true, std::memory_order_release);
}

void DesktopPlugin::endEdit (int index)
{
    if (index < 0 || index >= int (params.size())) return;
    const double cur = displayValue (index);
    if (size_t (index) < uiHoldUntil.size())
        uiHoldUntil[size_t (index)] = nowMs() + 150;
    enqueue (UiEventKind::GestureEnd, params[size_t (index)].id, cur);
}

void DesktopPlugin::setValueOnce (int index, double value)
{
    if (index < 0 || index >= int (params.size())) return;
    beginEdit (index);
    setEdit (index, value);
    endEdit (index);
}

bool DesktopPlugin::loadPresetByIndex (int idx)
{
    if (!innerPresetLoad || idx < 0 || idx >= int (presets.size()))
        return false;
    resolveHostExtensions();
    inInternalPresetLoad = true;
    const bool ok = innerPresetLoad->from_location (inner, CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN,
                                                    nullptr, presets[size_t (idx)].loadKey.c_str());
    inInternalPresetLoad = false;
    if (ok)
    {
        currentPresetIndex = idx;
        markAllDirty();
        refreshScreenKnobs();
        pendingRescanFlags.fetch_or (CLAP_PARAM_RESCAN_VALUES, std::memory_order_acq_rel);
        pendingStateDirty.store (true, std::memory_order_release);
        scheduleMainThreadFlush (this);
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

void DesktopPlugin::refreshScreenKnobs()
{
    if (!innerScreen || !innerScreen->knobs)
        return;
    clap_id ids[3] { CLAP_INVALID_ID, CLAP_INVALID_ID, CLAP_INVALID_ID };
    innerScreen->knobs (inner, ids);
    for (int i = 0; i < 3; ++i)
        screenKnobIds[i] = ids[i];
}

void DesktopPlugin::sendScreenInput (uint32_t kind, int32_t value)
{
    if (!innerScreen || !innerScreen->input) return;
    melodee_screen_input_t inp { kind, value, screenShift };
    inScreenInput = true;
    innerScreen->input (inner, &inp);
    inScreenInput = false;
    refreshScreenKnobs();
    screenRequested.store (true, std::memory_order_release);
}

bool DesktopPlugin::paintScreen (uint64_t now)
{
    if (!innerScreen || !innerScreen->paint) return false;
    if (screenPixels.size() != size_t (MELODEE_SCREEN_WIDTH * MELODEE_SCREEN_HEIGHT))
    {
        screenPixels.assign (size_t (MELODEE_SCREEN_WIDTH * MELODEE_SCREEN_HEIGHT), 0xFF0A0C10u);
        screenWholeNeeded = true;
    }

    refreshScreenKnobs();

    melodee_screen_canvas_t canvas {};
    canvas.pixels = screenPixels.data();
    canvas.width = MELODEE_SCREEN_WIDTH;
    canvas.height = MELODEE_SCREEN_HEIGHT;
    canvas.stride = MELODEE_SCREEN_WIDTH;
    canvas.now_ms = now;
    canvas.accent = theme.accent;
    for (int i = 0; i < 3; ++i)
    {
        const int idx = (screenKnobIds[i] != CLAP_INVALID_ID) ? findParamIndex (screenKnobIds[i]) : -1;
        if (idx >= 0)
        {
            const auto& p = params[size_t (idx)];
            const double r = p.range();
            const double n = r > 0.0 ? std::clamp ((displayValue (idx) - p.minVal) / r, 0.0, 1.0) : 0.0;
            canvas.knobs[i].position = int16_t (std::round (n * 127.0));
            canvas.knobs[i].engaged = true;
        }
        else
        {
            canvas.knobs[i].position = -1;
            canvas.knobs[i].engaged = false;
        }
    }
    canvas.shift = screenShift;
    canvas.playing = transportPlaying.load (std::memory_order_relaxed);
    canvas.whole = screenWholeNeeded;

    const bool drew = innerScreen->paint (inner, &canvas);
    screenWholeNeeded = false;
    screenRequested.store (false, std::memory_order_release);
    refreshScreenKnobs();
    return drew;
}

void DesktopPlugin::markAllDirty()
{
    std::fill (uiHoldUntil.begin(), uiHoldUntil.end(), 0);
    screenWholeNeeded = true;
    screenRequested.store (true, std::memory_order_release);
    if (ui) ui->invalidateAll();
}

} // namespace hdslave_desktop

extern "C" CLAP_EXPORT const clap_plugin_entry_t clap_entry = {
    CLAP_VERSION,
    hdslave_desktop::wrapEntryInit,
    hdslave_desktop::wrapEntryDeinit,
    hdslave_desktop::wrapEntryGetFactory,
};
