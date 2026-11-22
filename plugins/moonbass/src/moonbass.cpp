// MoonBass: an analog-style kick drum synthesizer as a CLAP plug-in, made for the Akai MPC Sample.
// This file is the CLAP side: the factory, ports, parameters, state, presets, the MIDI controller map and the process
// call, which cuts the block at each event so every change lands on its frame. The sound is in engine.h.
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

namespace moonbass
{
namespace
{
const char* const features[] = { CLAP_PLUGIN_FEATURE_INSTRUMENT, CLAP_PLUGIN_FEATURE_SYNTHESIZER, CLAP_PLUGIN_FEATURE_DRUM,
                                 CLAP_PLUGIN_FEATURE_STEREO, nullptr }; // one voice, a stereo port (the same both sides)

const clap_plugin_descriptor descriptor {
    CLAP_VERSION, "com.hdslave.moonbass", "hdslave MoonBass", "hdslave", "", "", "", MOONBASS_VERSION,
    "An analog-style kick drum: a VCO with pitch envelope and harmonics, pulse and two metallic noise generators in "
    "the attack, two LFOs and four CV inputs, every parameter on its own page",
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
    Screen screen;
    const clap_host_melodee_screen* hostScreen = nullptr;
    const clap_host_params* hostParams = nullptr;
    std::atomic<uint64_t> outgoing { 0 }, pressedAt { 0 };
    std::atomic<uint32_t> hits { 0 };
    bool valuesMoved = false; // coalesce host automation and MIDI into one repaint per block
    bool active = false;
};

Plugin& of (const clap_plugin* p) { return *static_cast<Plugin*> (p->plugin_data); }

void requestPaint (const Plugin& p)
{
    if (p.hostScreen && p.hostScreen->request_paint) p.hostScreen->request_paint (p.host);
}

uint64_t steadyMs()
{
    using namespace std::chrono;
    return (uint64_t) duration_cast<milliseconds> (steady_clock::now().time_since_epoch()).count();
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

// A value the plug-in set itself (a button returning to 0, a MIDI controller), told to the host.
bool tellHost (const clap_output_events* out, uint32_t time, clap_id id, double value)
{
    if (out == nullptr)
        return false;
    clap_event_param_value back {};
    back.header = { sizeof (back), time, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_PARAM_VALUE, 0 };
    back.param_id = id;
    back.note_id = back.port_index = back.channel = back.key = -1;
    back.value = value;
    return out->try_push (out, &back.header);
}

// Page edits are recordable parameter events. Retry a full output queue on its next flush/block.
void sendPageValues (Plugin& p, const clap_output_events* out)
{
    static_assert (paramCount <= 64);
    if (!out || !p.outgoing.load (std::memory_order_relaxed)) return;
    uint64_t rows = p.outgoing.exchange (0, std::memory_order_acquire);
    while (rows)
    {
        const int row = __builtin_ctzll (rows);
        rows &= rows - 1;
        if (!tellHost (out, 0, paramTable[row].id, p.values[row].load (std::memory_order_relaxed)))
            p.outgoing.fetch_or (uint64_t (1) << row, std::memory_order_release);
    }
}

// The hardware's MIDI controller map (the ModBase 09's; the MBase 11 shares 100-117): a controller sets its parameter,
// as turning it would, and the host is told. Any MIDI channel: the host's track is the channel.
// 120, 121 and 123 at 0 are MIDI's All Sound Off, Reset All Controllers and All Notes Off, which hosts send on stop
// and panic (Melodee does, on every channel): those stop the sound or do nothing, and never move a parameter.
void controller (Plugin& p, uint32_t time, int number, int value, const clap_output_events* out)
{
    using namespace ParamId;
    if (value == 0 && (number == 120 || number == 121 || number == 123))
    {
        if (number == 120)
            p.engine.silence();
        return; // a note has no hold to let go of, and no controller here is MIDI's to reset
    }
    uint32_t id = count;
    float v = (float) value / 127.0f;
    switch (number)
    {
        case 100: id = tune; break;
        case 101: id = pitch; break;
        case 102: id = decay; break;
        case 103: id = harmonics; break;
        case 104: id = pulse; break;
        case 105: id = noise; break;
        case 106: id = attack; break;
        case 107: id = eq; break;
        case 113: id = compression; break;
        case 114: id = gateTime; break;
        // The metal noises: 0 Off, then (Metal Noise 1) 1 Noise, and the rest spread over the patterns to 499.
        case 115: id = metalNoise1, v = value < 2 ? (float) value : 2.0f + std::round ((float) (value - 2) * (float) (metalPatterns - 2) / 125.0f); break;
        case 116: id = metalNoise2, v = value < 1 ? 0.0f : 1.0f + std::round ((float) (value - 1) * (float) (metalPatterns - 1) / 126.0f); break;
        case 117: id = volume; break;
        case 119: id = lfo1Wave, v = (float) std::min (value, 15); break;
        case 120: id = noise2Mask, v = (float) (std::min (value, 15) + 1); break;
        case 121: id = lfo1Int; break;
        case 122: id = lfo1Speed; break;
        case 123: id = lfo2Wave, v = (float) std::min (value, 15); break;
        case 124: id = lfo2Int; break;
        case 125: id = lfo2Speed; break;
        default: return;
    }
    const int index = paramIndex (id);
    const float clamped = clampParam (paramTable[index], v);
    p.valuesMoved |= p.values[index].load (std::memory_order_relaxed) != clamped;
    p.values[index].store (clamped, std::memory_order_relaxed);
    p.applied[index] = clamped;
    p.engine.set (id, clamped);
    tellHost (out, time, id, clamped);
}

// One event: parameter values go to the engine at once; notes trigger it.
void handle (Plugin& p, const clap_event_header* e, const clap_output_events* out)
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
                    tellHost (out, e->time, info.id, 0.0);
                }
                p.values[index].store (0.0f, std::memory_order_relaxed);
                p.applied[index] = 0.0f;
                break;
            }
            p.valuesMoved |= p.values[index].load (std::memory_order_relaxed) != value;
            p.values[index].store (value, std::memory_order_relaxed);
            p.applied[index] = value;
            p.engine.set (info.id, value);
            break;
        }
        case CLAP_EVENT_NOTE_ON:
        {
            // A note plays the kick if Split Mode takes its key (a note off does nothing: Gate Time is the gate).
            const auto* n = reinterpret_cast<const clap_event_note*> (e);
            if (n->velocity > 0.0 && n->key >= 0 && n->key < 128)
                p.engine.note (n->key, (float) n->velocity);
            break;
        }
        case CLAP_EVENT_NOTE_CHOKE:
        {
            // A choke (Melodee's all sound off, a cut group) stops the hit its key played.
            p.engine.choke (reinterpret_cast<const clap_event_note*> (e)->key);
            break;
        }
        case CLAP_EVENT_MIDI:
        {
            const auto* m = reinterpret_cast<const clap_event_midi*> (e);
            if ((m->data[0] & 0xf0) == 0x90 && (m->data[2] & 0x7f) > 0)
                p.engine.note (m->data[1] & 0x7f, (float) (m->data[2] & 0x7f) / 127.0f);
            else if ((m->data[0] & 0xf0) == 0xb0)
                controller (p, e->time, m->data[1] & 0x7f, m->data[2] & 0x7f, out);
            break;
        }
        default: break;
    }
}

void paramsFlush (const clap_plugin* plugin, const clap_input_events* in, const clap_output_events* out)
{
    auto& p = of (plugin);
    sendPageValues (p, out);
    bool changed = false;
    const uint32_t count = in ? in->size (in) : 0;
    for (uint32_t i = 0; i < count; ++i)
    {
        const auto* e = in->get (in, i);
        // Outside process the engine is not running: only values are kept (buttons do nothing). Not active, the
        // engine takes them now, as setAll() does; active, at the next block.
        if (e->space_id == CLAP_CORE_EVENT_SPACE_ID && e->type == CLAP_EVENT_PARAM_VALUE)
        {
            const auto* v = reinterpret_cast<const clap_event_param_value*> (e);
            const int index = paramIndex (v->param_id);
            if (index >= 0 && paramTable[index].kind != Kind::button)
            {
                const float value = clampParam (paramTable[index], (float) v->value);
                changed |= p.values[index].exchange (value, std::memory_order_relaxed) != value;
                if (p.active)
                    p.mainChanged.store (true, std::memory_order_release);
                else
                    p.applied[index] = value, p.engine.set (paramTable[index].id, value);
            }
        }
    }
    if (changed) requestPaint (p);
}

const clap_plugin_params paramsExtension { paramsCount, paramsInfo, paramsValue, paramsToText, paramsFromText, paramsFlush };

// State: "MOON", a version, the number of values, then (id, value) pairs; unknown ids are skipped, missing ones keep
// their defaults, so a state saved by another version still loads.
constexpr char stateTag[4] = { 'M', 'O', 'O', 'N' };
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
    p.outgoing.store (0, std::memory_order_relaxed);
    p.pressedAt.store (0, std::memory_order_relaxed);
    for (uint32_t i = 0; i < paramCount; ++i)
        p.values[i].store (values[i], std::memory_order_relaxed);
    p.mainChanged.store (true, std::memory_order_release);
    if (! p.active) // not processing: the engine takes them now; otherwise at the next block
        for (uint32_t i = 0; i < paramCount; ++i)
            p.applied[i] = values[i], p.engine.set (paramTable[i].id, values[i]);
    requestPaint (p);
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
// note input (CLAP notes or MIDI, with the controller map).
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
        p.values[i].store (paramTable[i].def, std::memory_order_relaxed);
        p.applied[i] = paramTable[i].def;
        p.engine.set (paramTable[i].id, paramTable[i].def);
    }
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

    sendPageValues (p, process->out_events);

    // Events at the block's first frame come first, the transport after them: a setting the host sends with Play is
    // in place for what Play does (a synced LFO taking its place in the song).
    const auto* in = process->in_events;
    const uint32_t count = in != nullptr ? in->size (in) : 0;
    uint32_t next = 0;
    for (; next < count && in->get (in, next)->time == 0; ++next)
        handle (p, in->get (in, next), process->out_events);

    // The host's transport: the tempo for the LFOs whose Clock follows it, the song's place for those in step with it.
    if (const auto* t = process->transport)
    {
        const bool playing = (t->flags & CLAP_TRANSPORT_IS_PLAYING) != 0;
        const bool placed = (t->flags & CLAP_TRANSPORT_HAS_BEATS_TIMELINE) != 0;
        // A tempo that is no number, or none, is 120; the rest is held to 1-1000 BPM (an infinite one would make the
        // synced LFOs' places NaN, and the EQ would keep it).
        const bool tempo = (t->flags & CLAP_TRANSPORT_HAS_TEMPO) && std::isfinite (t->tempo) && t->tempo > 0.0;
        const double bpm = tempo ? std::min (std::max (t->tempo, 1.0), 1000.0) : 120.0;
        const double beats = placed ? (double) t->song_pos_beats / (double) CLAP_BEATTIME_FACTOR : 0.0;
        p.engine.transport (playing, placed, beats, bpm, frames);
    }
    else
        p.engine.transport (false, false, 0.0, 120.0, frames);

    // A sleeping host may deliver this much later. Only audition a recent press.
    if (p.pressedAt.load (std::memory_order_relaxed) != 0)
    {
        const auto pressed = p.pressedAt.exchange (0, std::memory_order_acquire);
        if (pressed && steadyMs() - pressed <= 500)
            p.engine.press (ParamId::play);
    }

    uint32_t done = 0;
    while (done < frames)
    {
        // Every event due by now, then sound up to the next one.
        while (next < count)
        {
            const auto* e = in->get (in, next);
            if (e->time > done)
                break;
            handle (p, e, process->out_events);
            ++next;
        }
        uint32_t until = frames;
        if (next < count)
            until = std::min<uint32_t> (frames, std::max<uint32_t> (in->get (in, next)->time, done + 1));
        p.engine.render (left + done, right ? right + done : nullptr, until - done);
        done = until;
    }
    for (; next < count; ++next) // events stamped past the block (a host's mistake): applied at its end
        handle (p, in->get (in, next), process->out_events);
    if (p.hostScreen && (p.valuesMoved || p.hits.load (std::memory_order_relaxed) != p.engine.hitCount()))
    {
        p.hits.store (p.engine.hitCount(), std::memory_order_release);
        requestPaint (p);
    }
    p.valuesMoved = false;
    // Never "sleep": a synced LFO must hear the next Play, which is no event. (Melodee puts an idle instrument to sleep
    // itself, and the engine does no work once the kick has died away.)
    return CLAP_PROCESS_CONTINUE;
}

// Melodee's page ---------------------------------------------------------------------------------------------
bool screenPaint (const clap_plugin* plugin, const melodee_screen_canvas_t* c)
{
    if (!c || !c->pixels || c->width != hdui::width || c->height != hdui::height || c->stride < c->width)
        return false;
    auto& p = of (plugin);
    const bool drew = p.screen.paint (*c, p.values, p.hits.load (std::memory_order_acquire));
    if (p.screen.animating()) requestPaint (p); // the next frame even after audio processing stops
    return drew;
}

bool screenInput (const clap_plugin* plugin, const melodee_screen_input_t* e)
{
    if (!e) return false;
    auto& p = of (plugin);
    switch (e->kind)
    {
        case MELODEE_SCREEN_NUDGE:
            p.screen.navigate (e->value);
            break;
        case MELODEE_SCREEN_PUSH:
            p.pressedAt.store (steadyMs(), std::memory_order_release);
            if (p.host->request_process) p.host->request_process (p.host);
            break;
        case MELODEE_SCREEN_WHEEL:
        {
            const int row = p.screen.firstRow();
            const auto& par = paramTable[row];
            const float before = p.values[row].load (std::memory_order_relaxed);
            const float step = par.kind == Kind::continuous ? (par.max - par.min) / (e->shift ? 1024.0f : 128.0f) : 1.0f;
            const float v = clampParam (par, before + e->value * step);
            if (v == before) return true;
            p.values[row].store (v, std::memory_order_relaxed);
            p.mainChanged.store (true, std::memory_order_release);
            p.outgoing.fetch_or (uint64_t (1) << row, std::memory_order_release);
            if (p.hostParams && p.hostParams->rescan) p.hostParams->rescan (p.host, CLAP_PARAM_RESCAN_VALUES);
            if (p.hostParams && p.hostParams->request_flush) p.hostParams->request_flush (p.host);
            if (p.host->request_process) p.host->request_process (p.host);
            break;
        }
        default: return false;
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

// Preset discovery: one provider, one location inside the plug-in, the presets of presets.h. Nothing to allocate:
// the provider is a constant.
const clap_preset_discovery_provider_descriptor presetProviderDescriptor { CLAP_VERSION, "com.hdslave.moonbass.presets",
                                                                           "hdslave MoonBass presets", "hdslave" };

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
} // namespace moonbass

extern "C" CLAP_EXPORT const clap_plugin_entry clap_entry { CLAP_VERSION, moonbass::entryInit, moonbass::entryDeinit, moonbass::entryFactory };
