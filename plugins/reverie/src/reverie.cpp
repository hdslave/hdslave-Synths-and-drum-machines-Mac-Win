// Reverie: a pad and chord synthesizer for the Akai MPC Sample: the pads play chords from progressions (chords.h),
// voiced on a two-oscillator wavetable voice with Motion (engine.h) and lush effects (fx.h). This file is the CLAP
// side: the factory, ports, parameters, state, presets, notes and MIDI, Melodee's screen page and the process call.
#include "engine.h"
#include "melodee_screen.h"
#include "params.h"
#include "presets.h"
#include "screen.h"

#include <clap/clap.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <new>
#include <vector>

namespace reverie
{
namespace
{
const char* const features[] = { CLAP_PLUGIN_FEATURE_INSTRUMENT, CLAP_PLUGIN_FEATURE_SYNTHESIZER, CLAP_PLUGIN_FEATURE_STEREO, nullptr };

const clap_plugin_descriptor descriptor {
    CLAP_VERSION, "com.hdslave.reverie", "hdslave Reverie", "hdslave", "", "", "", REVERIE_VERSION,
    "Huge pads and lush chords: the pads play jazzy, soulful progressions, voiced and voice-led, on two morphing "
    "wavetable oscillators with an X / Y Motion path, ensemble, delay and a shimmering reverb, with its own page",
    features,
};

// The pads as Melodee reads them from a plug-in's library: with this, a plug-in track on Reverie plays its pads as
// notes 36-51 (pads A01-A16) whatever the key or octave, and names them. Kept in the binary by padsJson() below.
[[gnu::used]] const char padMapJson[] =
    "{\"pad_layout\":\"drums\",\"name\":\"hdslave Reverie\",\"levels\":{"
    "\"c1\":{\"name\":\"Chord 1\",\"note\":36},\"c2\":{\"name\":\"Chord 2\",\"note\":37},"
    "\"c3\":{\"name\":\"Chord 3\",\"note\":38},\"c4\":{\"name\":\"Chord 4\",\"note\":39},"
    "\"c5\":{\"name\":\"Chord 5\",\"note\":40},\"c6\":{\"name\":\"Chord 6\",\"note\":41},"
    "\"c7\":{\"name\":\"Chord 7\",\"note\":42},\"c8\":{\"name\":\"Chord 8\",\"note\":43},"
    "\"l1\":{\"name\":\"Lift 1\",\"note\":44},\"l2\":{\"name\":\"Lift 2\",\"note\":45},"
    "\"l3\":{\"name\":\"Lift 3\",\"note\":46},\"l4\":{\"name\":\"Lift 4\",\"note\":47},"
    "\"l5\":{\"name\":\"Lift 5\",\"note\":48},\"l6\":{\"name\":\"Lift 6\",\"note\":49},"
    "\"l7\":{\"name\":\"Lift 7\",\"note\":50},\"l8\":{\"name\":\"Lift 8\",\"note\":51}}}";
const char* padsJson()
{
    const char* volatile json = padMapJson;
    return json;
}

// A bit a parameter row, set from any thread, taken (and cleared) by the audio thread.
struct RowBits
{
    static constexpr int words = (paramCount + 63) / 64;
    std::atomic<uint64_t> word[words];
    RowBits() { for (auto& w : word) w.store (0, std::memory_order_relaxed); }
    void set (uint32_t row) { word[row >> 6].fetch_or (uint64_t (1) << (row & 63), std::memory_order_release); }
    bool any() const
    {
        for (const auto& w : word)
            if (w.load (std::memory_order_relaxed) != 0) return true;
        return false;
    }
    void clear() { for (auto& w : word) w.store (0, std::memory_order_relaxed); }
};

struct Plugin
{
    clap_plugin clap {};
    const clap_host* host = nullptr;
    // Every parameter's value. The audio thread writes it from events; the main thread reads it, and writes it when a
    // state or a sound is loaded or the page edits. The engine notices those at the next block.
    std::atomic<float> values[paramCount];
    std::atomic<bool> mainChanged { false };
    float applied[paramCount] {};
    Telemetry telemetry;
    Engine engine { &telemetry };
    bool active = false;
    std::vector<float> spare; // the right channel of a mono output
    const clap_host_params* hostParams = nullptr;
    const clap_host_melodee_screen* hostScreen = nullptr;
    Screen screen { values, telemetry };
    RowBits outgoing;
    bool valuesMoved = false;
    uint32_t shownStrikes = 0, shownSounding = 0, shownHeld = 0;
    // [audio thread] The keys of the last notes the host named by CLAP note id, for note-offs and chokes that give
    // only the id.
    struct NoteId { int32_t id = -1; int16_t key = -1; };
    NoteId noteIds[32];
    uint32_t noteIdNext = 0;
};

Plugin& of (const clap_plugin* p) { return *static_cast<Plugin*> (p->plugin_data); }

void requestPaint (const Plugin& p)
{
    if (p.hostScreen != nullptr && p.hostScreen->request_paint != nullptr)
        p.hostScreen->request_paint (p.host);
}

bool pushValue (const clap_output_events* out, uint32_t time, clap_id id, double value)
{
    if (out == nullptr)
        return false;
    clap_event_param_value e {};
    e.header = { sizeof (e), time, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_PARAM_VALUE, 0 };
    e.param_id = id;
    e.note_id = e.port_index = e.channel = e.key = -1;
    e.value = value;
    return out->try_push (out, &e.header);
}

// The values the page set, told to the host (its copy, its automation and its knobs follow). Retried if the queue is
// full.
void tellHost (Plugin& p, const clap_output_events* out, uint32_t time)
{
    if (out == nullptr || ! p.outgoing.any())
        return;
    for (int w = 0; w < RowBits::words; ++w)
    {
        uint64_t rows = p.outgoing.word[w].exchange (0, std::memory_order_acquire);
        while (rows != 0)
        {
            const uint32_t row = (uint32_t) (w * 64 + __builtin_ctzll (rows));
            rows &= rows - 1;
            if (! pushValue (out, time, paramTable[row].id, p.values[row].load (std::memory_order_relaxed)))
                p.outgoing.set (row);
        }
    }
}

// Parameters --------------------------------------------------------------------------------------------------

uint32_t paramsCount (const clap_plugin*) { return paramCount; }

bool paramsInfo (const clap_plugin*, uint32_t index, clap_param_info* info)
{
    if (index >= (uint32_t) paramCount)
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
    if (index < 0 || text == nullptr)
        return false;
    float v = 0.0f;
    if (! parseParam (paramTable[index], text, v))
        return false;
    *value = v;
    return true;
}

// MIDI: notes, pitch bend, and the controllers the engine reads: the mod wheel (1, moves Motion's Y), sustain (64)
// and the channel modes (120, 121, 123 at 0: they stop sound or notes and never move a parameter).
void midi (Plugin& p, const uint8_t* data)
{
    const int status = data[0] & 0xf0, a = data[1] & 0x7f, b = data[2] & 0x7f;
    switch (status)
    {
        case 0x90:
            if (b > 0) { p.engine.noteOn (a, (float) b / 127.0f); break; }
            [[fallthrough]];
        case 0x80: p.engine.noteOff (a); break;
        case 0xb0: p.engine.controller (a, b); break;
        case 0xe0: p.engine.pitchBend (((float) ((b << 7) | a) - 8192.0f) / 8192.0f); break;
        default: break;
    }
}

// The key a CLAP note event stands for: its own, or (key -1) the key of the note it names by id; -1 when it names
// neither (a wildcard: every key), -2 when it names an id Reverie does not know or a key out of range (ignored).
int keyOf (Plugin& p, const clap_event_note& n, bool forget)
{
    if (n.key >= 0 && n.key < 128) return n.key;
    if (n.key >= 0 || n.note_id < 0) return n.key < 0 ? -1 : -2;
    for (auto& s : p.noteIds)
        if (s.id == n.note_id)
        {
            const int key = s.key;
            if (forget) s.id = -1;
            return key;
        }
    return -2;
}

void handle (Plugin& p, const clap_event_header* e)
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
            const float value = clampParam (paramTable[index], (float) v->value);
            const bool moved = p.applied[index] != value;
            p.values[index].store (value, std::memory_order_relaxed);
            p.applied[index] = value;
            p.engine.set ((uint32_t) index, value);
            p.valuesMoved |= moved;
            break;
        }
        case CLAP_EVENT_NOTE_ON:
        {
            const auto* n = reinterpret_cast<const clap_event_note*> (e);
            if (n->key >= 0 && n->key < 128)
            {
                if (n->note_id >= 0) p.noteIds[p.noteIdNext++ % 32] = { n->note_id, (int16_t) n->key };
                if (n->velocity > 0.0) p.engine.noteOn (n->key, (float) std::min (1.0, n->velocity));
                else p.engine.noteOff (n->key);
            }
            break;
        }
        case CLAP_EVENT_NOTE_OFF:
        {
            const int key = keyOf (p, *reinterpret_cast<const clap_event_note*> (e), true);
            if (key != -2) p.engine.noteOff (key); // -1: every key
            break;
        }
        case CLAP_EVENT_NOTE_CHOKE:
        {
            const int key = keyOf (p, *reinterpret_cast<const clap_event_note*> (e), true);
            if (key != -2) p.engine.choke (key);
            break;
        }
        case CLAP_EVENT_MIDI: midi (p, reinterpret_cast<const clap_event_midi*> (e)->data); break;
        default: break;
    }
}

void paramsFlush (const clap_plugin* plugin, const clap_input_events* in, const clap_output_events* out)
{
    auto& p = of (plugin);
    const uint32_t count = in != nullptr ? in->size (in) : 0;
    bool changed = false;
    for (uint32_t i = 0; i < count; ++i)
    {
        const auto* e = in->get (in, i);
        if (e->space_id != CLAP_CORE_EVENT_SPACE_ID || e->type != CLAP_EVENT_PARAM_VALUE)
            continue;
        handle (p, e);
        changed = true;
    }
    tellHost (p, out, 0);
    if (changed)
        requestPaint (p);
}

const clap_plugin_params paramsExtension { paramsCount, paramsInfo, paramsValue, paramsToText, paramsFromText, paramsFlush };

// State: "RVRE", a version, the number of values, then (id, value) pairs; unknown ids are skipped, missing ones keep
// their defaults, so a state saved by another version still loads.
constexpr char stateTag[4] = { 'R', 'V', 'R', 'E' };
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

// Every value at once, from the main thread (a state or a sound loaded). Edits the page still had to tell are
// dropped: the loaded values win.
void setAll (Plugin& p, const float* values)
{
    p.outgoing.clear();
    for (int i = 0; i < paramCount; ++i)
        p.values[i].store (values[i], std::memory_order_relaxed);
    p.mainChanged.store (true, std::memory_order_release);
    if (! p.active)
        for (int i = 0; i < paramCount; ++i)
            p.applied[i] = values[i], p.engine.set ((uint32_t) i, values[i]);
    requestPaint (p);
}

bool stateSave (const clap_plugin* plugin, const clap_ostream* stream)
{
    auto& p = of (plugin);
    const uint32_t header[2] = { stateVersion, (uint32_t) paramCount };
    if (! writeAll (stream, stateTag, 4) || ! writeAll (stream, header, sizeof (header)))
        return false;
    for (int i = 0; i < paramCount; ++i)
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
    for (int i = 0; i < paramCount; ++i)
        loaded[i] = paramTable[i].def;
    for (uint32_t i = 0; i < header[1]; ++i)
    {
        struct { uint32_t id; float value; } pair {};
        if (! readAll (stream, &pair, sizeof (pair)))
            return false;
        const int index = paramIndex (pair.id);
        if (index >= 0)
            loaded[index] = clampParam (paramTable[index], pair.value);
    }
    setAll (p, loaded);
    return true;
}

const clap_plugin_state stateExtension { stateSave, stateLoad };

// Sounds (presets.h): inside the plug-in, listed by the preset-discovery factory and loaded by their key. A sound
// keeps the chord section as it is.
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
    for (int i = 0; i < paramCount; ++i) values[i] = p.values[i].load (std::memory_order_relaxed);
    presetValuesOf (*preset, values);
    setAll (p, values);
    if (p.hostParams != nullptr && p.hostParams->rescan != nullptr)
        p.hostParams->rescan (p.host, CLAP_PARAM_RESCAN_VALUES);
    if (hostPresets != nullptr && hostPresets->loaded != nullptr)
        hostPresets->loaded (p.host, kind, nullptr, key);
    return true;
}

const clap_plugin_preset_load presetExtension { presetFromLocation };

// Ports: one stereo out (Melodee plays a mono port on the left only), one note input (CLAP notes or MIDI).
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
    for (int i = 0; i < paramCount; ++i)
    {
        p.values[i].store (paramTable[i].def, std::memory_order_relaxed);
        p.applied[i] = paramTable[i].def;
        p.engine.set ((uint32_t) i, paramTable[i].def);
    }
    (void) WaveBank::get(); // the waves, made once (main thread)
    (void) padsJson();
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
    if (! (rate >= 8000.0 && rate <= 192000.0))
        return false;
    for (int i = 0; i < paramCount; ++i)
    {
        const float v = p.values[i].load (std::memory_order_relaxed);
        p.applied[i] = v;
        p.engine.set ((uint32_t) i, v);
    }
    p.mainChanged.store (false, std::memory_order_relaxed);
    p.spare.assign (std::max<uint32_t> (maxFrames, 1) + 16, 0.0f);
    p.engine.prepare (rate);
    p.active = true;
    return true;
}

void pluginDeactivate (const clap_plugin* plugin)
{
    auto& p = of (plugin);
    p.active = false;
    p.engine.deactivate();
}
bool pluginStart (const clap_plugin*) { return true; }
void pluginStop (const clap_plugin*) {}
void pluginReset (const clap_plugin* plugin) { of (plugin).engine.reset(); }

// Denormals: flushed to zero for the block (filters, envelopes and reverb tails decay into them).
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
    if (right == nullptr)
    {
        if (p.spare.size() < frames) return CLAP_PROCESS_ERROR;
        right = p.spare.data();
    }

    if (const auto* t = process->transport)
    {
        const bool playing = (t->flags & CLAP_TRANSPORT_IS_PLAYING) != 0;
        const bool placed = (t->flags & CLAP_TRANSPORT_HAS_BEATS_TIMELINE) != 0;
        const bool tempo = (t->flags & CLAP_TRANSPORT_HAS_TEMPO) && std::isfinite (t->tempo) && t->tempo > 0.0;
        p.engine.transport (playing && placed, placed ? (double) t->song_pos_beats / (double) CLAP_BEATTIME_FACTOR : 0.0,
                            tempo ? std::min (std::max (t->tempo, 20.0), 999.0) : 120.0);
    }
    if (p.mainChanged.exchange (false, std::memory_order_acquire))
        for (int i = 0; i < paramCount; ++i)
        {
            const float v = p.values[i].load (std::memory_order_relaxed);
            if (v != p.applied[i])
                p.applied[i] = v, p.engine.set ((uint32_t) i, v), p.valuesMoved = true;
        }
    tellHost (p, process->out_events, 0);

    // Each event on its own sample: render up to it, then take it.
    const auto* in = process->in_events;
    const uint32_t count = in != nullptr ? in->size (in) : 0;
    uint32_t at = 0;
    for (uint32_t i = 0; i < count; ++i)
    {
        const auto* e = in->get (in, i);
        const uint32_t time = std::min (e->time, frames);
        if (time > at)
        {
            p.engine.render (left + at, right + at, time - at);
            at = time;
        }
        handle (p, e);
    }
    if (at < frames)
        p.engine.render (left + at, right + at, frames - at);

    // The page: a chord struck, notes starting or ending, or a value the host moved; it paces its own animation.
    const uint32_t strikes = p.telemetry.strikes.load (std::memory_order_relaxed);
    const uint32_t sounding = p.telemetry.sounding.load (std::memory_order_relaxed);
    const uint32_t held = p.telemetry.padsHeld.load (std::memory_order_relaxed);
    if (p.valuesMoved || strikes != p.shownStrikes || sounding != p.shownSounding || held != p.shownHeld)
    {
        p.shownStrikes = strikes, p.shownSounding = sounding, p.shownHeld = held;
        requestPaint (p);
    }
    p.valuesMoved = false;
    return p.engine.quiet() && count == 0 ? CLAP_PROCESS_SLEEP : CLAP_PROCESS_CONTINUE;
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
    const bool drew = p.screen.paint (h, c->pixels, (int) c->stride, c->whole);
    if (p.screen.animating())
        requestPaint (p);
    return drew;
}

bool screenInput (const clap_plugin* plugin, const melodee_screen_input_t* e)
{
    if (e == nullptr || e->kind > MELODEE_SCREEN_NUDGE)
        return false;
    auto& p = of (plugin);
    hdui::Edits edits;
    const bool used = p.screen.input ((hdui::Key) e->kind, e->value, e->shift, edits);
    bool changed = false;
    for (int i = 0; i < edits.count; ++i)
    {
        const auto& edit = edits.list[i];
        const int index = paramIndex (edit.id);
        if (index < 0 || edit.press)
            continue;
        p.values[index].store (clampParam (paramTable[index], edit.value), std::memory_order_relaxed);
        p.outgoing.set ((uint32_t) index);
        p.mainChanged.store (true, std::memory_order_release);
        if (! p.active)
            p.applied[index] = p.values[index].load (std::memory_order_relaxed), p.engine.set ((uint32_t) index, p.applied[index]);
        changed = true;
    }
    if (changed && p.hostParams != nullptr)
    {
        if (p.hostParams->rescan != nullptr)
            p.hostParams->rescan (p.host, CLAP_PARAM_RESCAN_VALUES);
        if (p.hostParams->request_flush != nullptr)
            p.hostParams->request_flush (p.host);
    }
    if (changed && p.host != nullptr && p.host->request_process != nullptr)
        p.host->request_process (p.host);
    if (used)
        requestPaint (p);
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

void pluginMainThread (const clap_plugin*) {}

// Factory -----------------------------------------------------------------------------------------------------

uint32_t factoryCount (const clap_plugin_factory*) { return 1; }
const clap_plugin_descriptor* factoryDescriptor (const clap_plugin_factory*, uint32_t index) { return index == 0 ? &descriptor : nullptr; }

const clap_plugin* factoryCreate (const clap_plugin_factory*, const clap_host* host, const char* id)
{
    if (host == nullptr || id == nullptr || ! clap_version_is_compatible (host->clap_version) || std::strcmp (id, descriptor.id) != 0)
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

// Preset discovery: one provider, one location inside the plug-in, the sounds of presets.h.
const clap_preset_discovery_provider_descriptor presetProviderDescriptor { CLAP_VERSION, "com.hdslave.reverie.presets",
                                                                           "hdslave Reverie sounds", "hdslave" };

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
} // namespace reverie

extern "C" CLAP_EXPORT const clap_plugin_entry clap_entry { CLAP_VERSION, reverie::entryInit, reverie::entryDeinit, reverie::entryFactory };
