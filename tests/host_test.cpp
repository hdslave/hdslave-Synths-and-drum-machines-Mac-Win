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

namespace
{
const void* CLAP_ABI hostGetExtension (const clap_host_t*, const char*) { return nullptr; }
void CLAP_ABI hostRequestRestart (const clap_host_t*) {}
void CLAP_ABI hostRequestProcess (const clap_host_t*) {}
void CLAP_ABI hostRequestCallback (const clap_host_t*) {}

uint32_t CLAP_ABI emptyInSize (const clap_input_events_t*) { return 0; }
const clap_event_header_t* CLAP_ABI emptyInGet (const clap_input_events_t*, uint32_t) { return nullptr; }
bool CLAP_ABI emptyOutPush (const clap_output_events_t*, const clap_event_header_t*) { return true; }

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

std::string resolveBinaryPath (const std::string& clapPath)
{
#ifdef __APPLE__
    // On macOS, a .clap is a bundle directory: Foo.clap/Contents/MacOS/Foo
    size_t slash = clapPath.find_last_of ('/');
    std::string base = (slash == std::string::npos) ? clapPath : clapPath.substr (slash + 1);
    if (base.size() > 5 && base.substr (base.size() - 5) == ".clap")
        base = base.substr (0, base.size() - 5);
    return clapPath + "/Contents/MacOS/" + base;
#else
    return clapPath;
#endif
}

bool testOnePlugin (const std::string& clapPath)
{
    const std::string binPath = resolveBinaryPath (clapPath);

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

    if (!entry)
    {
        std::fprintf (stderr, "FAIL: clap_entry symbol missing in %s\n", binPath.c_str());
        return false;
    }

    if (!entry->init (clapPath.c_str()))
    {
        std::fprintf (stderr, "FAIL: entry->init failed for %s\n", clapPath.c_str());
        return false;
    }

    const auto* factory = static_cast<const clap_plugin_factory_t*> (entry->get_factory (CLAP_PLUGIN_FACTORY_ID));
    if (!factory || factory->get_plugin_count (factory) == 0)
    {
        std::fprintf (stderr, "FAIL: no plugins in factory for %s\n", clapPath.c_str());
        return false;
    }

    const auto* desc = factory->get_plugin_descriptor (factory, 0);
    clap_host_t host { CLAP_VERSION, nullptr, "hdslave-host-test", "hdslave", "", "1.0.0",
                       hostGetExtension, hostRequestRestart, hostRequestProcess, hostRequestCallback };

    const clap_plugin_t* plug = factory->create_plugin (factory, &host, desc->id);
    if (!plug || !plug->init (plug))
    {
        std::fprintf (stderr, "FAIL: create/init failed for %s\n", desc->id);
        return false;
    }

    // 1. Verify CLAP_EXT_GUI
    const auto* gui = static_cast<const clap_plugin_gui_t*> (plug->get_extension (plug, CLAP_EXT_GUI));
    if (!gui)
    {
        std::fprintf (stderr, "FAIL: CLAP_EXT_GUI missing for %s\n", desc->id);
        return false;
    }
    const char* api = nullptr;
    bool floating = true;
    uint32_t gw = 0, gh = 0;
    if (!gui->get_preferred_api (plug, &api, &floating) || floating || !api || !gui->get_size (plug, &gw, &gh) || gw < 900 || gh < 560)
    {
        std::fprintf (stderr, "FAIL: invalid GUI config for %s (%ux%u)\n", desc->id, gw, gh);
        return false;
    }

    // 2. Verify CLAP_EXT_PARAMS and count modules
    const auto* params = static_cast<const clap_plugin_params_t*> (plug->get_extension (plug, CLAP_EXT_PARAMS));
    if (!params)
    {
        std::fprintf (stderr, "FAIL: CLAP_EXT_PARAMS missing for %s\n", desc->id);
        return false;
    }
    const uint32_t pCount = params->count (plug);
    std::set<std::string> modules;
    for (uint32_t i = 0; i < pCount; ++i)
    {
        clap_param_info_t info {};
        if (params->get_info (plug, i, &info))
            modules.insert (info.module);
    }

    // 3. Verify CLAP_EXT_STATE save/load round-trip
    const auto* state = static_cast<const clap_plugin_state_t*> (plug->get_extension (plug, CLAP_EXT_STATE));
    if (!state)
    {
        std::fprintf (stderr, "FAIL: CLAP_EXT_STATE missing for %s\n", desc->id);
        return false;
    }
    MemStream ms;
    clap_ostream_t os { &ms, MemStream::write };
    if (!state->save (plug, &os) || ms.data.size() < 12 || std::memcmp (ms.data.data(), "HDSD", 4) != 0)
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

    // 4. Verify audio processing (4 blocks of 256 samples, no NaN/Inf)
    if (!plug->activate (plug, 48000.0, 32, 512) || !plug->start_processing (plug))
    {
        std::fprintf (stderr, "FAIL: activate/start failed for %s\n", desc->id);
        return false;
    }

    float inL[256] {}, inR[256] {}, outL[256] {}, outR[256] {};
    float* inPtrs[2] = { inL, inR };
    float* outPtrs[2] = { outL, outR };
    clap_audio_buffer_t inBuf { inPtrs, nullptr, 2, 0, 0 };
    clap_audio_buffer_t outBuf { outPtrs, nullptr, 2, 0, 0 };
    clap_input_events_t inEv { nullptr, emptyInSize, emptyInGet };
    clap_output_events_t outEv { nullptr, emptyOutPush };

    for (int blk = 0; blk < 4; ++blk)
    {
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
            {
                std::fprintf (stderr, "FAIL: non-finite audio output in %s\n", desc->id);
                return false;
            }
        }
    }

    std::printf ("PASS  %-24s  gui=%s (%ux%u)  params=%3u  modules=%2zu  state=%zuB\n",
                 desc->name, api, gw, gh, pCount, modules.size(), ms.data.size());

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
    if (argc < 2)
    {
        std::fprintf (stderr, "Usage: %s <plugin1.clap> [plugin2.clap ...]\n", argv[0]);
        return 1;
    }
    int failures = 0;
    for (int i = 1; i < argc; ++i)
    {
        if (!testOnePlugin (argv[i]))
            ++failures;
    }
    if (failures > 0)
    {
        std::fprintf (stderr, "%d plugin(s) failed.\n", failures);
        return 1;
    }
    std::printf ("All %d plugins passed!\n", argc - 1);
    return 0;
}
