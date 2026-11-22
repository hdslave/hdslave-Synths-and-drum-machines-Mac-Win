// Anvil: an 8-step analog-style percussion synthesizer as a CLAP plug-in, made for the Akai MPC Sample.
// This file is the CLAP side: the factory, ports, parameters, state, presets and the process call, which cuts the
// block at each event so every change lands on its frame. The sound and the sequencer are in engine.h.
#include "engine.h"
#include "params.h"
#include "presets.h"
#include "screen.h"
#include "melodee_screen.h"

#include <clap/clap.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <new>

namespace anvil
{
namespace
{
const char* const features[] = { CLAP_PLUGIN_FEATURE_INSTRUMENT, CLAP_PLUGIN_FEATURE_SYNTHESIZER, CLAP_PLUGIN_FEATURE_DRUM,
                                 CLAP_PLUGIN_FEATURE_STEREO, nullptr };

const clap_plugin_descriptor descriptor {
    CLAP_VERSION, "com.hdslave.anvil", "hdslave Anvil", "hdslave", "", "", "", ANVIL_VERSION,
    "Two VCOs with FM and sync, noise, a ladder filter and three decay envelopes, played by its own 8-step sequencer "
    "in time with the host",
    features,
};

struct Plugin
{
    clap_plugin clap {};
    const clap_host* host = nullptr;
    // Every parameter's value. The audio thread writes it from events; the main thread reads it, and writes it when
    // a state is loaded. The engine notices those at the next block.
    std::atomic<float> values[paramCount];
    std::atomic<bool> mainChanged { false }; // the main thread wrote values (a state, a preset, a flush): look at them
    float applied[paramCount] {};
    Engine engine;
    bool active = false;
    // The steps' Pitch texts read by Quantize: when it moves, the host is asked (from the main thread) to read the
    // texts again.
    std::atomic<bool> textStale { false };
    const clap_host_params* hostParams = nullptr;
    // Melodee's screen (melodee_screen.h): the page, what the audio thread tells it, and what it changed. Values the
    // page sets are told to the host from the next process or flush (outgoing, a bit a row); buttons it presses act at
    // the next block (presses), as does a step it plays (audition, -1 none).
    const clap_host_melodee_screen* hostScreen = nullptr;
    StepState stepState;
    Screen screen { values, stepState };
    std::atomic<uint64_t> outgoing { 0 }, presses { 0 };
    std::atomic<int> audition { -1 };
    std::atomic<uint64_t> pressedAt { 0 }; // when the page last pressed (steady ms): a press kept waiting is dropped
    bool valuesMoved = false; // the host moved a value in this block (audio thread): the page is painted again
    int shownStep = -1;
    uint32_t shownHits = 0;
    bool shownRunning = false;
    // The page animates while the song plays, and for screenRestMs after it is opened, touched or a pot moves; left
    // alone while stopped it rests (asks for no next paint), and Play, a hit, a value or any input wakes it. Main
    // thread only (paint and input), like the page itself.
    uint64_t screenBusyMs = 0;
    bool screenTouched = true;
    int16_t shownPots[3] { -2, -2, -2 };
};
constexpr uint64_t screenRestMs = 4000;

Plugin& of (const clap_plugin* p) { return *static_cast<Plugin*> (p->plugin_data); }

// A steady clock in ms, for the age of the page's presses.
uint64_t steadyMs()
{
    return (uint64_t) std::chrono::duration_cast<std::chrono::milliseconds> (std::chrono::steady_clock::now().time_since_epoch()).count();
}
// A press on the page acts at the next block. A host may not run a plug-in for a while (Melodee puts a silent
// instrument to sleep, and nothing but MIDI wakes it): a press older than this is dropped rather than played late.
constexpr uint64_t pressFreshMs = 500;

TextContext contextOf (const Plugin& p)
{
    TextContext c;
    c.quantize = p.values[paramIndex (ParamId::quantize)].load (std::memory_order_relaxed);
    return c;
}

// A value that another's text depends on moved (any thread): the texts are read again from the main thread.
void textMoved (Plugin& p, uint32_t id)
{
    if (id != ParamId::quantize)
        return;
    if (! p.textStale.exchange (true, std::memory_order_acq_rel) && p.host != nullptr && p.host->request_callback != nullptr)
        p.host->request_callback (p.host); // once until the main thread has read them again
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
    *value = of (plugin).values[index].load (std::memory_order_relaxed);
    return true;
}

bool paramsToText (const clap_plugin* plugin, clap_id id, double value, char* text, uint32_t size)
{
    const int index = paramIndex (id);
    if (index < 0 || size == 0)
        return false;
    formatParam (paramTable[index], (float) value, text, size, contextOf (of (plugin)));
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

// One event: parameter values go to the engine at once; notes trigger it. A button's release is sent back stamped no
// later than `lastFrame` (an event stamped past the block is applied at its end).
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
                    if (out != nullptr)
                    {
                        clap_event_param_value back {};
                        back.header = { sizeof (back), std::min (e->time, lastFrame), CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_PARAM_VALUE, 0 };
                        back.param_id = info.id;
                        back.note_id = back.port_index = back.channel = back.key = -1;
                        back.value = 0.0;
                        out->try_push (out, &back.header);
                    }
                }
                p.values[index].store (0.0f, std::memory_order_relaxed);
                p.applied[index] = 0.0f;
                break;
            }
            const bool moved = p.applied[index] != value;
            p.values[index].store (value, std::memory_order_relaxed);
            p.applied[index] = value;
            p.engine.set (info.id, value);
            if (moved)
                textMoved (p, info.id), p.valuesMoved = true;
            break;
        }
        case CLAP_EVENT_NOTE_ON:
        {
            // Notes: Trigger plays the step the sequence is on; Notes: Steps the step the key names.
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

void paramsFlush (const clap_plugin* plugin, const clap_input_events* in, const clap_output_events* out)
{
    auto& p = of (plugin);
    const uint32_t count = in->size (in);
    for (uint32_t i = 0; i < count; ++i)
    {
        const auto* e = in->get (in, i);
        // Active, a flush comes on the audio thread in place of a process call: the engine takes it as it would
        // there (buttons too).
        if (p.active)
        {
            if (e->space_id == CLAP_CORE_EVENT_SPACE_ID && e->type == CLAP_EVENT_PARAM_VALUE)
            {
                handle (p, e, out, 0);
                if (p.hostScreen != nullptr)
                    p.hostScreen->request_paint (p.host);
            }
            continue;
        }
        // Not active, the engine is not running: only values are kept (buttons do nothing).
        if (e->space_id == CLAP_CORE_EVENT_SPACE_ID && e->type == CLAP_EVENT_PARAM_VALUE)
        {
            const auto* v = reinterpret_cast<const clap_event_param_value*> (e);
            const int index = paramIndex (v->param_id);
            if (index >= 0 && paramTable[index].kind != Kind::button)
            {
                p.values[index].store (clampParam (paramTable[index], (float) v->value), std::memory_order_relaxed);
                p.mainChanged.store (true, std::memory_order_release);
                textMoved (p, v->param_id);
                if (p.hostScreen != nullptr)
                    p.hostScreen->request_paint (p.host);
            }
        }
    }
    tellHost (p, out, 0);
}

const clap_plugin_params paramsExtension { paramsCount, paramsInfo, paramsValue, paramsToText, paramsFromText, paramsFlush };

// State: "ANVL", a version, the number of values, then (id, value) pairs; unknown ids are skipped, missing ones keep
// their defaults, so a state saved by another version still loads.
constexpr char stateTag[4] = { 'A', 'N', 'V', 'L' };
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

// Every value at once, from the main thread (a state or a preset loaded).
void setAll (Plugin& p, const float* values)
{
    for (uint32_t i = 0; i < paramCount; ++i)
        p.values[i].store (values[i], std::memory_order_relaxed);
    p.mainChanged.store (true, std::memory_order_release);
    if (! p.active) // not processing: the engine takes them now; otherwise at the next block
        for (uint32_t i = 0; i < paramCount; ++i)
            p.applied[i] = values[i], p.engine.set (paramTable[i].id, values[i]);
    // The texts may read differently now (Quantize), and the screen shows the new values.
    p.textStale.store (true, std::memory_order_release);
    if (p.host != nullptr && p.host->request_callback != nullptr)
        p.host->request_callback (p.host);
    if (p.hostScreen != nullptr)
        p.hostScreen->request_paint (p.host);
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
    for (uint32_t i = 0; i < header[1]; ++i)
    {
        struct { uint32_t id; float value; } pair {};
        if (! readAll (stream, &pair, sizeof (pair)))
            return false;
        const int index = paramIndex (pair.id);
        if (index >= 0 && paramTable[index].kind != Kind::button)
            loaded[index] = clampParam (paramTable[index], pair.value);
    }
    setAll (p, loaded);
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
    if (const auto* hostParams = static_cast<const clap_host_params*> (p.host->get_extension (p.host, CLAP_EXT_PARAMS)))
        if (hostParams->rescan != nullptr)
            hostParams->rescan (p.host, CLAP_PARAM_RESCAN_VALUES);
    if (hostPresets != nullptr && hostPresets->loaded != nullptr)
        hostPresets->loaded (p.host, kind, nullptr, key);
    return true;
}

const clap_plugin_preset_load presetExtension { presetFromLocation };

// Ports: no audio in, one stereo out (the same signal both sides: Melodee plays a mono port on the left only), one
// note input for triggering by hand.
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
    for (uint32_t i = 0; i < paramCount; ++i)
    {
        p.values[i].store (paramTable[i].def, std::memory_order_relaxed);
        p.applied[i] = paramTable[i].def;
        p.engine.set (paramTable[i].id, paramTable[i].def);
    }
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
    p.engine.prepare (rate, maxFrames);
    p.active = true;
    return true;
}

void pluginDeactivate (const clap_plugin* plugin) { of (plugin).active = false; }
bool pluginStart (const clap_plugin*) { return true; }
void pluginStop (const clap_plugin*) {}
void pluginReset (const clap_plugin* plugin) { of (plugin).engine.reset(); }

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

    // Values the main thread changed (a state loaded while playing): looked through only when it wrote some.
    if (p.mainChanged.exchange (false, std::memory_order_acquire))
        for (uint32_t i = 0; i < paramCount; ++i)
        {
            const float v = p.values[i].load (std::memory_order_relaxed);
            if (v != p.applied[i] && paramTable[i].kind != Kind::button)
                p.applied[i] = v, p.engine.set (paramTable[i].id, v);
        }
    // The values the screen's page set are told to the host first, at the frame the engine takes them (and before any
    // event this block sends back, so they stay in time order).
    tellHost (p, process->out_events, 0);

    // Events at the block's first frame come first, the transport after them: a setting the host sends with Play is
    // in place for what Play does (the sequencer's reset at Play, its first step).
    const auto* in = process->in_events;
    const uint32_t count = in != nullptr ? in->size (in) : 0;
    uint32_t next = 0;
    const uint32_t lastFrame = frames > 0 ? frames - 1 : 0;
    for (; next < count && in->get (in, next)->time == 0; ++next)
        handle (p, in->get (in, next), process->out_events, lastFrame);
    // The screen's buttons (Trigger, Advance) and a step it plays, at the block's first frame (if they are fresh).
    if (p.presses.load (std::memory_order_relaxed) != 0 || p.audition.load (std::memory_order_relaxed) >= 0)
    {
        uint64_t rows = p.presses.exchange (0, std::memory_order_acquire);
        const int s = p.audition.exchange (-1, std::memory_order_acquire);
        if (steadyMs() - p.pressedAt.load (std::memory_order_acquire) <= pressFreshMs)
        {
            for (uint32_t i = 0; rows != 0; ++i, rows >>= 1)
                if (rows & 1u)
                    p.engine.press (paramTable[i].id);
            if (s >= 0)
                p.engine.playStep (s, 1.0f);
        }
    }

    // The host's transport. Without a song position (Melodee always sends one), the sequence counts its own from
    // Play, so the first step is the one Play lands on.
    if (const auto* t = process->transport)
    {
        const bool playing = (t->flags & CLAP_TRANSPORT_IS_PLAYING) != 0;
        const bool placed = (t->flags & CLAP_TRANSPORT_HAS_BEATS_TIMELINE) != 0;
        const double bpm = (t->flags & CLAP_TRANSPORT_HAS_TEMPO) && t->tempo > 0.0 ? t->tempo : 120.0;
        const double beats = placed ? (double) t->song_pos_beats / (double) CLAP_BEATTIME_FACTOR : p.engine.ownPosition();
        p.engine.transport (playing, beats, bpm, frames);
    }
    else
        p.engine.transport (false, 0.0, 120.0, frames);

    uint32_t done = 0;
    while (done < frames)
    {
        // Every event due by now, then sound up to the next one.
        while (next < count)
        {
            const auto* e = in->get (in, next);
            if (e->time > done)
                break;
            handle (p, e, process->out_events, lastFrame);
            ++next;
        }
        uint32_t until = frames;
        if (next < count)
            until = std::min<uint32_t> (frames, std::max<uint32_t> (in->get (in, next)->time, done + 1));
        p.engine.render (left + done, right ? right + done : nullptr, until - done);
        done = until;
    }
    for (; next < count; ++next) // events stamped past the block (a host's mistake): applied at its end
        handle (p, in->get (in, next), process->out_events, lastFrame);
    // Publish sequence and hit changes. The screen schedules its own animation frames.
    if (p.hostScreen != nullptr)
    {
        const int step = p.engine.currentStep();
        const uint32_t hits = p.engine.hitCount();
        const bool running = p.engine.running();
        if (step != p.shownStep || hits != p.shownHits || running != p.shownRunning)
        {
            p.shownStep = step, p.shownHits = hits, p.shownRunning = running;
            p.stepState.step.store (step, std::memory_order_relaxed);
            p.stepState.running.store (running, std::memory_order_relaxed);
            p.stepState.hits.store (hits, std::memory_order_release);
            p.hostScreen->request_paint (p.host);
        }
        else if (p.valuesMoved)
            p.hostScreen->request_paint (p.host);
    }
    p.valuesMoved = false;
    // Never "sleep": the sequencer must hear the next Play, which is no event. (Melodee puts an idle instrument to
    // sleep itself, and the engine skips its own work while the VCA is closed.)
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
    p.screen.update (h.nowMs);
    const bool drew = p.screen.paint (h, c->pixels, (int) c->stride, c->whole);
    bool busy = p.screenTouched || c->whole || c->playing;
    for (int k = 0; k < 3; ++k)
        if (c->knobs[k].position != p.shownPots[k])
            p.shownPots[k] = c->knobs[k].position, busy = true;
    p.screenTouched = false;
    if (busy)
        p.screenBusyMs = h.nowMs;
    // The next pose while something moves (Melodee keeps painting while the idle instrument sleeps); at rest, none.
    if (p.hostScreen != nullptr && (h.nowMs < p.screenBusyMs || h.nowMs - p.screenBusyMs < screenRestMs))
        p.hostScreen->request_paint (p.host);
    return drew;
}

bool screenInput (const clap_plugin* plugin, const melodee_screen_input_t* e)
{
    if (e == nullptr || e->kind > MELODEE_SCREEN_NUDGE)
        return false;
    auto& p = of (plugin);
    p.screenTouched = true; // the page animates again from its next paint
    hdui::Edits edits;
    const bool used = p.screen.input ((hdui::Key) e->kind, e->value, e->shift, edits);
    bool changed = false, pressed = false;
    for (int i = 0; i < edits.count; ++i)
    {
        const auto& edit = edits.list[i];
        if (edit.press)
            p.pressedAt.store (steadyMs(), std::memory_order_release), pressed = true;
        if (edit.press && edit.id >= auditionId && edit.id < auditionId + 8)
        {
            p.audition.store ((int) (edit.id - auditionId), std::memory_order_release);
            continue;
        }
        const int index = paramIndex (edit.id);
        if (index < 0)
            continue;
        if (edit.press)
            p.presses.fetch_or (1ull << index, std::memory_order_release);
        else
        {
            p.values[index].store (clampParam (paramTable[index], edit.value), std::memory_order_relaxed);
            p.outgoing.fetch_or (1ull << index, std::memory_order_release);
            p.mainChanged.store (true, std::memory_order_release);
            textMoved (p, edit.id);
            changed = true;
        }
    }
    // The host's own copy of the values: read again now (this is a main-thread call), since a host may not run the
    // plug-in soon enough to hear them from process() (Melodee does not run a sleeping instrument, and its
    // request_flush waits for the next block). The output events still follow for hosts that record them.
    if (changed && p.hostParams != nullptr)
    {
        if (p.hostParams->rescan != nullptr)
            p.hostParams->rescan (p.host, CLAP_PARAM_RESCAN_VALUES);
        if (p.hostParams->request_flush != nullptr)
            p.hostParams->request_flush (p.host);
    }
    // A press, or a value the engine should take: ask to be run (a host that put the plug-in to sleep wakes it).
    if ((changed || pressed) && p.host != nullptr && p.host->request_process != nullptr)
        p.host->request_process (p.host);
    if (used && p.hostScreen != nullptr)
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
    if (std::strcmp (id, CLAP_EXT_NOTE_PORTS) == 0) return &noteExtension;
    if (std::strcmp (id, CLAP_EXT_PRESET_LOAD) == 0 || std::strcmp (id, CLAP_EXT_PRESET_LOAD_COMPAT) == 0) return &presetExtension;
    return nullptr;
}

// The host's call back to the main thread (asked for by textMoved): the steps' Pitch texts changed with Quantize.
void pluginMainThread (const clap_plugin* plugin)
{
    auto& p = of (plugin);
    if (p.textStale.exchange (false, std::memory_order_acquire) && p.hostParams != nullptr && p.hostParams->rescan != nullptr)
        p.hostParams->rescan (p.host, CLAP_PARAM_RESCAN_TEXT);
}

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

// Preset discovery: one provider, one location inside the plug-in, the presets of presets.h. Nothing to allocate:
// the provider is a constant.
const clap_preset_discovery_provider_descriptor presetProviderDescriptor { CLAP_VERSION, "com.hdslave.anvil.presets",
                                                                           "hdslave Anvil presets", "hdslave" };

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
} // namespace anvil

extern "C" CLAP_EXPORT const clap_plugin_entry clap_entry { CLAP_VERSION, anvil::entryInit, anvil::entryDeinit, anvil::entryFactory };
