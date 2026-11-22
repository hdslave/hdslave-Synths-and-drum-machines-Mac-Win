// Necronomicon: a polyrhythmic subharmonic synthesizer as a CLAP plug-in, made for the Akai MPC Sample.
// This file is the CLAP side: the factory, ports, parameters, state, presets and the process call, which cuts the
// block at each event so every change lands on its frame. The sound and the sequencers are in engine.h.
#include "screen.h"
#include <chrono>
#include "engine.h"
#include "params.h"
#include "presets.h"

#include <clap/clap.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <new>

namespace necronomicon
{
namespace
{
const char* const features[] = { CLAP_PLUGIN_FEATURE_INSTRUMENT, CLAP_PLUGIN_FEATURE_SYNTHESIZER, CLAP_PLUGIN_FEATURE_STEREO, nullptr };

const clap_plugin_descriptor descriptor {
    CLAP_VERSION, "com.hdslave.necronomicon", "hdslave Necronomicon", "hdslave", "", "", "", NECRONOMICON_VERSION,
    "Two VCOs, each with two subharmonics, a ladder filter and two AD envelopes, played by two 4-step sequencers "
    "and four rhythm generators in time with the host",
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
    std::atomic<bool> textChanged { false }; // Seq Oct moved: the steps' text reads otherwise; tell the host
    float applied[paramCount] {};
    Engine engine;
    bool active = false;
    const clap_host_melodee_screen* hostScreen = nullptr;
    const clap_host_params* hostParams = nullptr;
    Screen screen;
    std::atomic<uint64_t> position { 0 }, outgoing { 0 };
    std::atomic<int> colorStep { 0 };
    bool valuesMoved = false; // owned by process/flush
    // One producer (screen input), one consumer (process). Keep repeated presses in order,
    // with their individual timestamps, even if the host sleeps between them.
    struct Press { uint32_t id, generation; uint64_t when; };
    std::array<Press, 32> presses {};
    std::atomic<uint32_t> written { 0 }, taken { 0 }, generation { 0 };
    // The settings pages' eye animates while the song plays and for screenRestMs after the page is opened, touched or a
    // pot moves; stopped and left alone it rests (no next paint asked for), and Play, a value or any input wakes it.
    // Main thread only (paint and input).
    uint64_t screenBusyMs = 0;
    bool screenTouched = true;
    int16_t shownPots[3] { -2, -2, -2 };
};
constexpr uint64_t screenRestMs = 4000;

Plugin& of (const clap_plugin* p) { return *static_cast<Plugin*> (p->plugin_data); }

static_assert (paramCount <= 64, "page edits use one bit per parameter");

uint64_t steadyMs()
{
    return uint64_t (std::chrono::duration_cast<std::chrono::milliseconds> (
        std::chrono::steady_clock::now().time_since_epoch()).count());
}

void repaint (Plugin& p)
{
    if (p.hostScreen && p.hostScreen->request_paint) p.hostScreen->request_paint (p.host);
}

void publishScreen (Plugin& p, bool colorsMoved = false)
{
    if (!p.hostScreen) return;
    const uint64_t pos = p.engine.screenPosition();
    const bool moved = pos != p.position.load (std::memory_order_relaxed);
    if (moved) p.position.store (pos, std::memory_order_release);
    if (moved || p.valuesMoved || colorsMoved) repaint (p);
    p.valuesMoved = false;
}

void tellHost (Plugin& p, const clap_output_events* out)
{
    if (!out || !p.outgoing.load (std::memory_order_relaxed)) return;
    uint64_t bits = p.outgoing.exchange (0, std::memory_order_acquire);
    while (bits)
    {
        const unsigned i = unsigned (__builtin_ctzll (bits));
        const uint64_t bit = uint64_t (1) << i;
        bits &= ~bit;
        clap_event_param_value e {};
        e.header = { sizeof e, 0, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_PARAM_VALUE, 0 };
        e.param_id = paramTable[i].id; e.note_id = e.port_index = e.channel = e.key = -1;
        e.value = p.values[i].load (std::memory_order_relaxed);
        if (!out->try_push (out, &e.header)) p.outgoing.fetch_or (bit, std::memory_order_release);
    }
}

void takePresses (Plugin& p)
{
    uint32_t at = p.taken.load (std::memory_order_relaxed);
    const uint32_t end = p.written.load (std::memory_order_acquire);
    if (at == end) return;
    const uint64_t now = steadyMs();
    while (at != end)
    {
        const auto press = p.presses[at++ % p.presses.size()];
        if (press.generation == p.generation.load (std::memory_order_acquire) && now - press.when <= 500)
            p.engine.press (press.id);
    }
    p.taken.store (end, std::memory_order_release);
}

// The octaves a step knob at full turn is worth now: the steps' text depends on Seq Oct.
float octavesNow (const Plugin& p) { return stepOctaves (p.values[paramIndex (ParamId::seqOct)].load (std::memory_order_relaxed)); }

// Seq Oct moved (on any thread): the host should read the steps' text again, from the main thread.
void textMoved (Plugin& p)
{
    p.textChanged.store (true, std::memory_order_release);
    p.host->request_callback (p.host);
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
    formatParam (paramTable[index], (float) value, text, size, octavesNow (of (plugin)));
    return true;
}

bool paramsFromText (const clap_plugin* plugin, clap_id id, const char* text, double* value)
{
    const int index = paramIndex (id);
    if (index < 0)
        return false;
    float v = 0.0f;
    if (! parseParam (paramTable[index], text, v, octavesNow (of (plugin))))
        return false;
    *value = v;
    return true;
}

// One event: parameter values go to the engine at once; notes move the pitch and gate the EGs. A button's release
// is sent back stamped no later than `lastFrame` (an event stamped past the block is applied at its end).
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
            p.valuesMoved |= value != p.applied[index];
            if (info.id == ParamId::seqOct && value != p.applied[index])
                textMoved (p);
            p.values[index].store (value, std::memory_order_relaxed);
            p.applied[index] = value;
            p.engine.set (info.id, value);
            break;
        }
        case CLAP_EVENT_NOTE_ON:
        case CLAP_EVENT_NOTE_OFF:
        case CLAP_EVENT_NOTE_CHOKE:
        {
            const auto* n = reinterpret_cast<const clap_event_note*> (e);
            if (e->type != CLAP_EVENT_NOTE_ON && n->key < 0) // key -1: every note (a wildcard off or choke)
                p.engine.allNotesOff();
            else if (n->key >= 0 && n->key < 128)
            {
                if (e->type == CLAP_EVENT_NOTE_ON)
                    p.engine.noteOn (n->key);
                else
                    p.engine.noteOff (n->key);
            }
            break;
        }
        case CLAP_EVENT_MIDI:
        {
            const auto* m = reinterpret_cast<const clap_event_midi*> (e);
            const int status = m->data[0] & 0xf0, key = m->data[1] & 0x7f;
            if (status == 0x90 && (m->data[2] & 0x7f) > 0)
                p.engine.noteOn (key);
            else if (status == 0x80 || status == 0x90)
                p.engine.noteOff (key);
            else if (status == 0xb0 && (key == 120 || key == 123)) // all sound off, all notes off
                p.engine.allNotesOff();
            break;
        }
        default: break;
    }
}

void paramsFlush (const clap_plugin* plugin, const clap_input_events* in, const clap_output_events* out)
{
    auto& p = of (plugin);
    tellHost (p, out);
    const uint32_t count = in ? in->size (in) : 0;
    for (uint32_t i = 0; i < count; ++i)
    {
        const auto* e = in->get (in, i);
        // Active, a flush comes on the audio thread in place of a process call: the engine takes it as it would
        // there (buttons too).
        if (p.active)
        {
            if (e->space_id == CLAP_CORE_EVENT_SPACE_ID && e->type == CLAP_EVENT_PARAM_VALUE)
                handle (p, e, out, 0);
            continue;
        }
        // Not active, the engine is not running: only values are kept (buttons do nothing).
        if (e->space_id == CLAP_CORE_EVENT_SPACE_ID && e->type == CLAP_EVENT_PARAM_VALUE)
        {
            const auto* v = reinterpret_cast<const clap_event_param_value*> (e);
            const int index = paramIndex (v->param_id);
            if (index >= 0 && paramTable[index].kind != Kind::button)
            {
                const float value = clampParam (paramTable[index], (float) v->value);
                if (paramTable[index].id == ParamId::seqOct && value != p.values[index].load (std::memory_order_relaxed))
                    textMoved (p);
                p.valuesMoved = true;
                p.values[index].store (value, std::memory_order_relaxed);
                p.mainChanged.store (true, std::memory_order_release);
            }
        }
    }
    publishScreen (p);
}

const clap_plugin_params paramsExtension { paramsCount, paramsInfo, paramsValue, paramsToText, paramsFromText, paramsFlush };

// State: "NECR", a version, the number of values, then (id, value) pairs; unknown ids are skipped, missing ones keep
// their defaults, so a state saved by another version still loads.
constexpr char stateTag[4] = { 'N', 'E', 'C', 'R' };
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
    p.generation.fetch_add (1, std::memory_order_release);
    p.outgoing.store (0, std::memory_order_release);
    for (uint32_t i = 0; i < paramCount; ++i)
        p.values[i].store (values[i], std::memory_order_relaxed);
    p.mainChanged.store (true, std::memory_order_release);
    if (! p.active) // not processing: the engine takes them now; otherwise at the next block
        for (uint32_t i = 0; i < paramCount; ++i)
            p.applied[i] = values[i], p.engine.set (paramTable[i].id, values[i]);
    repaint (p);
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
    const int octIndex = paramIndex (ParamId::seqOct);
    if (loaded[octIndex] != p.values[octIndex].load (std::memory_order_relaxed))
        textMoved (p); // the steps' text reads otherwise now
    setAll (p, loaded);
    return true;
}

const clap_plugin_state stateExtension { stateSave, stateLoad };

// Presets (presets.h): inside the plug-in, listed by the preset-discovery factory below and loaded by their key.
// A preset sets every value, as a state does; then the host is told the values (and the steps' text) moved.
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
            hostParams->rescan (p.host, CLAP_PARAM_RESCAN_VALUES | CLAP_PARAM_RESCAN_TEXT);
    if (hostPresets != nullptr && hostPresets->loaded != nullptr)
        hostPresets->loaded (p.host, kind, nullptr, key);
    return true;
}

const clap_plugin_preset_load presetExtension { presetFromLocation };

// Ports: no audio in, one stereo out (the same signal both sides: Melodee plays a mono port on the left only), one
// note input for playing it from a keyboard or pads.
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
    p.hostScreen = static_cast<const clap_host_melodee_screen*> (p.host->get_extension (p.host, CLAP_EXT_MELODEE_SCREEN_HOST));
    p.hostParams = static_cast<const clap_host_params*> (p.host->get_extension (p.host, CLAP_EXT_PARAMS));
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
    publishScreen (p);
    p.active = true;
    return true;
}

void pluginDeactivate (const clap_plugin* plugin) { of (plugin).active = false; }
bool pluginStart (const clap_plugin*) { return true; }
void pluginStop (const clap_plugin*) {}
void pluginReset (const clap_plugin* plugin)
{
    auto& p = of (plugin); p.engine.reset(); publishScreen (p);
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

int wheelColorStep (const clap_event_transport& t, double beat)
{
    const bool metered = (t.flags & CLAP_TRANSPORT_HAS_TIME_SIGNATURE) && t.tsig_num && t.tsig_denom;
    const double barLength = metered ? 4.0 * t.tsig_num / t.tsig_denom : 4.0;
    double bars = beat / barLength;
    if (metered && (t.flags & CLAP_TRANSPORT_HAS_BEATS_TIMELINE))
        bars = t.bar_number + (beat - double (t.bar_start) / CLAP_BEATTIME_FACTOR) / barLength;
    const double turns = bars / 4.0;
    return int (std::lround ((turns - std::floor (turns)) * colorCycleSteps)) % colorCycleSteps;
}

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

    tellHost (p, process->out_events);

    // Events at the block's first frame come first, the transport after them: a setting the host sends with Play is
    // in place for what Play does (the sequencers lined up by the song's place, the first clock).
    const auto* in = process->in_events;
    const uint32_t count = in != nullptr ? in->size (in) : 0;
    uint32_t next = 0;
    const uint32_t lastFrame = frames > 0 ? frames - 1 : 0;
    for (; next < count && in->get (in, next)->time == 0; ++next)
        handle (p, in->get (in, next), process->out_events, lastFrame);
    // The page's presses (if fresh) count as the host's buttons at the first frame do: before the transport.
    takePresses (p);

    // The host's transport. Without a song position (Melodee always sends one), the sequencers count their own from
    // Play, so the first clock is the one Play lands on.
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
    bool colorsMoved = false;
    if (p.hostScreen && process->transport && (process->transport->flags & CLAP_TRANSPORT_IS_PLAYING))
    {
        const int step = wheelColorStep (*process->transport, p.engine.ownPosition());
        colorsMoved = step != p.colorStep.load (std::memory_order_relaxed);
        if (colorsMoved) p.colorStep.store (step, std::memory_order_relaxed);
    }
    publishScreen (p, colorsMoved);
    // Never "sleep": the sequencers must hear the next Play, which is no event. (Melodee puts an idle instrument to
    // sleep itself, and the engine skips its own work while the VCA is closed.)
    return CLAP_PROCESS_CONTINUE;
}

bool screenPaint (const clap_plugin* plugin, const melodee_screen_canvas_t* c)
{
    if (!c || !c->pixels || c->width != hdui::width || c->height != hdui::height || c->stride < c->width || c->stride > INT32_MAX)
        return false;
    auto& p = of (plugin);
    const bool drew = p.screen.paint (*c, p.values, p.position.load (std::memory_order_acquire),
                                      p.colorStep.load (std::memory_order_relaxed));
    bool busy = p.screenTouched || c->whole || c->playing;
    for (int k = 0; k < 3; ++k)
        if (c->knobs[k].position != p.shownPots[k]) p.shownPots[k] = c->knobs[k].position, busy = true;
    p.screenTouched = false;
    if (busy) p.screenBusyMs = c->now_ms;
    const bool recent = c->now_ms < p.screenBusyMs || c->now_ms - p.screenBusyMs < screenRestMs;
    if (c->playing || p.screen.flashing() || (p.screen.needsAnimation() && recent)) repaint (p);
    return drew;
}

bool screenInput (const clap_plugin* plugin, const melodee_screen_input_t* e)
{
    if (!e) return false;
    auto& p = of (plugin);
    p.screenTouched = true; // the page animates again from its next paint
    ScreenEdit edit;
    if (!p.screen.input (*e, p.values, edit)) return false;
    if (edit.id != hdui::noParam)
    {
        if (edit.press)
        {
            const uint32_t at = p.written.load (std::memory_order_relaxed);
            if (at - p.taken.load (std::memory_order_acquire) >= p.presses.size()) return false;
            p.presses[at % p.presses.size()] = { edit.id, p.generation.load (std::memory_order_relaxed), steadyMs() };
            p.written.store (at + 1, std::memory_order_release);
        }
        else
        {
            const int i = paramIndex (edit.id);
            p.values[i].store (edit.value, std::memory_order_relaxed);
            p.mainChanged.store (true, std::memory_order_release);
            p.outgoing.fetch_or (uint64_t (1) << i, std::memory_order_release);
            if (edit.id == ParamId::seqOct) textMoved (p);
            if (p.hostParams && p.hostParams->rescan) p.hostParams->rescan (p.host, CLAP_PARAM_RESCAN_VALUES);
            if (p.hostParams && p.hostParams->request_flush) p.hostParams->request_flush (p.host);
        }
        if (p.host->request_process) p.host->request_process (p.host);
    }
    repaint (p);
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

// On the main thread, when asked: Seq Oct moved, so the steps' text (semitones at its range) reads otherwise.
void pluginMainThread (const clap_plugin* plugin)
{
    auto& p = of (plugin);
    if (! p.textChanged.exchange (false, std::memory_order_acquire))
        return;
    if (const auto* hostParams = static_cast<const clap_host_params*> (p.host->get_extension (p.host, CLAP_EXT_PARAMS)))
        if (hostParams->rescan != nullptr)
            hostParams->rescan (p.host, CLAP_PARAM_RESCAN_TEXT);
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
const clap_preset_discovery_provider_descriptor presetProviderDescriptor { CLAP_VERSION, "com.hdslave.necronomicon.presets",
                                                                           "hdslave Necronomicon presets", "hdslave" };

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
} // namespace necronomicon

extern "C" CLAP_EXPORT const clap_plugin_entry clap_entry { CLAP_VERSION, necronomicon::entryInit, necronomicon::entryDeinit, necronomicon::entryFactory };
