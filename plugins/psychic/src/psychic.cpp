// Psychic: a generative analog-style synthesizer as a CLAP plug-in, made for the Akai MPC Sample.
// This file is the CLAP side: the factory, ports, parameters, state, presets and the process call, which cuts the
// block at each event so every change lands on its frame. The sound and the sequencers are in engine.h.
#include "engine.h"
#include "params.h"
#include "presets.h"
#include "screen.h"

#include <clap/clap.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <new>
#include <sched.h>

namespace psychic
{
namespace
{
// Stereo: its one output is a stereo port (the same signal both sides).
const char* const features[] = { CLAP_PLUGIN_FEATURE_INSTRUMENT, CLAP_PLUGIN_FEATURE_SYNTHESIZER, CLAP_PLUGIN_FEATURE_STEREO, nullptr };

const clap_plugin_descriptor descriptor {
    CLAP_VERSION, "com.hdslave.psychic", "hdslave Psychic", "hdslave", "", "", "", PSYCHIC_VERSION,
    "A sine VCO and a triangle Mod VCO with through-zero FM, a wavefolder and a state-variable filter, played by two "
    "corruptible 8-bit sequencers in time with the host",
    features,
};

struct Plugin
{
    clap_plugin clap {};
    const clap_host* host = nullptr;
    // Every parameter's value. The audio thread writes it from events and from the engine's own changes; the main
    // thread reads it, and writes all of them when a state or a preset is loaded. The engine takes those at the
    // next block.
    std::atomic<float> values[paramCount];
    std::atomic<bool> mainChanged { false }; // the main thread loaded values: give them to the engine
    // The buffer (Save and Recall), mirrored for the state: written by the audio thread when Save is pressed, by the
    // main thread when a state is loaded.
    std::atomic<float> buffer[seqStateCount];
    std::atomic<bool> bufferFull { false };
    std::atomic<bool> bufferLoaded { false }; // the main thread loaded a buffer: give it to the engine
    // Who is writing several of these at once. The main thread waits for it (loading, saving); the audio thread only
    // tries, and if the main thread has it, what it would write waits for a later block. `loads` counts the main
    // thread's loads: a load that lands while a block plays wins over what the engine changed in that block.
    std::atomic<bool> exchange { false };
    std::atomic<uint32_t> loads { 0 };
    const clap_host_melodee_screen* hostScreen = nullptr;
    const clap_host_params* hostParams = nullptr;
    std::atomic<uint32_t> bufferRevision { 0 };
    uint32_t paintedRevision = 0;
    ScreenFeed feed;
    Screen screen { values, feed };
    int64_t lastHit[2] { -1, -1 };
    bool audioValuesMoved = false;

    // Preserve the order of Save, Recall and individual edits even if several arrive in one block.
    // The exchange protects this queue. The audio side never waits on the main thread.
    struct Command { Screen::Edit edit; uint64_t time; };
    static constexpr unsigned pageCapacity = 128;
    Command commands[pageCapacity] {};
    unsigned written = 0, taken = 0;
    struct PageValue { unsigned queued = 0; float tellValue = 0; bool tell = false; };
    PageValue edits[paramCount] {};
    std::atomic<bool> pageWaiting { false };
    Engine engine;
    bool active = false;
};

Plugin& of (const clap_plugin* p) { return *static_cast<Plugin*> (p->plugin_data); }

void lockMain (Plugin& p)
{
    for (bool free = false; ! p.exchange.compare_exchange_weak (free, true, std::memory_order_acquire); free = false)
        sched_yield();
}
bool tryLockAudio (Plugin& p)
{
    bool free = false;
    return p.exchange.compare_exchange_strong (free, true, std::memory_order_acquire);
}
void unlock (Plugin& p) { p.exchange.store (false, std::memory_order_release); }

void requestPaint (Plugin& p)
{
    if (p.hostScreen && p.hostScreen->request_paint) p.hostScreen->request_paint (p.host);
}

uint64_t steadyMs()
{
    return (uint64_t) std::chrono::duration_cast<std::chrono::milliseconds> (std::chrono::steady_clock::now().time_since_epoch()).count();
}

// Publish clock positions and actual hits once per audio block. The page never reads the engine.
void publishScreen (Plugin& p)
{
    if (!p.hostScreen) return;
    uint32_t heads = 0;
    bool moved = p.audioValuesMoved;
    p.audioValuesMoved = false;
    for (int s = 0; s < 2; ++s)
    {
        heads |= (uint32_t) p.engine.screenCell (s,false) << (s*10);
        heads |= (uint32_t) p.engine.screenCell (s,true) << (s*10+5);
        const auto hit = p.engine.screenHitFrame (s);
        if (hit >= 0 && hit != p.lastHit[s])
        {
            p.feed.hits[s].fetch_add (1, std::memory_order_release);
            moved = true;
        }
        p.lastHit[s] = hit;
    }
    if (heads != p.feed.heads.load (std::memory_order_relaxed))
    {
        p.feed.heads.store (heads, std::memory_order_release);
        moved = true;
    }
    if (moved) requestPaint (p);
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
                | (p.kind == Kind::choice || p.kind == Kind::button ? (uint32_t) CLAP_PARAM_IS_ENUM : 0u);
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
    const auto& p = of (plugin);
    *value = p.feed.value (p.values, index);
    return true;
}

bool paramsToText (const clap_plugin*, clap_id id, double value, char* text, uint32_t size)
{
    const int index = paramIndex (id);
    if (index < 0 || size == 0)
        return false;
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

// A value the plug-in set itself (a button's release, the engine's own change): the host hears it, and does not
// record it as automation (which would then play the dice back over the sequence).
bool pushValue (const clap_output_events* out, uint32_t time, clap_id id, double value, uint32_t flags = CLAP_EVENT_DONT_RECORD)
{
    if (out == nullptr)
        return false;
    clap_event_param_value back {};
    back.header = { sizeof (back), time, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_PARAM_VALUE, flags };
    back.param_id = id;
    back.note_id = back.port_index = back.channel = back.key = -1;
    back.value = value;
    return out->try_push (out, &back.header);
}

// One event: parameter values go to the engine at once; notes transpose (and, stopped, trigger) it. What it sends
// back goes at the event's time, or the block's last frame for an event stamped past the block.
void handle (Plugin& p, const clap_event_header* e, const clap_output_events* out, uint32_t lastFrame)
{
    if (e->space_id != CLAP_CORE_EVENT_SPACE_ID)
        return;
    switch (e->type)
    {
        case CLAP_EVENT_PARAM_VALUE:
        {
            const auto* v = reinterpret_cast<const clap_event_param_value*> (e);
            const int index = paramIndex (v->param_id);
            if (index < 0)
                break;
            const auto& info = paramTable[index];
            const float value = clampParam (info, (float) v->value);
            if (info.kind == Kind::button)
            {
                // A press acts once; the button then reports itself released.
                if (value >= 0.5f)
                {
                    p.engine.press (info.id);
                    pushValue (out, std::min (e->time, lastFrame), info.id, 0.0);
                }
                p.values[index].store (0.0f, std::memory_order_relaxed);
                break;
            }
            p.audioValuesMoved = true;
            p.values[index].store (value, std::memory_order_relaxed);
            p.engine.set (info.id, value);
            break;
        }
        case CLAP_EVENT_NOTE_ON:
        {
            const auto* n = reinterpret_cast<const clap_event_note*> (e);
            if (n->velocity > 0.0 && n->key >= 0 && n->key < 128)
                p.engine.note (n->key, (float) n->velocity);
            break;
        }
        case CLAP_EVENT_MIDI:
        {
            const auto* m = reinterpret_cast<const clap_event_midi*> (e);
            if ((m->data[0] & 0xf0) == 0x90 && m->data[2] > 0)
                p.engine.note (m->data[1] & 0x7f, (float) (m->data[2] & 0x7f) / 127.0f);
            break;
        }
        default: break;
    }
}

// Flush can deliver values even while stopped. A press waits for process and its transport,
// and later edits wait behind it. Keep the last undelivered value if the host's output queue fills.
void takePageCommands (Plugin& p, const clap_output_events* out, bool allowPress)
{
    if (!p.pageWaiting.load (std::memory_order_acquire) || !tryLockAudio (p)) return;
    uint64_t now = 0;
    while (p.taken != p.written)
    {
        const auto& command = p.commands[p.taken % Plugin::pageCapacity];
        const auto& edit = command.edit;
        if (edit.press)
        {
            if (!allowPress) break;
            if (!now) now = steadyMs();
            if (now - command.time <= 500)
            {
                clap_event_param_value press {};
                press.header = {sizeof (press), 0, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_PARAM_VALUE, 0};
                press.param_id = edit.id;
                press.value = 1;
                handle (p, &press.header, out, 0);
            }
        }
        else
        {
            const int i = paramIndex (edit.id);
            auto& pending = p.edits[i];
            p.engine.set (edit.id, edit.value);
            p.values[i].store (edit.value, std::memory_order_relaxed);
            if (--pending.queued == 0) p.feed.preview[i].store (ScreenFeed::noEdit, std::memory_order_release);
            pending.tellValue = edit.value;
            pending.tell = !pushValue (out, 0, edit.id, edit.value, 0);
        }
        ++p.taken;
    }
    bool waiting = p.taken != p.written;
    for (uint32_t i = 0; i < paramCount; ++i)
    {
        auto& pending = p.edits[i];
        if (pending.tell) pending.tell = !pushValue (out, 0, paramTable[i].id, pending.tellValue, 0);
        waiting |= pending.tell;
    }
    p.pageWaiting.store (waiting, std::memory_order_release);
    unlock (p);
}

// What the engine changed itself (Bit Flip, Bit Shift, Corrupt, Recall), as the parameters' values the host hears at
// `time`, and a buffer Save wrote. If the main thread has the exchange, they wait for a later block (the engine keeps
// them). If it loaded values since `since` (the loads count when the block began), those stand: the engine's changes
// are dropped, and the next block gives the engine the loaded values again.
void publish (Plugin& p, const clap_output_events* out, uint32_t time, uint32_t since)
{
    if (! tryLockAudio (p))
        return;
    const bool loaded = p.loads.load (std::memory_order_relaxed) != since;
    uint32_t id = 0;
    float value = 0.0f;
    while (p.engine.takeChange (id, value))
        if (! loaded)
        {
            p.audioValuesMoved = true;
            p.values[paramIndex (id)].store (value, std::memory_order_relaxed);
            pushValue (out, time, id, value);
        }
    if (loaded)
        p.mainChanged.store (true, std::memory_order_release);
    if (p.engine.takeBufferSaved() && ! p.bufferLoaded.load (std::memory_order_acquire))
    {
        const float* saved = p.engine.bufferValues();
        for (uint32_t i = 0; i < seqStateCount; ++i)
            p.buffer[i].store (saved[i], std::memory_order_relaxed);
        p.bufferFull.store (true, std::memory_order_relaxed);
        p.bufferRevision.fetch_add (1, std::memory_order_release);
        p.audioValuesMoved = true;
    }
    unlock (p);
}

// Outside process (never at the same time as it): on the audio thread while active, on the main thread while not, when
// the engine is not running either. Events act as they do in process, buttons too, all at once.
void paramsFlush (const clap_plugin* plugin, const clap_input_events* in, const clap_output_events* out)
{
    auto& p = of (plugin);
    const uint32_t since = p.loads.load (std::memory_order_acquire);
    takePageCommands (p, out, false);
    const uint32_t count = in ? in->size (in) : 0;
    for (uint32_t i = 0; i < count; ++i)
        handle (p, in->get (in, i), out, 0);
    publish (p, out, 0, since);
    publishScreen (p);
}

const clap_plugin_params paramsExtension { paramsCount, paramsInfo, paramsValue, paramsToText, paramsFromText, paramsFlush };

// State: "PSYC", a version, the number of values, then (id, value) pairs; unknown ids are skipped, missing ones keep
// their defaults, so a state saved by another version still loads. Then the buffer: "BUFR", whether it holds
// anything, its count and its (id, value) pairs (a state without it loads with the buffer empty).
constexpr char stateTag[4] = { 'P', 'S', 'Y', 'C' };
constexpr char bufferTag[4] = { 'B', 'U', 'F', 'R' };
constexpr uint32_t stateVersion = 1;

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

// Every value at once, and the buffer if `buffer` is given, from the main thread (a state or a preset loaded). Not
// processing, the engine takes them now; otherwise at the next block.
void loadAll (Plugin& p, const float* values, const float* buffer = nullptr, bool full = false)
{
    lockMain (p);
    for (uint32_t i = 0; i < paramCount; ++i)
    {
        p.values[i].store (values[i], std::memory_order_relaxed);
        p.edits[i] = {};
        p.feed.preview[i].store (ScreenFeed::noEdit, std::memory_order_release);
    }
    p.taken = p.written;
    p.pageWaiting.store (false, std::memory_order_release);
    if (buffer != nullptr)
    {
        for (uint32_t i = 0; i < seqStateCount; ++i)
            p.buffer[i].store (buffer[i], std::memory_order_relaxed);
        p.bufferFull.store (full, std::memory_order_relaxed);
        p.bufferRevision.fetch_add (1, std::memory_order_release);
        p.bufferLoaded.store (true, std::memory_order_release);
    }
    p.loads.fetch_add (1, std::memory_order_release);
    p.mainChanged.store (true, std::memory_order_release);
    unlock (p);
    if (! p.active)
    {
        for (uint32_t i = 0; i < paramCount; ++i)
            if (paramTable[i].kind != Kind::button)
                p.engine.set (paramTable[i].id, values[i]);
        if (buffer != nullptr)
            p.engine.setBuffer (full, buffer);
        publishScreen (p);
    }
    requestPaint (p);
}

bool stateSave (const clap_plugin* plugin, const clap_ostream* stream)
{
    auto& p = of (plugin);
    struct Pair { uint32_t id; float value; };
    // Copied holding the exchange, so a change the engine makes to several values at once (a Bit Shift) is saved
    // whole or not at all.
    Pair values[paramCount], buffer[seqStateCount];
    lockMain (p);
    for (uint32_t i = 0; i < paramCount; ++i)
        values[i] = { paramTable[i].id, p.feed.value (p.values, i) };
    for (uint32_t i = 0; i < seqStateCount; ++i)
        buffer[i] = { seqStateIds[i], p.buffer[i].load (std::memory_order_relaxed) };
    const uint32_t buffered[2] = { p.bufferFull.load (std::memory_order_relaxed) ? 1u : 0u, seqStateCount };
    unlock (p);
    const uint32_t header[2] = { stateVersion, paramCount };
    return writeAll (stream, stateTag, 4) && writeAll (stream, header, sizeof (header)) && writeAll (stream, values, sizeof (values))
        && writeAll (stream, bufferTag, 4) && writeAll (stream, buffered, sizeof (buffered)) && writeAll (stream, buffer, sizeof (buffer));
}

bool stateLoad (const clap_plugin* plugin, const clap_istream* stream)
{
    auto& p = of (plugin);
    struct Pair { uint32_t id; float value; };
    char tag[4] = {};
    uint32_t header[2] = {};
    if (! readAll (stream, tag, 4) || std::memcmp (tag, stateTag, 4) != 0 || ! readAll (stream, header, sizeof (header))
        || header[0] == 0 || header[1] > 4096)
        return false;
    float loaded[paramCount];
    for (uint32_t i = 0; i < paramCount; ++i)
        loaded[i] = paramTable[i].def;
    for (uint32_t i = 0; i < header[1]; ++i)
    {
        Pair pair {};
        if (! readAll (stream, &pair, sizeof (pair)))
            return false;
        const int index = paramIndex (pair.id);
        if (index >= 0 && paramTable[index].kind != Kind::button)
            loaded[index] = clampParam (paramTable[index], pair.value);
    }
    // The buffer, if the state has one: its values start as the loaded sequence's, so an id it lacks is that.
    float buffered[seqStateCount];
    for (uint32_t i = 0; i < seqStateCount; ++i)
        buffered[i] = loaded[paramIndex (seqStateIds[i])];
    bool full = false;
    uint32_t bufferHeader[2] = {};
    if (readAll (stream, tag, 4) && std::memcmp (tag, bufferTag, 4) == 0 && readAll (stream, bufferHeader, sizeof (bufferHeader))
        && bufferHeader[1] <= 4096)
    {
        full = bufferHeader[0] != 0;
        for (uint32_t i = 0; i < bufferHeader[1]; ++i)
        {
            Pair pair {};
            if (! readAll (stream, &pair, sizeof (pair)))
                return false;
            const int at = seqStateIndex (pair.id);
            if (at >= 0)
                buffered[at] = clampParam (paramTable[paramIndex (pair.id)], pair.value);
        }
    }
    loadAll (p, loaded, buffered, full);
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
    loadAll (p, values);
    if (const auto* hostParams = static_cast<const clap_host_params*> (p.host->get_extension (p.host, CLAP_EXT_PARAMS)))
        if (hostParams->rescan != nullptr)
            hostParams->rescan (p.host, CLAP_PARAM_RESCAN_VALUES);
    if (hostPresets != nullptr && hostPresets->loaded != nullptr)
        hostPresets->loaded (p.host, kind, nullptr, key);
    return true;
}

const clap_plugin_preset_load presetExtension { presetFromLocation };

// Ports: no audio in, one stereo out (the same signal both sides: Melodee plays a mono port on the left only), one
// note input for transposing and playing by hand.
uint32_t audioCount (const clap_plugin*, bool input) { return input ? 0 : 1; }
bool audioGet (const clap_plugin*, uint32_t index, bool input, clap_audio_port_info* info)
{
    if (input || index != 0)
        return false;
    *info = {};
    info->id = 0;
    std::snprintf (info->name, sizeof (info->name), "Out");
    info->flags = CLAP_AUDIO_PORT_IS_MAIN;
    info->channel_count = 2;
    info->port_type = CLAP_PORT_STEREO;
    info->in_place_pair = CLAP_INVALID_ID;
    return true;
}
const clap_plugin_audio_ports audioExtension { audioCount, audioGet };

uint32_t noteCount (const clap_plugin*, bool input) { return input ? 1 : 0; }
bool noteGet (const clap_plugin*, uint32_t index, bool input, clap_note_port_info* info)
{
    if (! input || index != 0)
        return false;
    *info = {};
    info->id = 0;
    info->supported_dialects = CLAP_NOTE_DIALECT_CLAP | CLAP_NOTE_DIALECT_MIDI;
    info->preferred_dialect = CLAP_NOTE_DIALECT_CLAP;
    std::snprintf (info->name, sizeof (info->name), "Notes");
    return true;
}
const clap_plugin_note_ports noteExtension { noteCount, noteGet };

// The plug-in -------------------------------------------------------------------------------------------------

bool pluginInit (const clap_plugin* plugin)
{
    auto& p = of (plugin);
    p.hostScreen = static_cast<const clap_host_melodee_screen*> (p.host->get_extension (p.host, CLAP_EXT_MELODEE_SCREEN_HOST));
    p.hostParams = static_cast<const clap_host_params*> (p.host->get_extension (p.host, CLAP_EXT_PARAMS));
    for (uint32_t i = 0; i < paramCount; ++i)
    {
        p.feed.preview[i].store (ScreenFeed::noEdit, std::memory_order_relaxed);
        p.values[i].store (paramTable[i].def, std::memory_order_relaxed);
        p.engine.set (paramTable[i].id, paramTable[i].def);
    }
    for (uint32_t i = 0; i < seqStateCount; ++i)
        p.buffer[i].store (paramTable[paramIndex (seqStateIds[i])].def, std::memory_order_relaxed);
    return true;
}

void pluginDestroy (const clap_plugin* plugin)
{
    auto* p = &of (plugin);
    p->~Plugin();
    ::operator delete (p, std::align_val_t (64));
}

bool pluginActivate (const clap_plugin* plugin, double rate, uint32_t, uint32_t)
{
    auto& p = of (plugin);
    p.engine.prepare (rate);
    p.active = true;
    return true;
}

void pluginDeactivate (const clap_plugin* plugin) { of (plugin).active = false; }
bool pluginStart (const clap_plugin*) { return true; }
void pluginStop (const clap_plugin*) {}
void pluginReset (const clap_plugin* plugin)
{
    auto& p = of (plugin);
    p.engine.reset();
    publishScreen (p);
}

// Denormals: flush them to zero for the block (the filter and envelopes decay into them), as the FPU had it after.
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
    if (process->audio_outputs_count < 1 || process->audio_outputs[0].channel_count < 1)
        return CLAP_PROCESS_ERROR;
    float* left = process->audio_outputs[0].data32[0];
    float* right = process->audio_outputs[0].channel_count > 1 ? process->audio_outputs[0].data32[1] : nullptr;

    // What the main thread loaded (a state or a preset, while playing): the engine takes each value it does not have.
    const uint32_t since = p.loads.load (std::memory_order_acquire);
    if (p.mainChanged.exchange (false, std::memory_order_acquire))
        for (uint32_t i = 0; i < paramCount; ++i)
            if (paramTable[i].kind != Kind::button)
                p.engine.set (paramTable[i].id, p.values[i].load (std::memory_order_relaxed));
    if (p.bufferLoaded.exchange (false, std::memory_order_acquire))
    {
        float values[seqStateCount];
        for (uint32_t i = 0; i < seqStateCount; ++i)
            values[i] = p.buffer[i].load (std::memory_order_relaxed);
        p.engine.setBuffer (p.bufferFull.load (std::memory_order_relaxed), values);
    }

    takePageCommands (p, process->out_events, false);

    // Settings at the block's first frame come first, the transport after them: a setting the host sends with Play
    // is in place for what Play does. Buttons and notes at that frame come after the transport: they act on where
    // the sequences are once it moved (a Reset with Play sticks; a note with Play transposes without playing).
    const auto* in = process->in_events;
    const uint32_t count = in != nullptr ? in->size (in) : 0;
    const uint32_t last = frames > 0 ? frames - 1 : 0;
    auto setting = [] (const clap_event_header* e) {
        if (e->space_id != CLAP_CORE_EVENT_SPACE_ID || e->type != CLAP_EVENT_PARAM_VALUE)
            return false;
        const int index = paramIndex (reinterpret_cast<const clap_event_param_value*> (e)->param_id);
        return index >= 0 && paramTable[index].kind != Kind::button;
    };
    for (uint32_t i = 0; i < count && in->get (in, i)->time == 0; ++i)
        if (setting (in->get (in, i)))
            handle (p, in->get (in, i), process->out_events, last);

    // The host's transport. Without a song position (Melodee always sends one), the sequences count their own from
    // Play, so the first step is the one Play lands on. A tempo past 1000 BPM is taken as 1000 (a step must still be
    // at least a frame or so long).
    if (const auto* t = process->transport)
    {
        const bool playing = (t->flags & CLAP_TRANSPORT_IS_PLAYING) != 0;
        const bool placed = (t->flags & CLAP_TRANSPORT_HAS_BEATS_TIMELINE) != 0;
        const double bpm = (t->flags & CLAP_TRANSPORT_HAS_TEMPO) && t->tempo > 0.0 ? std::min (std::max (t->tempo, 1.0), 1000.0) : 120.0;
        const double beats = placed ? (double) t->song_pos_beats / (double) CLAP_BEATTIME_FACTOR : p.engine.ownPosition();
        if (p.hostScreen) p.feed.setTransport (beats, bpm);
        p.engine.transport (playing, beats, bpm, frames);
    }
    else
        p.engine.transport (false, 0.0, 120.0, frames);

    takePageCommands (p, process->out_events, true);
    uint32_t next = 0, done = 0;
    while (done < frames)
    {
        // Every event due by now (but the first frame's settings, already in), then sound up to the next one.
        while (next < count)
        {
            const auto* e = in->get (in, next);
            if (e->time > done)
                break;
            if (e->time != 0 || ! setting (e))
                handle (p, e, process->out_events, last);
            ++next;
        }
        uint32_t until = frames;
        if (next < count)
            until = std::min<uint32_t> (frames, std::max<uint32_t> (in->get (in, next)->time, done + 1));
        p.engine.render (left + done, right ? right + done : nullptr, until - done);
        done = until;
    }
    for (; next < count; ++next) // events stamped past the block (a host's mistake): applied at its end
        if (in->get (in, next)->time != 0 || ! setting (in->get (in, next)))
            handle (p, in->get (in, next), process->out_events, last);

    // What the engine changed itself, heard by the host at the block's end.
    publish (p, process->out_events, last, since);
    publishScreen (p);
    // Never "sleep": the sequencers must hear the next Play, which is no event. (Melodee puts an idle instrument to
    // sleep itself, and the engine skips its own work while the VCAs are closed.)
    return CLAP_PROCESS_CONTINUE;
}

bool screenPaint (const clap_plugin* plugin, const melodee_screen_canvas_t* c)
{
    if (!c || !c->pixels || c->width != hdui::width || c->height != hdui::height || c->stride < c->width) return false;
    auto& p = of (plugin);
    if (p.paintedRevision != p.bufferRevision.load (std::memory_order_acquire))
    {
        std::array<float, seqStateCount> saved;
        lockMain (p);
        for (uint32_t i = 0; i < seqStateCount; ++i) saved[i] = p.buffer[i].load (std::memory_order_relaxed);
        const bool full = p.bufferFull.load (std::memory_order_relaxed);
        p.paintedRevision = p.bufferRevision.load (std::memory_order_relaxed);
        unlock (p);
        p.screen.setMemory (saved, full);
    }
    const bool drew = p.screen.paint (*c);
    if (p.screen.animating()) requestPaint (p);
    return drew;
}

bool screenInput (const clap_plugin* plugin, const melodee_screen_input_t* input)
{
    if (!input || input->kind > MELODEE_SCREEN_NUDGE) return false;
    auto& p = of (plugin);
    Screen::Edit edit;
    if (!p.screen.input (*input, edit)) return false;
    if (edit.id != CLAP_INVALID_ID)
    {
        lockMain (p);
        if (p.written - p.taken == Plugin::pageCapacity)
        {
            unlock (p);
            return false;
        }
        p.commands[p.written++ % Plugin::pageCapacity] = {edit, edit.press ? steadyMs() : 0};
        if (!edit.press)
        {
            const int i = paramIndex (edit.id);
            p.feed.preview[i].store (edit.value, std::memory_order_release);
            ++p.edits[i].queued;
        }
        p.pageWaiting.store (true, std::memory_order_release);
        unlock (p);
        if (!edit.press)
        {
            if (p.hostParams && p.hostParams->rescan) p.hostParams->rescan (p.host, CLAP_PARAM_RESCAN_VALUES);
            if (p.hostParams && p.hostParams->request_flush) p.hostParams->request_flush (p.host);
        }
        if (p.host->request_process) p.host->request_process (p.host);
    }
    requestPaint (p);
    return true;
}

void screenKnobs (const clap_plugin* plugin, clap_id ids[3]) { of (plugin).screen.knobs (ids); }
const clap_plugin_melodee_screen screenExtension { screenPaint, screenInput, screenKnobs };

const void* pluginExtension (const clap_plugin*, const char* id)
{
    if (std::strcmp (id, CLAP_EXT_MELODEE_SCREEN) == 0) return &screenExtension;
    if (std::strcmp (id, CLAP_EXT_PARAMS) == 0) return &paramsExtension;
    if (std::strcmp (id, CLAP_EXT_STATE) == 0) return &stateExtension;
    if (std::strcmp (id, CLAP_EXT_AUDIO_PORTS) == 0) return &audioExtension;
    if (std::strcmp (id, CLAP_EXT_NOTE_PORTS) == 0) return &noteExtension;
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
const clap_preset_discovery_provider_descriptor presetProviderDescriptor { CLAP_VERSION, "com.hdslave.psychic.presets",
                                                                           "hdslave Psychic presets", "hdslave" };

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
    // provider_data holds the indexer, which init() declares the location to.
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
} // namespace psychic

extern "C" CLAP_EXPORT const clap_plugin_entry clap_entry { CLAP_VERSION, psychic::entryInit, psychic::entryDeinit, psychic::entryFactory };
