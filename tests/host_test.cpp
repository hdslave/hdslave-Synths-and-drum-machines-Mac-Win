#include <clap/clap.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>
#include <string>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <dlfcn.h>
#endif

extern "C"
{
typedef struct hdslave_desktop_debug_api
{
    uint32_t version;
    bool (*ui_open) (const clap_plugin_t* plugin);
    void (*ui_close) (const clap_plugin_t* plugin);
    const uint32_t* (*ui_render) (const clap_plugin_t* plugin, int w, int h, int scale, int* physW, int* physH);
    void (*ui_mouse) (const clap_plugin_t* plugin, int kind, float x, float y, int button, uint32_t mods, float delta, int clicks);
    int (*preset_count) (const clap_plugin_t* plugin);
    bool (*preset_load) (const clap_plugin_t* plugin, int index);
    double (*ms_per_full_render) (const clap_plugin_t* plugin, int w, int h, int scale);
} hdslave_desktop_debug_api_t;
}

namespace
{
constexpr uint64_t kHostCanary = 0x4844534C5544494Full;

struct StrictHostCtx
{
    uint64_t canary = kHostCanary;
    const clap_host_t* expectedHost = nullptr;
    int badHostCalls = 0;
    int paramRescans = 0;
    int flushRequests = 0;
    int presetLoadedCalls = 0;
    int stateDirtyCalls = 0;
    int callbackRequests = 0;
    clap_id openGesture = CLAP_INVALID_ID;
    uint32_t lastOutTime = 0;
    int bareParamValues = 0;
    int outParamValues = 0;
};

void checkHostPtr (const clap_host_t* h, const char* fn)
{
    if (!h || !h->host_data)
    {
        std::fprintf (stderr, "FAIL: %s called with null host/host_data!\n", fn);
        std::abort();
    }
    auto* ctx = static_cast<StrictHostCtx*> (h->host_data);
    if (ctx->canary != kHostCanary || h != ctx->expectedHost)
    {
        std::fprintf (stderr, "FAIL: %s called with WRONG clap_host pointer (proxy leak — causes FL Studio crash)!\n", fn);
        std::abort();
    }
}

void CLAP_ABI hParamsRescan (const clap_host_t* h, clap_param_rescan_flags)
{
    checkHostPtr (h, "host_params.rescan");
    ++static_cast<StrictHostCtx*> (h->host_data)->paramRescans;
}
void CLAP_ABI hParamsClear (const clap_host_t* h, clap_id, clap_param_clear_flags)
{
    checkHostPtr (h, "host_params.clear");
}
void CLAP_ABI hParamsFlush (const clap_host_t* h)
{
    checkHostPtr (h, "host_params.request_flush");
    ++static_cast<StrictHostCtx*> (h->host_data)->flushRequests;
}
const clap_host_params_t kHostParams { hParamsRescan, hParamsClear, hParamsFlush };

void CLAP_ABI hPresetError (const clap_host_t* h, uint32_t, const char* loc, const char* key, int32_t, const char*)
{
    checkHostPtr (h, "host_preset_load.on_error");
    if (!loc || !key)
    {
        std::fprintf (stderr, "FAIL: host_preset_load.on_error received NULL string!\n");
        std::abort();
    }
}
void CLAP_ABI hPresetLoaded (const clap_host_t* h, uint32_t, const char* loc, const char* key)
{
    checkHostPtr (h, "host_preset_load.loaded");
    if (!loc || !key)
    {
        std::fprintf (stderr, "FAIL: host_preset_load.loaded received NULL location/key (crashes FL Studio)!\n");
        std::abort();
    }
    ++static_cast<StrictHostCtx*> (h->host_data)->presetLoadedCalls;
}
const clap_host_preset_load_t kHostPresetLoad { hPresetError, hPresetLoaded };

void CLAP_ABI hStateDirty (const clap_host_t* h)
{
    checkHostPtr (h, "host_state.mark_dirty");
    ++static_cast<StrictHostCtx*> (h->host_data)->stateDirtyCalls;
}
const clap_host_state_t kHostState { hStateDirty };

const void* CLAP_ABI hostGetExtension (const clap_host_t* h, const char* id)
{
    checkHostPtr (h, "host.get_extension");
    if (!id) return nullptr;
    if (std::strcmp (id, CLAP_EXT_PARAMS) == 0) return &kHostParams;
    if (std::strcmp (id, CLAP_EXT_PRESET_LOAD) == 0 || std::strcmp (id, CLAP_EXT_PRESET_LOAD_COMPAT) == 0) return &kHostPresetLoad;
    if (std::strcmp (id, CLAP_EXT_STATE) == 0) return &kHostState;
    return nullptr;
}
void CLAP_ABI hostRequestRestart (const clap_host_t* h) { checkHostPtr (h, "host.request_restart"); }
void CLAP_ABI hostRequestProcess (const clap_host_t* h) { checkHostPtr (h, "host.request_process"); }
void CLAP_ABI hostRequestCallback (const clap_host_t* h)
{
    checkHostPtr (h, "host.request_callback");
    ++static_cast<StrictHostCtx*> (h->host_data)->callbackRequests;
}

uint32_t CLAP_ABI emptyInSize (const clap_input_events_t*) { return 0; }
const clap_event_header_t* CLAP_ABI emptyInGet (const clap_input_events_t*, uint32_t) { return nullptr; }
bool CLAP_ABI strictOutPush (const clap_output_events_t* list, const clap_event_header_t* h)
{
    auto* ctx = static_cast<StrictHostCtx*> (list->ctx);
    if (!ctx || !h) return false;
    if (h->time < ctx->lastOutTime)
    {
        std::fprintf (stderr, "FAIL: out_events non-monotonic time (%u < %u)!\n", h->time, ctx->lastOutTime);
        std::abort();
    }
    ctx->lastOutTime = h->time;
    if (h->space_id == CLAP_CORE_EVENT_SPACE_ID)
    {
        if (h->type == CLAP_EVENT_PARAM_GESTURE_BEGIN)
            ctx->openGesture = reinterpret_cast<const clap_event_param_gesture_t*> (h)->param_id;
        else if (h->type == CLAP_EVENT_PARAM_GESTURE_END)
            ctx->openGesture = CLAP_INVALID_ID;
        else if (h->type == CLAP_EVENT_PARAM_VALUE)
        {
            ++ctx->outParamValues;
            const auto* pv = reinterpret_cast<const clap_event_param_value_t*> (h);
            if (ctx->openGesture != pv->param_id)
            {
                ++ctx->bareParamValues;
                std::fprintf (stderr, "FAIL: bare CLAP_EVENT_PARAM_VALUE without gesture (id=%u)!\n", pv->param_id);
                std::abort();
            }
        }
    }
    return true;
}

struct MemStream
{
    std::vector<uint8_t> data;
    size_t readPos = 0;

    static int64_t CLAP_ABI write (const clap_ostream_t* s, const void* buf, uint64_t size)
    {
        auto* self = static_cast<MemStream*> (s->ctx);
        const auto* b = static_cast<const uint8_t*> (buf);
        self->data.insert (self->data.end(), b, b + size);
        return int64_t (size);
    }

    static int64_t CLAP_ABI read (const clap_istream_t* s, void* buf, uint64_t size)
    {
        auto* self = static_cast<MemStream*> (s->ctx);
        size_t avail = (self->readPos < self->data.size()) ? (self->data.size() - self->readPos) : 0;
        size_t n = avail < size ? avail : size_t (size);
        if (n > 0)
        {
            std::memcpy (buf, self->data.data() + self->readPos, n);
            self->readPos += n;
        }
        return int64_t (n);
    }
};

void writePpm (const std::string& path, const uint32_t* argb, int w, int h)
{
    FILE* f = std::fopen (path.c_str(), "wb");
    if (!f) return;
    std::fprintf (f, "P6\n%d %d\n255\n", w, h);
    std::vector<uint8_t> row (size_t (w) * 3);
    for (int y = 0; y < h; ++y)
    {
        const uint32_t* src = argb + size_t (y) * size_t (w);
        for (int x = 0; x < w; ++x)
        {
            const uint32_t p = src[x];
            row[size_t (x) * 3 + 0] = uint8_t ((p >> 16) & 0xFFu);
            row[size_t (x) * 3 + 1] = uint8_t ((p >> 8) & 0xFFu);
            row[size_t (x) * 3 + 2] = uint8_t (p & 0xFFu);
        }
        std::fwrite (row.data(), 1, row.size(), f);
    }
    std::fclose (f);
}

std::string resolveBinaryPath (const std::string& clapPath, std::string& baseOut)
{
    size_t slash = clapPath.find_last_of ("/\\");
    std::string base = (slash == std::string::npos) ? clapPath : clapPath.substr (slash + 1);
    if (base.size() > 5 && base.substr (base.size() - 5) == ".clap")
        base = base.substr (0, base.size() - 5);
    baseOut = base;
#ifdef __APPLE__
    return clapPath + "/Contents/MacOS/" + base;
#else
    return clapPath;
#endif
}

bool testOnePlugin (const std::string& clapPath, const std::string& snapDir)
{
    std::string baseName;
    const std::string binPath = resolveBinaryPath (clapPath, baseName);

#ifdef _WIN32
    HMODULE lib = LoadLibraryA (binPath.c_str());
    if (!lib)
    {
        std::fprintf (stderr, "FAIL: LoadLibrary(%s) failed\n", binPath.c_str());
        return false;
    }
    const auto* entry = reinterpret_cast<const clap_plugin_entry_t*> (GetProcAddress (lib, "clap_entry"));
#else
    void* lib = dlopen (binPath.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!lib)
    {
        std::fprintf (stderr, "FAIL: dlopen(%s): %s\n", binPath.c_str(), dlerror());
        return false;
    }
    const auto* entry = reinterpret_cast<const clap_plugin_entry_t*> (dlsym (lib, "clap_entry"));
#endif

    if (!entry || !entry->init (clapPath.c_str()))
    {
        std::fprintf (stderr, "FAIL: entry missing or init failed for %s\n", binPath.c_str());
        return false;
    }

    const auto* factory = static_cast<const clap_plugin_factory_t*> (entry->get_factory (CLAP_PLUGIN_FACTORY_ID));
    if (!factory || factory->get_plugin_count (factory) == 0)
    {
        std::fprintf (stderr, "FAIL: no plugins in factory for %s\n", clapPath.c_str());
        return false;
    }

    const auto* desc = factory->get_plugin_descriptor (factory, 0);
    StrictHostCtx ctx {};
    clap_host_t host { CLAP_VERSION, &ctx, "hdslave-strict-host", "hdslave", "", "1.0.0",
                       hostGetExtension, hostRequestRestart, hostRequestProcess, hostRequestCallback };
    ctx.expectedHost = &host;

    const clap_plugin_t* plug = factory->create_plugin (factory, &host, desc->id);
    if (!plug || !plug->init (plug))
    {
        std::fprintf (stderr, "FAIL: create/init failed for %s\n", desc->id);
        return false;
    }

    // 1. Verify CLAP_EXT_GUI
    const auto* gui = static_cast<const clap_plugin_gui_t*> (plug->get_extension (plug, CLAP_EXT_GUI));
    const char* api = nullptr;
    bool floating = true;
    uint32_t gw = 0, gh = 0;
    if (!gui || !gui->get_preferred_api (plug, &api, &floating) || floating || !api || !gui->get_size (plug, &gw, &gh) || gw < 1000 || gh < 640)
    {
        std::fprintf (stderr, "FAIL: invalid GUI config for %s (%ux%u)\n", desc->id, gw, gh);
        return false;
    }

    // 2. Verify CLAP_EXT_PARAMS
    const auto* params = static_cast<const clap_plugin_params_t*> (plug->get_extension (plug, CLAP_EXT_PARAMS));
    if (!params) return false;
    const uint32_t pCount = params->count (plug);
    std::set<std::string> modules;
    for (uint32_t i = 0; i < pCount; ++i)
    {
        clap_param_info_t info {};
        if (params->get_info (plug, i, &info))
            modules.insert (info.module);
    }

    // 3. Verify CLAP_EXT_STATE round-trip
    const auto* state = static_cast<const clap_plugin_state_t*> (plug->get_extension (plug, CLAP_EXT_STATE));
    MemStream ms;
    clap_ostream_t os { &ms, MemStream::write };
    if (!state || !state->save (plug, &os) || ms.data.size() < 12 || std::memcmp (ms.data.data(), "HDSD", 4) != 0)
    {
        std::fprintf (stderr, "FAIL: state save envelope invalid for %s\n", desc->id);
        return false;
    }
    clap_istream_t is { &ms, MemStream::read };
    if (!state->load (plug, &is))
    {
        std::fprintf (stderr, "FAIL: state load failed for %s\n", desc->id);
        return false;
    }

    // 4. Audio processing
    if (!plug->activate (plug, 48000.0, 32, 512) || !plug->start_processing (plug))
        return false;

    float inL[256] {}, inR[256] {}, outL[256] {}, outR[256] {};
    float* inPtrs[2] = { inL, inR };
    float* outPtrs[2] = { outL, outR };
    clap_audio_buffer_t inBuf { inPtrs, nullptr, 2, 0, 0 };
    clap_audio_buffer_t outBuf { outPtrs, nullptr, 2, 0, 0 };
    clap_input_events_t inEv { nullptr, emptyInSize, emptyInGet };
    clap_output_events_t outEv { &ctx, strictOutPush };

    for (int blk = 0; blk < 4; ++blk)
    {
        ctx.lastOutTime = 0;
        clap_process_t proc {};
        proc.steady_time = blk * 256;
        proc.frames_count = 256;
        proc.audio_inputs = &inBuf;
        proc.audio_inputs_count = 1;
        proc.audio_outputs = &outBuf;
        proc.audio_outputs_count = 1;
        proc.in_events = &inEv;
        proc.out_events = &outEv;
        plug->process (plug, &proc);
        for (int i = 0; i < 256; ++i)
        {
            if (!std::isfinite (outL[i]) || !std::isfinite (outR[i]))
                return false;
        }
    }

    // 5. Strict Preset Load stress test (exercises proxy host trampolines + deferred on_main_thread!) + UI render
    const auto* dbg = static_cast<const hdslave_desktop_debug_api_t*> (plug->get_extension (plug, "com.hdslave.desktop.debug/1"));
    int pPresets = 0;
    double renderMs = 0.0;
    if (dbg)
    {
        if (!dbg->ui_open (plug)) return false;
        pPresets = dbg->preset_count (plug);
        clap_event_note_t testNote {};
        testNote.header = { sizeof (testNote), 0, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_NOTE_ON, 0 };
        testNote.note_id = -1;
        testNote.port_index = 0;
        testNote.channel = 0;
        testNote.key = 48;
        testNote.velocity = 0.85;
        auto oneNoteSize = [] (const clap_input_events_t*) -> uint32_t { return 1; };
        auto oneNoteGet = [] (const clap_input_events_t* l, uint32_t) -> const clap_event_header_t* {
            return static_cast<const clap_event_header_t*> (l->ctx);
        };
        clap_input_events_t noteIn { &testNote.header, oneNoteSize, oneNoteGet };

        for (int pi = 0; pi < pPresets; ++pi)
        {
            if (!dbg->preset_load (plug, pi))
            {
                std::fprintf (stderr, "FAIL: preset_load(%d) failed for %s\n", pi, desc->id);
                return false;
            }
            if (plug->on_main_thread) plug->on_main_thread (plug);

            // Render 6 blocks with a note-on + subtle audio input to catch any NaN/Inf in every single preset!
            for (int blk = 0; blk < 6; ++blk)
            {
                for (int i = 0; i < 256; ++i) inL[i] = inR[i] = 0.1f * std::sin (float (i) * 0.05f);
                ctx.lastOutTime = 0;
                clap_process_t proc {};
                proc.steady_time = (pi * 6 + blk) * 256;
                proc.frames_count = 256;
                proc.audio_inputs = &inBuf;
                proc.audio_inputs_count = 1;
                proc.audio_outputs = &outBuf;
                proc.audio_outputs_count = 1;
                proc.in_events = (blk == 0) ? &noteIn : &inEv;
                proc.out_events = &outEv;
                plug->process (plug, &proc);
                for (int i = 0; i < 256; ++i)
                {
                    if (!std::isfinite (outL[i]) || !std::isfinite (outR[i]))
                    {
                        std::fprintf (stderr, "FAIL: non-finite audio on preset %d in %s\n", pi, desc->id);
                        return false;
                    }
                }
            }
        }
        if (pPresets > 0)
        {
            dbg->preset_load (plug, 0);
            if (plug->on_main_thread) plug->on_main_thread (plug);
        }

        // Exercise live screen click/drag + process block to prove SafeOutputEvents brackets every tellHost push
        dbg->ui_mouse (plug, 0, 200, 200, 0, 0, 0, 1);
        dbg->ui_mouse (plug, 1, 260, 200, 0, 0, 0, 1);
        dbg->ui_mouse (plug, 2, 260, 200, 0, 0, 0, 1);
        dbg->ui_mouse (plug, 0, 200, 200, 0, 0, 0, 1);
        dbg->ui_mouse (plug, 2, 200, 200, 0, 0, 0, 1);
        ctx.lastOutTime = 0;
        clap_process_t proc {};
        proc.steady_time = 1024;
        proc.frames_count = 256;
        proc.audio_inputs = &inBuf;
        proc.audio_inputs_count = 1;
        proc.audio_outputs = &outBuf;
        proc.audio_outputs_count = 1;
        proc.in_events = &inEv;
        proc.out_events = &outEv;
        plug->process (plug, &proc);
        if (plug->on_main_thread) plug->on_main_thread (plug);

        int pw = 0, ph = 0;
        const uint32_t* px = dbg->ui_render (plug, int (gw), int (gh), 1, &pw, &ph);
        if (!px || pw != int (gw) || ph != int (gh))
        {
            std::fprintf (stderr, "FAIL: ui_render failed for %s\n", desc->id);
            return false;
        }
        renderMs = dbg->ms_per_full_render (plug, int (gw), int (gh), 1);

        if (!snapDir.empty())
            writePpm (snapDir + "/" + baseName + ".ppm", px, pw, ph);

        dbg->ui_close (plug);
    }

    std::printf ("PASS  %-22s  gui=%s (%ux%u)  params=%3u  mods=%2zu  presets=%2d  render=%.1fms  rescans=%d\n",
                 desc->name, api, gw, gh, pCount, modules.size(), pPresets, renderMs, ctx.paramRescans);

    plug->stop_processing (plug);
    plug->deactivate (plug);
    plug->destroy (plug);
    entry->deinit();

#ifdef _WIN32
    FreeLibrary (lib);
#else
    dlclose (lib);
#endif
    return true;
}
} // namespace

int main (int argc, char** argv)
{
    std::string snapDir;
    std::vector<std::string> plugins;
    for (int i = 1; i < argc; ++i)
    {
        if (std::strcmp (argv[i], "--snap") == 0 && i + 1 < argc)
            snapDir = argv[++i];
        else
            plugins.push_back (argv[i]);
    }
    if (plugins.empty())
    {
        std::fprintf (stderr, "Usage: %s [--snap <dir>] <plugin1.clap> ...\n", argv[0]);
        return 1;
    }
    int failures = 0;
    for (const auto& p : plugins)
    {
        if (!testOnePlugin (p, snapDir))
            ++failures;
    }
    if (failures > 0)
    {
        std::fprintf (stderr, "%d plugin(s) failed.\n", failures);
        return 1;
    }
    std::printf ("All %zu plugins passed strict host, preset & UI tests!\n", plugins.size());
    return 0;
}
