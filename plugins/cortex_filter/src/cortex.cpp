// Cortex Filter: a stereo 3D morphing filter as a CLAP effect, made for the Akai MPC Sample. This file is the CLAP side: the
// factory, ports, parameters, state, presets, Melodee's screen and the process call, which cuts the block at each
// event so every change lands on its frame. The sound is in engine.h, the filter's maths in morph.h, the cubes in
// cubes.h, the filters, sequences and sequencer in filters.h, the page in screen.h.
#include "engine.h"
#include "params.h"
#include "presets.h"
#include "screen.h"
#include "melodee_screen.h"

#include <clap/clap.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <new>

namespace cortex
{
namespace
{
const char* const features[] = { CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_FILTER, CLAP_PLUGIN_FEATURE_STEREO, nullptr };

const clap_plugin_descriptor descriptor {
    CLAP_VERSION, "com.hdslave.cortex-filter", "hdslave Cortex Filter", "hdslave", "", "", "", CORTEX_VERSION,
    "A stereo filter that morphs through a cube of eight complex filters: Morph, Freq and Transform pick the point, an "
    "LFO and an envelope follower move it, a sequencer steps through a bank of filters, and the screen shows it in 3D",
    features,
};

struct Plugin
{
    clap_plugin clap {};
    const clap_host* host = nullptr;
    // Every parameter's value. The audio thread writes it from events; the main thread reads it, and writes it when
    // a state is loaded or the page changes one. The engine notices those at the next block.
    std::atomic<float> values[paramCount];
    std::atomic<bool> mainChanged { false };
    std::atomic<bool> settle { false }; // a state was loaded: its values land at once, not gliding (Engine::settle)
    float applied[paramCount] {};
    Engine engine;
    bool active = false;
    const clap_host_params* hostParams = nullptr;
    // Melodee's screen (melodee_screen.h): the page, what the audio thread tells it, the values it changed (told to
    // the host from the next process or flush, a bit a row) and whether an input asked for a paint now.
    const clap_host_melodee_screen* hostScreen = nullptr;
    Feed feed;
    Commands commands;
    Screen screen { values, feed, engine.bank(), commands };
    std::atomic<uint64_t> outgoing { 0 };
    std::atomic<bool> paintAsked { false };
    std::atomic<uint32_t> meterUntil { 0 }; // the page's meters are kept up until this block (about a second after a paint)
    uint32_t sincePaint = 0; // frames since the audio thread last asked for a paint
    std::atomic<bool> inputSeen { false }; // an input on the page since its last paint (used or not)
    // A state's bank and sequences as loadBank() reads them, whole before any of it is used (main thread; each
    // instance its own).
    struct BankLoad
    {
        struct Filter
        {
            bool used, own;
            char name[nameLength + 1];
            float fields[fieldCount];
        };
        Filter filters[slotCount + 1];
        uint8_t steps[sequenceCount + 1][maxSteps];
        int lengths[sequenceCount + 1];
        uint8_t seq[1 + 4096];
    } bankLoad;
};

Plugin& of (const clap_plugin* p) { return *static_cast<Plugin*> (p->plugin_data); }

void invalidatePage (Plugin& p)
{
    p.paintAsked.store (true, std::memory_order_release);
    if (p.hostScreen != nullptr)
        p.hostScreen->request_paint (p.host);
}

// The values the screen's page set, told to the host (so its own copy, its automation and its knobs follow).
void tellHost (Plugin& p, const clap_output_events* out, uint32_t time)
{
    if (out == nullptr || p.outgoing.load (std::memory_order_relaxed) == 0)
        return;
    uint64_t rows = p.outgoing.exchange (0, std::memory_order_acquire);
    for (uint32_t i = 0; rows != 0; ++i, rows >>= 1)
    {
        if ((rows & 1u) == 0)
            continue;
        clap_event_param_value e {};
        e.header = { sizeof (e), time, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_PARAM_VALUE, 0 };
        e.param_id = paramTable[i].id;
        e.note_id = e.port_index = e.channel = e.key = -1;
        e.value = p.values[i].load (std::memory_order_relaxed);
        if (! out->try_push (out, &e.header))
            p.outgoing.fetch_or (1ull << i, std::memory_order_relaxed); // again next time
    }
}

// Parameters --------------------------------------------------------------------------------------------------

uint32_t paramsCount (const clap_plugin*) { return paramCount; }

bool paramsInfo (const clap_plugin*, uint32_t index, clap_param_info* info)
{
    if (index >= paramCount)
        return false;
    const auto& p = paramTable[index];
    *info = {};
    info->id = p.id;
    info->flags = CLAP_PARAM_IS_AUTOMATABLE | (p.kind != Kind::continuous ? (uint32_t) CLAP_PARAM_IS_STEPPED : 0u)
                | (p.kind == Kind::choice ? (uint32_t) CLAP_PARAM_IS_ENUM : 0u);
    std::snprintf (info->name, sizeof (info->name), "%s", p.name);
    std::snprintf (info->module, sizeof (info->module), "%s", p.module);
    info->min_value = p.min;
    info->max_value = p.max;
    info->default_value = p.def;
    return true;
}

bool paramsValue (const clap_plugin* plugin, clap_id id, double* value)
{
    const int index = paramIndex (id);
    if (index < 0)
        return false;
    *value = of (plugin).values[index].load (std::memory_order_relaxed);
    return true;
}

bool paramsToText (const clap_plugin* plugin, clap_id id, double value, char* text, uint32_t size)
{
    const int index = paramIndex (id);
    if (index < 0 || size == 0)
        return false;
    if (id == ParamId::filter) // its number and its name: "F07 Bell Tower"
    {
        const int slot = (int) clampParam (paramTable[index], (float) value);
        std::snprintf (text, size, "F%02d %s", slot, of (plugin).engine.bank().name (slot));
        return true;
    }
    formatParam (paramTable[index], (float) value, text, size);
    return true;
}

bool paramsFromText (const clap_plugin*, clap_id id, const char* text, double* value)
{
    const int index = paramIndex (id);
    if (index < 0)
        return false;
    float v = 0.0f;
    if (! parseParam (paramTable[index], text, v))
        return false;
    *value = v;
    return true;
}

// The filter the edit filter's parameters show and change: the one Filter picks.
int editSlot (const Plugin& p) { return (int) p.values[paramIndex (ParamId::filter)].load (std::memory_order_relaxed); }

// The edit filter's parameters show filter `slot`'s settings; those that moved are told to the host. (`applied` is
// the audio thread's: the main thread leaves it, and the next block sees the new values and passes them by.)
void showFilter (Plugin& p, int slot, bool audio)
{
    const Store& bank = p.engine.bank();
    for (int f = 0; f < fieldCount; ++f)
    {
        const int index = paramIndex (fieldParam (f));
        const float v = bank.field (slot, f);
        if (p.values[index].load (std::memory_order_relaxed) != v)
        {
            p.values[index].store (v, std::memory_order_relaxed);
            if (audio)
                p.applied[index] = v;
            p.outgoing.fetch_or (1ull << index, std::memory_order_release);
        }
    }
}

// A parameter's new value, on the thread it arrives on: kept, and on the audio thread given to the engine (the main
// thread's reach it at the next block). An edit filter's field goes into the bank's filter that Filter picks; a new
// Filter shows its settings in those fields.
void setValue (Plugin& p, int index, float value, bool audio)
{
    const Param& info = paramTable[index];
    value = clampParam (info, value);
    p.values[index].store (value, std::memory_order_relaxed);
    if (isEditField (info.id))
    {
        p.engine.bank().setField (editSlot (p), (int) (info.id - ParamId::editCube), value);
        if (audio)
            p.applied[index] = value;
        return;
    }
    if (audio)
        p.applied[index] = value, p.engine.set (info.id, value);
    if (info.id == ParamId::filter)
        showFilter (p, (int) value, audio);
}

// One event: parameter values go to the engine at once.
void handle (Plugin& p, const clap_event_header* e)
{
    if (e->space_id != CLAP_CORE_EVENT_SPACE_ID || e->type != CLAP_EVENT_PARAM_VALUE)
        return;
    const auto* v = reinterpret_cast<const clap_event_param_value*> (e);
    const int index = paramIndex (v->param_id);
    if (index >= 0)
        setValue (p, index, (float) v->value, true);
}

void paramsFlush (const clap_plugin* plugin, const clap_input_events* in, const clap_output_events* out)
{
    auto& p = of (plugin);
    const uint32_t count = in != nullptr ? in->size (in) : 0;
    bool changed = false;
    for (uint32_t i = 0; i < count; ++i)
    {
        const auto* e = in->get (in, i);
        // Outside process the engine is not running: the values are kept, and taken at the next block.
        if (e->space_id == CLAP_CORE_EVENT_SPACE_ID && e->type == CLAP_EVENT_PARAM_VALUE)
        {
            const auto* v = reinterpret_cast<const clap_event_param_value*> (e);
            const int index = paramIndex (v->param_id);
            if (index >= 0)
            {
                setValue (p, index, (float) v->value, false);
                p.mainChanged.store (true, std::memory_order_release);
                changed = true;
            }
        }
    }
    tellHost (p, out, 0);
    if (changed)
        invalidatePage (p);
}

const clap_plugin_params paramsExtension { paramsCount, paramsInfo, paramsValue, paramsToText, paramsFromText, paramsFlush };

// State: "CRTX", a version, the number of values, then (id, value) pairs; unknown ids are skipped, missing ones keep
// their defaults, so a state saved by another version still loads. From version 2 the bank follows: "BANK", the
// number of filters and of fields, then each filter (in use, its own name or not, its name in 12 bytes, its fields);
// then "SEQS", the number of sequences and of steps, then each sequence (its length, its steps). A version 1 state
// (0.1.0) has no bank: its Cube (id 0) loads as the factory's filter for that cube.
constexpr char stateTag[4] = { 'C', 'R', 'T', 'X' }, bankTag[4] = { 'B', 'A', 'N', 'K' }, seqsTag[4] = { 'S', 'E', 'Q', 'S' };
constexpr uint32_t stateVersion = 2;

bool writeAll (const clap_ostream* s, const void* data, uint64_t size)
{
    const auto* at = static_cast<const uint8_t*> (data);
    while (size > 0)
    {
        const int64_t n = s->write (s, at, size);
        if (n <= 0)
            return false;
        at += n;
        size -= (uint64_t) n;
    }
    return true;
}

bool readAll (const clap_istream* s, void* data, uint64_t size)
{
    auto* at = static_cast<uint8_t*> (data);
    while (size > 0)
    {
        const int64_t n = s->read (s, at, size);
        if (n <= 0)
            return false;
        at += n;
        size -= (uint64_t) n;
    }
    return true;
}

// Every value at once, from the main thread (a state or a preset loaded). The edit filter's fields are not taken
// from `values`: they show the bank's filter that Filter picks.
void setAll (Plugin& p, const float* values)
{
    for (uint32_t i = 0; i < paramCount; ++i)
        if (! isEditField (paramTable[i].id))
            p.values[i].store (values[i], std::memory_order_relaxed);
    showFilter (p, editSlot (p), false);
    p.mainChanged.store (true, std::memory_order_release);
    if (! p.active) // not processing: the engine takes them now; otherwise at the next block
        for (uint32_t i = 0; i < paramCount; ++i)
            p.applied[i] = p.values[i].load (std::memory_order_relaxed), p.engine.set (paramTable[i].id, p.applied[i]);
    invalidatePage (p);
}

bool stateSave (const clap_plugin* plugin, const clap_ostream* stream)
{
    auto& p = of (plugin);
    const uint32_t header[2] = { stateVersion, paramCount };
    if (! writeAll (stream, stateTag, 4) || ! writeAll (stream, header, sizeof (header)))
        return false;
    for (uint32_t i = 0; i < paramCount; ++i)
    {
        const struct { uint32_t id; float value; } pair { paramTable[i].id, p.values[i].load (std::memory_order_relaxed) };
        if (! writeAll (stream, &pair, sizeof (pair)))
            return false;
    }
    const Store& bank = p.engine.bank();
    const uint32_t sizes[2] = { (uint32_t) slotCount, (uint32_t) fieldCount };
    if (! writeAll (stream, bankTag, 4) || ! writeAll (stream, sizes, sizeof (sizes)))
        return false;
    for (int s = 1; s <= slotCount; ++s)
    {
        uint8_t head[2 + nameLength] = {};
        head[0] = bank.used (s) ? 1 : 0;
        head[1] = bank.ownName (s) ? 1 : 0;
        if (head[1] != 0)
            std::strncpy (reinterpret_cast<char*> (head + 2), bank.name (s), nameLength);
        float fields[fieldCount];
        for (int f = 0; f < fieldCount; ++f)
            fields[f] = bank.field (s, f);
        if (! writeAll (stream, head, sizeof (head)) || ! writeAll (stream, fields, sizeof (fields)))
            return false;
    }
    const uint32_t seqSizes[2] = { (uint32_t) sequenceCount, (uint32_t) maxSteps };
    if (! writeAll (stream, seqsTag, 4) || ! writeAll (stream, seqSizes, sizeof (seqSizes)))
        return false;
    for (int q = 1; q <= sequenceCount; ++q)
    {
        uint8_t seq[1 + maxSteps] = {};
        seq[0] = (uint8_t) bank.length (q);
        for (int i = 0; i < maxSteps; ++i)
            seq[1 + i] = (uint8_t) bank.step (q, i);
        if (! writeAll (stream, seq, sizeof (seq)))
            return false;
    }
    return true;
}

// The bank and the sequences of a version 2 state, after its values: each part read whole before any of it is used,
// so a cut-off state changes nothing. Counts other than this version's are read as far as both go.
bool loadBank (Plugin& p, const clap_istream* stream)
{
    char tag[4];
    uint32_t sizes[2];
    if (! readAll (stream, tag, 4) || std::memcmp (tag, bankTag, 4) != 0 || ! readAll (stream, sizes, sizeof (sizes)) || sizes[0] > 4096
        || sizes[1] > 256)
        return false;
    auto& filters = p.bankLoad.filters;
    float fields[256];
    for (uint32_t s = 1; s <= sizes[0]; ++s)
    {
        uint8_t head[2 + nameLength];
        if (! readAll (stream, head, sizeof (head)) || ! readAll (stream, fields, sizes[1] * sizeof (float)))
            return false;
        if (s > (uint32_t) slotCount)
            continue;
        auto& f = filters[s];
        f.used = head[0] != 0, f.own = head[1] != 0;
        std::memcpy (f.name, head + 2, nameLength), f.name[nameLength] = 0;
        for (int k = 0; k < fieldCount; ++k)
            f.fields[k] = k < (int) sizes[1] ? clampParam (fieldInfo (k), fields[k]) : fieldInfo (k).def;
    }
    uint32_t seqSizes[2];
    if (! readAll (stream, tag, 4) || std::memcmp (tag, seqsTag, 4) != 0 || ! readAll (stream, seqSizes, sizeof (seqSizes)) || seqSizes[0] > 4096
        || seqSizes[1] > 4096)
        return false;
    auto& steps = p.bankLoad.steps;
    auto& lengths = p.bankLoad.lengths;
    uint8_t* const seq = p.bankLoad.seq;
    for (uint32_t q = 1; q <= seqSizes[0]; ++q)
    {
        if (! readAll (stream, seq, 1 + seqSizes[1]))
            return false;
        if (q > (uint32_t) sequenceCount)
            continue;
        lengths[q] = std::min<int> ({ seq[0], (int) seqSizes[1], maxSteps });
        for (int i = 0; i < maxSteps; ++i)
            steps[q][i] = i < lengths[q] ? seq[1 + (size_t) i] : 0;
    }
    Store& bank = p.engine.bank();
    for (int s = 1; s <= std::min<int> ((int) sizes[0], slotCount); ++s)
        if (filters[s].used)
        {
            for (int k = 0; k < fieldCount; ++k)
                bank.setField (s, k, filters[s].fields[k]);
            bank.setName (s, filters[s].own ? filters[s].name : nullptr);
        }
        else
            bank.clearSlot (s);
    for (int q = 1; q <= std::min<int> ((int) seqSizes[0], sequenceCount); ++q)
        bank.setSequence (q, steps[q], lengths[q]);
    return true;
}

bool stateLoad (const clap_plugin* plugin, const clap_istream* stream)
{
    auto& p = of (plugin);
    char tag[4] = {};
    uint32_t header[2] = {};
    if (! readAll (stream, tag, 4) || std::memcmp (tag, stateTag, 4) != 0 || ! readAll (stream, header, sizeof (header))
        || header[0] == 0 || header[1] > 4096)
        return false;
    float loaded[paramCount];
    for (uint32_t i = 0; i < paramCount; ++i)
        loaded[i] = paramTable[i].def;
    bool sawFilter = false;
    float oldCube = -1.0f;
    for (uint32_t i = 0; i < header[1]; ++i)
    {
        struct { uint32_t id; float value; } pair {};
        if (! readAll (stream, &pair, sizeof (pair)))
            return false;
        if (pair.id == ParamId::retiredCube)
            oldCube = pair.value;
        const int index = paramIndex (pair.id);
        if (index >= 0)
            loaded[index] = clampParam (paramTable[index], pair.value), sawFilter |= pair.id == ParamId::filter;
    }
    if (! sawFilter && oldCube >= 0.0f) // 0.1.0's Cube: the factory's filter for it, F01..F16
        loaded[paramIndex (ParamId::filter)] = clampParam (paramTable[paramIndex (ParamId::filter)], std::round (oldCube) + 1.0f);
    if (header[0] >= 2)
    {
        if (! loadBank (p, stream))
            return false;
    }
    else
        p.engine.bank().factory();
    setAll (p, loaded);
    p.settle.store (true, std::memory_order_release);
    return true;
}

const clap_plugin_state stateExtension { stateSave, stateLoad };

// Presets (presets.h): inside the plug-in, listed by the preset-discovery factory below and loaded by their key.
// A preset sets every value, as a state does; then the host is told the values moved.
bool presetFromLocation (const clap_plugin* plugin, uint32_t kind, const char*, const char* key)
{
    auto& p = of (plugin);
    const Preset* preset = kind == CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN ? findPreset (key) : nullptr;
    const auto* hostPresets = static_cast<const clap_host_preset_load*> (p.host->get_extension (p.host, CLAP_EXT_PRESET_LOAD));
    if (preset == nullptr)
    {
        if (hostPresets != nullptr && hostPresets->on_error != nullptr)
            hostPresets->on_error (p.host, kind, nullptr, key, 0, "no such preset");
        return false;
    }
    float values[paramCount];
    presetValuesOf (*preset, values);
    setAll (p, values);
    if (p.hostParams != nullptr && p.hostParams->rescan != nullptr)
        p.hostParams->rescan (p.host, CLAP_PARAM_RESCAN_VALUES);
    if (hostPresets != nullptr && hostPresets->loaded != nullptr)
        hostPresets->loaded (p.host, kind, nullptr, key);
    return true;
}

const clap_plugin_preset_load presetExtension { presetFromLocation };

// Ports: one stereo in, one stereo out, which may be the same buffers. A mono input is heard on both sides.
uint32_t audioCount (const clap_plugin*, bool) { return 1; }
bool audioGet (const clap_plugin*, uint32_t index, bool input, clap_audio_port_info* info)
{
    if (index != 0)
        return false;
    *info = {};
    info->id = 0;
    std::snprintf (info->name, sizeof (info->name), "%s", input ? "In" : "Out");
    info->flags = CLAP_AUDIO_PORT_IS_MAIN;
    info->channel_count = 2;
    info->port_type = CLAP_PORT_STEREO;
    info->in_place_pair = 0;
    return true;
}
const clap_plugin_audio_ports audioExtension { audioCount, audioGet };

// The plug-in -------------------------------------------------------------------------------------------------

bool pluginInit (const clap_plugin* plugin)
{
    auto& p = of (plugin);
    cubeTables(); // built here, on the main thread, before any audio
    for (uint32_t i = 0; i < paramCount; ++i)
    {
        p.values[i].store (paramTable[i].def, std::memory_order_relaxed);
        p.applied[i] = paramTable[i].def;
        p.engine.set (paramTable[i].id, paramTable[i].def);
    }
    showFilter (p, editSlot (p), true); // not processing yet: this thread is the only one
    p.outgoing.store (0, std::memory_order_relaxed); // nothing to tell: the host reads the values as they are
    p.hostParams = static_cast<const clap_host_params*> (p.host->get_extension (p.host, CLAP_EXT_PARAMS));
    p.hostScreen = static_cast<const clap_host_melodee_screen*> (p.host->get_extension (p.host, CLAP_EXT_MELODEE_SCREEN_HOST));
    return true;
}

void pluginDestroy (const clap_plugin* plugin)
{
    auto* p = &of (plugin);
    p->~Plugin();
    ::operator delete (p, std::align_val_t (64));
}

bool pluginActivate (const clap_plugin* plugin, double rate, uint32_t, uint32_t maxFrames)
{
    auto& p = of (plugin);
    for (uint32_t i = 0; i < paramCount; ++i) // values the host set while inactive: in place before the glides settle
        p.applied[i] = p.values[i].load (std::memory_order_relaxed), p.engine.set (paramTable[i].id, p.applied[i]);
    p.engine.prepare (rate, maxFrames);
    p.feed.rate.store ((float) rate, std::memory_order_relaxed);
    p.active = true;
    invalidatePage (p);
    return true;
}

// Stopped, reset or deactivated: the page's presses not yet taken are dropped (none fires when the sound comes back).
void pluginDeactivate (const clap_plugin* plugin)
{
    auto& p = of (plugin);
    p.active = false;
    p.commands.clear();
}
bool pluginStart (const clap_plugin*) { return true; }
void pluginStop (const clap_plugin* plugin) { of (plugin).commands.clear(); }
void pluginReset (const clap_plugin* plugin)
{
    auto& p = of (plugin);
    p.commands.clear();
    p.engine.reset();
}

// Denormals: flush them to zero for the block (the filters ring down into them), as the FPU had it after.
struct FlushDenormals
{
#if defined(__aarch64__)
    uint64_t saved;
    FlushDenormals() { asm volatile ("mrs %0, fpcr" : "=r"(saved)); asm volatile ("msr fpcr, %0" ::"r"(saved | (1ull << 24))); }
    ~FlushDenormals() { asm volatile ("msr fpcr, %0" ::"r"(saved)); }
#elif defined(__SSE__)
    unsigned saved;
    FlushDenormals() { saved = __builtin_ia32_stmxcsr(); __builtin_ia32_ldmxcsr (saved | 0x8040); }
    ~FlushDenormals() { __builtin_ia32_ldmxcsr (saved); }
#endif
};

clap_process_status pluginProcess (const clap_plugin* plugin, const clap_process* process)
{
    auto& p = of (plugin);
    FlushDenormals flush;
    const uint32_t frames = process->frames_count;
    if (process->audio_outputs_count < 1 || process->audio_outputs[0].channel_count < 1 || process->audio_outputs[0].data32 == nullptr)
        return CLAP_PROCESS_ERROR;
    float* left = process->audio_outputs[0].data32[0];
    float* right = process->audio_outputs[0].channel_count > 1 ? process->audio_outputs[0].data32[1] : nullptr;
    const float* inL = nullptr;
    const float* inR = nullptr;
    if (process->audio_inputs_count >= 1 && process->audio_inputs[0].channel_count >= 1 && process->audio_inputs[0].data32 != nullptr)
    {
        inL = process->audio_inputs[0].data32[0];
        inR = process->audio_inputs[0].channel_count > 1 ? process->audio_inputs[0].data32[1] : nullptr;
    }

    // Values the main thread changed (a state loaded while playing, the page): looked through only when it wrote some.
    if (p.mainChanged.exchange (false, std::memory_order_acquire))
        for (uint32_t i = 0; i < paramCount; ++i)
        {
            const float v = p.values[i].load (std::memory_order_relaxed);
            if (v != p.applied[i])
                p.applied[i] = v, p.engine.set (paramTable[i].id, v);
        }
    if (p.settle.exchange (false, std::memory_order_acq_rel))
        p.engine.settle();

    // Events at the block's first frame come first, with the page's triggers and wheel (the sequencer's Inc, Dec,
    // Reset; stepping through a sequence's filters: those given in the last half second), the transport after them.
    const auto* in = process->in_events;
    const uint32_t count = in != nullptr ? in->size (in) : 0;
    uint32_t next = 0;
    for (; next < count && in->get (in, next)->time == 0; ++next)
        handle (p, in->get (in, next));
    if (p.commands.waiting())
        p.commands.take (Commands::nowMs(), [&p] (int kind, int amount) {
            if (kind == Commands::wheelKind)
                p.engine.browse (amount);
            else
                p.engine.trigger (kind, (uint32_t) amount);
        });

    if (const auto* t = process->transport)
    {
        const bool playing = (t->flags & CLAP_TRANSPORT_IS_PLAYING) != 0;
        const bool placed = (t->flags & CLAP_TRANSPORT_HAS_BEATS_TIMELINE) != 0;
        const double bpm = (t->flags & CLAP_TRANSPORT_HAS_TEMPO) && t->tempo > 0.0 ? t->tempo : 120.0;
        p.engine.transport (playing, placed, placed ? (double) t->song_pos_beats / (double) CLAP_BEATTIME_FACTOR : 0.0, bpm);
    }
    else
        p.engine.transport (false, false, 0.0, 120.0);

    // The page's input and output meters, only while it is being painted: the loudest sample of each side.
    const bool metering = p.feed.blocks.load (std::memory_order_relaxed) < p.meterUntil.load (std::memory_order_relaxed);
    auto peak = [frames] (const float* x) {
        float m = 0.0f;
        for (uint32_t i = 0; x != nullptr && i < frames; ++i)
            m = std::max (m, std::fabs (x[i]));
        return m;
    };
    float meters[6] = {};
    if (metering) // before the sound: the output may be the input's buffers
        meters[0] = peak (inL), meters[1] = peak (inR != nullptr ? inR : inL);

    uint32_t done = 0;
    while (done < frames)
    {
        // Every event due by now, then sound up to the next one.
        while (next < count)
        {
            const auto* e = in->get (in, next);
            if (e->time > done)
                break;
            handle (p, e);
            ++next;
        }
        uint32_t until = frames;
        if (next < count)
            until = std::min<uint32_t> (frames, std::max<uint32_t> (in->get (in, next)->time, done + 1));
        p.engine.render (inL != nullptr ? inL + done : nullptr, inR != nullptr ? inR + done : nullptr, left + done,
                         right != nullptr ? right + done : nullptr, until - done);
        done = until;
    }
    for (; next < count; ++next) // events stamped past the block (a host's mistake): applied at its end
        handle (p, in->get (in, next));
    tellHost (p, process->out_events, 0);

    // The page: where the filter is now; a paint asked for about 40 times a second (the page paces its frames).
    const Position l = p.engine.left(), r = p.engine.right();
    p.feed.l[0].store (l.x, std::memory_order_relaxed), p.feed.l[1].store (l.y, std::memory_order_relaxed), p.feed.l[2].store (l.z, std::memory_order_relaxed);
    p.feed.r[0].store (r.x, std::memory_order_relaxed), p.feed.r[1].store (r.y, std::memory_order_relaxed), p.feed.r[2].store (r.z, std::memory_order_relaxed);
    p.feed.split.store (p.engine.split(), std::memory_order_relaxed);
    p.feed.env.store (p.engine.envelope(), std::memory_order_relaxed);
    p.feed.lfo.store (p.engine.lfoPlace(), std::memory_order_relaxed);
    const Sequencer& seq = p.engine.sequencer();
    p.feed.slot.store (p.engine.slotNow(), std::memory_order_relaxed);
    p.feed.sequence.store (p.engine.sequencing() ? seq.sequence : 0, std::memory_order_relaxed);
    p.feed.step.store (seq.pos, std::memory_order_relaxed);
    p.feed.mode.store ((int) seq.mode, std::memory_order_relaxed);
    p.feed.fade.store (seq.fade, std::memory_order_relaxed);
    p.feed.triggers.store (p.engine.triggerCount(), std::memory_order_relaxed);
    p.feed.drives.store (p.engine.transformDrives(), std::memory_order_relaxed);
    for (int a = 0; a < 3; ++a)
        p.feed.control[a].store (p.engine.control (a), std::memory_order_relaxed);
    p.engine.takeBend (meters + 2);
    if (metering)
    {
        meters[4] = peak (left), meters[5] = peak (right != nullptr ? right : left);
        for (int m = 0; m < 6; ++m) // the loudest since the page last looked (it takes them as it paints)
            if (meters[m] > p.feed.meters[m].load (std::memory_order_relaxed))
                p.feed.meters[m].store (meters[m], std::memory_order_relaxed);
    }
    p.feed.blocks.fetch_add (1, std::memory_order_release);
    if (p.hostScreen != nullptr)
    {
        p.sincePaint += frames;
        if (p.sincePaint >= (uint32_t) (p.engine.sampleRate() * 0.025f))
        {
            p.sincePaint = 0;
            p.hostScreen->request_paint (p.host);
        }
    }
    return CLAP_PROCESS_CONTINUE;
}

// Melodee's screen ---------------------------------------------------------------------------------------------

hdui::Host hostOf (const melodee_screen_canvas_t* c)
{
    hdui::Host h;
    h.nowMs = c->now_ms;
    h.accent = c->accent;
    h.shift = c->shift;
    h.playing = c->playing;
    for (int k = 0; k < 3; ++k)
        h.pot[k] = c->knobs[k].position, h.engaged[k] = c->knobs[k].engaged;
    return h;
}

bool screenPaint (const clap_plugin* plugin, const melodee_screen_canvas_t* c)
{
    if (c == nullptr || c->pixels == nullptr || c->width != (uint32_t) hdui::width || c->height != (uint32_t) hdui::height
        || c->stride < c->width)
        return false;
    auto& p = of (plugin);
    const hdui::Host h = hostOf (c);
    const bool asked = p.paintAsked.exchange (false, std::memory_order_acq_rel), changed = p.screen.hostChanged (h);
    const bool input = p.inputSeen.exchange (false, std::memory_order_acq_rel);
    // The page wakes for a few seconds when it opens, on an input, a value changed, the host's own (a knob turned,
    // Shift, the song starting or stopping) and while sound runs (Screen::wake).
    if (c->whole || asked || changed || input || p.screen.newBlocks())
        p.screen.wake (h.nowMs);
    bool drew = false;
    if (! c->whole && ! asked && ! changed && p.screen.bodyAtRest (h))
        drew = p.screen.paintTitleOnly (h, c->pixels, (int) c->stride);
    else if (c->whole || asked || changed || p.screen.due (h.nowMs))
    {
        p.meterUntil.store (p.feed.blocks.load (std::memory_order_relaxed) + 400, std::memory_order_relaxed);
        p.screen.update (h.nowMs);
        drew = p.screen.paint (h, c->pixels, (int) c->stride, c->whole);
        p.screen.drew (h);
    }
    // The next frame only while something moves: otherwise the title rests and nothing is asked for until the sound,
    // a value or an input asks for a paint (each of them does), or the host paints on its own (the page in view
    // again, a knob, the transport).
    if (p.screen.moving (h) && p.hostScreen != nullptr)
        p.hostScreen->request_paint (p.host);
    return drew;
}

bool screenInput (const clap_plugin* plugin, const melodee_screen_input_t* e)
{
    if (e == nullptr || e->kind > MELODEE_SCREEN_NUDGE)
        return false;
    auto& p = of (plugin);
    p.inputSeen.store (true, std::memory_order_release); // wakes the page, even an input it has no use for
    hdui::Edits edits;
    const uint32_t commandsBefore = p.commands.count();
    const bool used = p.screen.input ((hdui::Key) e->kind, e->value, e->shift, edits);
    bool changed = false;
    for (int i = 0; i < edits.count; ++i)
    {
        const auto& edit = edits.list[i];
        const int index = paramIndex (edit.id);
        if (index < 0)
            continue;
        if (edit.press) // the sequencer's Inc, Dec and Reset, pressed on the page
        {
            if (edit.id >= ParamId::trigInc && edit.id <= ParamId::trigReset)
                p.commands.press ((int) (edit.id - ParamId::trigInc));
            continue;
        }
        setValue (p, index, edit.value, false);
        p.outgoing.fetch_or (1ull << index, std::memory_order_release);
        p.mainChanged.store (true, std::memory_order_release);
        changed = true;
    }
    if (changed && ! p.active)
        for (uint32_t i = 0; i < paramCount; ++i)
            p.engine.set (paramTable[i].id, p.values[i].load (std::memory_order_relaxed));
    if (changed && p.hostParams != nullptr && p.hostParams->request_flush != nullptr)
        p.hostParams->request_flush (p.host);
    // A trigger or the wheel through a sequence: the sound must run a block to take it, even asleep.
    if (p.commands.count() != commandsBefore && p.host->request_process != nullptr)
        p.host->request_process (p.host);
    if (used)
        invalidatePage (p);
    else if (p.hostScreen != nullptr) // nothing to draw, but the title wakes
        p.hostScreen->request_paint (p.host);
    return used;
}

void screenKnobs (const clap_plugin* plugin, clap_id ids[3])
{
    uint32_t chosen[3];
    of (plugin).screen.knobs (chosen);
    for (int k = 0; k < 3; ++k)
        ids[k] = chosen[k] == hdui::noParam ? CLAP_INVALID_ID : chosen[k];
}

const clap_plugin_melodee_screen screenExtension { screenPaint, screenInput, screenKnobs };

const void* pluginExtension (const clap_plugin*, const char* id)
{
    if (std::strcmp (id, CLAP_EXT_MELODEE_SCREEN) == 0) return &screenExtension;
    if (std::strcmp (id, CLAP_EXT_PARAMS) == 0) return &paramsExtension;
    if (std::strcmp (id, CLAP_EXT_STATE) == 0) return &stateExtension;
    if (std::strcmp (id, CLAP_EXT_AUDIO_PORTS) == 0) return &audioExtension;
    if (std::strcmp (id, CLAP_EXT_PRESET_LOAD) == 0 || std::strcmp (id, CLAP_EXT_PRESET_LOAD_COMPAT) == 0) return &presetExtension;
    return nullptr;
}

void pluginMainThread (const clap_plugin*) {}

// Factory -----------------------------------------------------------------------------------------------------

uint32_t factoryCount (const clap_plugin_factory*) { return 1; }
const clap_plugin_descriptor* factoryDescriptor (const clap_plugin_factory*, uint32_t index) { return index == 0 ? &descriptor : nullptr; }

const clap_plugin* factoryCreate (const clap_plugin_factory*, const clap_host* host, const char* id)
{
    if (! clap_version_is_compatible (host->clap_version) || std::strcmp (id, descriptor.id) != 0)
        return nullptr;
    void* memory = ::operator new (sizeof (Plugin), std::align_val_t (64), std::nothrow);
    if (memory == nullptr)
        return nullptr;
    auto* p = new (memory) Plugin();
    p->host = host;
    p->clap = { &descriptor, p, pluginInit, pluginDestroy, pluginActivate, pluginDeactivate, pluginStart, pluginStop,
                pluginReset, pluginProcess, pluginExtension, pluginMainThread };
    return &p->clap;
}

const clap_plugin_factory factory { factoryCount, factoryDescriptor, factoryCreate };

// Preset discovery: one provider, one location inside the plug-in, the presets of presets.h.
const clap_preset_discovery_provider_descriptor presetProviderDescriptor { CLAP_VERSION, "com.hdslave.cortex-filter.presets",
                                                                           "hdslave Cortex Filter presets", "hdslave" };

bool providerInit (const clap_preset_discovery_provider* provider)
{
    const auto* indexer = static_cast<const clap_preset_discovery_indexer*> (provider->provider_data);
    const clap_preset_discovery_location location { CLAP_PRESET_DISCOVERY_IS_FACTORY_CONTENT, "Factory",
                                                    CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN, nullptr };
    return indexer->declare_location (indexer, &location);
}

void providerDestroy (const clap_preset_discovery_provider* provider) { delete provider; }

bool providerMetadata (const clap_preset_discovery_provider*, uint32_t kind, const char*, const clap_preset_discovery_metadata_receiver* r)
{
    if (kind != CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN)
        return false;
    const clap_universal_plugin_id plugin { "clap", descriptor.id };
    for (const auto& preset : presetTable)
    {
        if (! r->begin_preset (r, preset.name, preset.key))
            break;
        r->add_plugin_id (r, &plugin);
        r->set_flags (r, CLAP_PRESET_DISCOVERY_IS_FACTORY_CONTENT);
        r->add_creator (r, "hdslave");
    }
    return true;
}

const void* providerExtension (const clap_preset_discovery_provider*, const char*) { return nullptr; }

uint32_t presetFactoryCount (const clap_preset_discovery_factory*) { return 1; }
const clap_preset_discovery_provider_descriptor* presetFactoryDescriptor (const clap_preset_discovery_factory*, uint32_t index)
{
    return index == 0 ? &presetProviderDescriptor : nullptr;
}
const clap_preset_discovery_provider* presetFactoryCreate (const clap_preset_discovery_factory*, const clap_preset_discovery_indexer* indexer,
                                                           const char* id)
{
    if (indexer == nullptr || id == nullptr || std::strcmp (id, presetProviderDescriptor.id) != 0)
        return nullptr;
    return new (std::nothrow) clap_preset_discovery_provider { &presetProviderDescriptor, const_cast<clap_preset_discovery_indexer*> (indexer),
                                                               providerInit, providerDestroy, providerMetadata, providerExtension };
}

const clap_preset_discovery_factory presetFactory { presetFactoryCount, presetFactoryDescriptor, presetFactoryCreate };

bool entryInit (const char*) { return true; }
void entryDeinit() {}
const void* entryFactory (const char* id)
{
    if (std::strcmp (id, CLAP_PLUGIN_FACTORY_ID) == 0) return &factory;
    if (std::strcmp (id, CLAP_PRESET_DISCOVERY_FACTORY_ID) == 0 || std::strcmp (id, CLAP_PRESET_DISCOVERY_FACTORY_ID_COMPAT) == 0)
        return &presetFactory;
    return nullptr;
}
} // namespace
} // namespace cortex

extern "C" CLAP_EXPORT const clap_plugin_entry clap_entry { CLAP_VERSION, cortex::entryInit, cortex::entryDeinit, cortex::entryFactory };
