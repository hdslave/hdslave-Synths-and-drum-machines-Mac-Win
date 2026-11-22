// Foundry: a sixteen-voice drum machine in the style of the 808, 909 and 606 as a CLAP plug-in, made for the Akai
// MPC Sample running Melodee. This file is the CLAP side: the factory, ports, parameters, state, presets, note names
// and the process call, which cuts the block at each event so every change lands on its frame. The sound is in
// voice.h, fx.h and engine.h.
#include "engine.h"
#include "params.h"
#include "presets.h"
#include "screen.h"

#include <clap/clap.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <new>

namespace foundry
{
namespace
{
const char* const features[] = { CLAP_PLUGIN_FEATURE_INSTRUMENT, CLAP_PLUGIN_FEATURE_DRUM_MACHINE, CLAP_PLUGIN_FEATURE_DRUM,
                                 CLAP_PLUGIN_FEATURE_STEREO, nullptr };

const clap_plugin_descriptor descriptor {
    CLAP_VERSION, "com.hdslave.foundry", "hdslave Foundry", "hdslave", "", "", "", FOUNDRY_VERSION,
    "Sixteen analog-style drum voices, each an 808, 909 or 606 style circuit with the TR-1000's controls and more, "
    "with reverb, delay, side chain, bus compression and an analog-style filter",
    features,
};

// The kit's pads as Melodee reads them from a drum plug-in's library: with this, a plug-in track on Foundry plays
// its pads as a kit (pads A01-A16 are notes 36-51, in padVoice's order) and names them. Kept in the binary by
// voicesJson() below; host.cpp checks it against the note names.
[[gnu::used]] const char drumMapJson[] =
    "{\"pad_layout\":\"drums\",\"name\":\"hdslave Foundry\",\"levels\":{"
    "\"bd\":{\"name\":\"Bass Drum\",\"note\":36},\"sd\":{\"name\":\"Snare Drum\",\"note\":37},"
    "\"cp\":{\"name\":\"Hand Clap\",\"note\":38},\"ch\":{\"name\":\"Closed Hat\",\"note\":39},"
    "\"b2\":{\"name\":\"Bass Drum 2\",\"note\":40},\"rs\":{\"name\":\"Rim Shot\",\"note\":41},"
    "\"cy\":{\"name\":\"Crash\",\"note\":42},\"oh\":{\"name\":\"Open Hat\",\"note\":43},"
    "\"lt\":{\"name\":\"Low Tom\",\"note\":44},\"mt\":{\"name\":\"Mid Tom\",\"note\":45},"
    "\"ht\":{\"name\":\"High Tom\",\"note\":46},\"cb\":{\"name\":\"Cowbell\",\"note\":47},"
    "\"rd\":{\"name\":\"Ride\",\"note\":48},\"cl\":{\"name\":\"Claves\",\"note\":49},"
    "\"ma\":{\"name\":\"Maracas\",\"note\":50},\"cg\":{\"name\":\"Conga\",\"note\":51}}}";
const char* voicesJson()
{
    const char* volatile json = drumMapJson;
    return json;
}

struct Plugin
{
    clap_plugin clap {};
    const clap_host* host = nullptr;
    // Every parameter's value. The audio thread writes it from events; the main thread reads it, and writes it when
    // a state is loaded. The engine notices those at the next block.
    std::atomic<float> values[paramCount];
    std::atomic<bool> mainChanged { false };
    float applied[paramCount] {};
    Engine engine;
    bool active = false;
    const clap_host_melodee_screen* hostScreen = nullptr;
    Screen screen;
    HitMeter meter;
    Auditions auditions;
    bool blockChanged = false;
    std::atomic<bool> paintPending { false };

    void askPaint()
    {
        if (hostScreen && !paintPending.exchange (true, std::memory_order_acq_rel))
            hostScreen->request_paint (host);
    }
};

Plugin& of (const clap_plugin* p) { return *static_cast<Plugin*> (p->plugin_data); }

// Parameters --------------------------------------------------------------------------------------------------

uint32_t paramsCount (const clap_plugin*) { return table().count; }

bool paramsInfo (const clap_plugin*, uint32_t index, clap_param_info* info)
{
    if (index >= table().count)
        return false;
    const auto& p = table().rows[index];
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
    formatParam (table().rows[index], (float) value, text, size);
    return true;
}

bool paramsFromText (const clap_plugin*, clap_id id, const char* text, double* value)
{
    const int index = paramIndex (id);
    if (index < 0)
        return false;
    float v = 0.0f;
    if (! parseParam (table().rows[index], text, v))
        return false;
    *value = v;
    return true;
}

// One event: parameter values go to the engine at once; notes play (or choke) their voice.
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
            const auto& info = table().rows[index];
            const float value = clampParam (info, (float) v->value);
            p.values[index].store (value, std::memory_order_relaxed);
            p.blockChanged |= p.applied[index] != value;
            p.applied[index] = value;
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
        case CLAP_EVENT_NOTE_CHOKE:
        {
            const auto* n = reinterpret_cast<const clap_event_note*> (e);
            if (n->key >= -1 && n->key < 128) // -1: every key
                p.engine.chokeKey (n->key);
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

void paramsFlush (const clap_plugin* plugin, const clap_input_events* in, const clap_output_events*)
{
    auto& p = of (plugin);
    const uint32_t count = in->size (in);
    for (uint32_t i = 0; i < count; ++i)
    {
        const auto* e = in->get (in, i);
        // Outside process the engine is not running: only values are kept.
        if (e->space_id == CLAP_CORE_EVENT_SPACE_ID && e->type == CLAP_EVENT_PARAM_VALUE)
        {
            const auto* v = reinterpret_cast<const clap_event_param_value*> (e);
            const int index = paramIndex (v->param_id);
            if (index >= 0)
            {
                p.values[index].store (clampParam (table().rows[index], (float) v->value), std::memory_order_relaxed);
                p.mainChanged.store (true, std::memory_order_release);
                p.askPaint();
            }
        }
    }
}

const clap_plugin_params paramsExtension { paramsCount, paramsInfo, paramsValue, paramsToText, paramsFromText, paramsFlush };

// State: "FNDY", a version, the number of values, then (id, value) pairs; unknown ids are skipped, missing ones keep
// their defaults, so a state saved by another version still loads.
constexpr char stateTag[4] = { 'F', 'N', 'D', 'Y' };
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
    p.askPaint();
    if (! p.active) // not processing: the engine takes them now; otherwise at the next block
        for (uint32_t i = 0; i < paramCount; ++i)
            p.applied[i] = values[i], p.engine.set (table().rows[i].id, values[i]);
}

bool stateSave (const clap_plugin* plugin, const clap_ostream* stream)
{
    auto& p = of (plugin);
    const uint32_t header[2] = { stateVersion, paramCount };
    if (! writeAll (stream, stateTag, 4) || ! writeAll (stream, header, sizeof (header)))
        return false;
    for (uint32_t i = 0; i < paramCount; ++i)
    {
        const struct { uint32_t id; float value; } pair { table().rows[i].id, p.values[i].load (std::memory_order_relaxed) };
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
        || header[0] == 0 || header[1] > 8192)
        return false;
    static float loaded[paramCount]; // main thread only
    for (uint32_t i = 0; i < paramCount; ++i)
        loaded[i] = table().rows[i].def;
    for (uint32_t i = 0; i < header[1]; ++i)
    {
        struct { uint32_t id; float value; } pair {};
        if (! readAll (stream, &pair, sizeof (pair)))
            return false;
        const int index = paramIndex (pair.id);
        if (index >= 0)
            loaded[index] = clampParam (table().rows[index], pair.value);
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
    static float values[paramCount]; // main thread only
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

// Ports: no audio in; the mix out (main), and the side-chain key out: the SC Source voice alone (the kick unless set
// otherwise), before its Level, for a host's compressor side-chain input. One note input (CLAP notes or MIDI).
uint32_t audioCount (const clap_plugin*, bool input) { return input ? 0 : 2; }
bool audioGet (const clap_plugin*, uint32_t index, bool input, clap_audio_port_info* info)
{
    if (input || index > 1)
        return false;
    *info = {};
    info->id = index;
    std::snprintf (info->name, sizeof (info->name), "%s", index == 0 ? "Out" : "SC Key");
    info->flags = index == 0 ? (uint32_t) CLAP_AUDIO_PORT_IS_MAIN : 0u;
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

// Note names: the pads' (Key Map: Pads).
uint32_t noteNameCount (const clap_plugin*) { return voiceCount; }
bool noteNameGet (const clap_plugin*, uint32_t index, clap_note_name* name)
{
    if (index >= (uint32_t) voiceCount)
        return false;
    *name = {};
    std::snprintf (name->name, sizeof (name->name), "%s", voiceSpecs[padVoice[index]].name);
    name->port = -1;
    name->key = (int16_t) (36 + index); // pad `index`
    name->channel = -1;
    return true;
}
const clap_plugin_note_name noteNameExtension { noteNameCount, noteNameGet };

// The plug-in -------------------------------------------------------------------------------------------------

bool pluginInit (const clap_plugin* plugin)
{
    auto& p = of (plugin);
    (void) voicesJson();
    p.hostScreen = static_cast<const clap_host_melodee_screen*> (p.host->get_extension (p.host, CLAP_EXT_MELODEE_SCREEN_HOST));
    if (p.hostScreen && p.hostScreen->request_paint)
    {
        p.engine.meterContext = &p;
        p.engine.meterStrike = [] (void* ctx, int voice, float strength) {
            auto& owner = *static_cast<Plugin*> (ctx);
            owner.meter.strike (voice, strength);
            owner.blockChanged = true;
        };
    }
    else p.hostScreen = nullptr;
    for (uint32_t i = 0; i < paramCount; ++i)
    {
        const auto& row = table().rows[i];
        p.values[i].store (row.def, std::memory_order_relaxed);
        p.applied[i] = row.def;
        p.engine.set (row.id, row.def);
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
void pluginStop (const clap_plugin* plugin) { of (plugin).auditions.discard(); }
void pluginReset (const clap_plugin* plugin)
{
    of (plugin).auditions.discard();
    of (plugin).engine.reset();
}

// Denormals: flush them to zero for the block (the filters and resonators decay into them), as the FPU had it after.
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
    float *keyL = nullptr, *keyR = nullptr;
    if (process->audio_outputs_count > 1 && process->audio_outputs[1].channel_count > 0)
    {
        keyL = process->audio_outputs[1].data32[0];
        keyR = process->audio_outputs[1].channel_count > 1 ? process->audio_outputs[1].data32[1] : nullptr;
    }

    // Values the main thread changed (a state loaded while playing): looked through only when it wrote some.
    if (p.mainChanged.exchange (false, std::memory_order_acquire))
        for (uint32_t i = 0; i < paramCount; ++i)
        {
            const float v = p.values[i].load (std::memory_order_relaxed);
            if (v != p.applied[i])
                p.applied[i] = v, p.engine.set (table().rows[i].id, v);
        }

    // The host's transport first: notes at the block's first frame read the song's place (chance, cycles).
    if (const auto* t = process->transport)
    {
        const bool playing = (t->flags & CLAP_TRANSPORT_IS_PLAYING) != 0;
        const bool placed = (t->flags & CLAP_TRANSPORT_HAS_BEATS_TIMELINE) != 0;
        const double bpm = (t->flags & CLAP_TRANSPORT_HAS_TEMPO) && t->tempo > 0.0 ? t->tempo : 120.0;
        const double beats = placed ? (double) t->song_pos_beats / (double) CLAP_BEATTIME_FACTOR : __builtin_nan ("");
        double bar = 4.0;
        if ((t->flags & CLAP_TRANSPORT_HAS_TIME_SIGNATURE) && t->tsig_num > 0 && t->tsig_denom > 0)
            bar = 4.0 * t->tsig_num / t->tsig_denom;
        p.engine.transport (playing, beats, bpm, bar);
    }
    else
        p.engine.transport (false, __builtin_nan (""), 0.0, 4.0);

    p.auditions.take ([&] (int v) { p.engine.playVoice (v, 104.0f / 127.0f, 0.0f, true); });

    const auto* in = process->in_events;
    const uint32_t count = in != nullptr ? in->size (in) : 0;
    uint32_t next = 0, done = 0;
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
        p.engine.render (left + done, right ? right + done : nullptr, until - done, keyL ? keyL + done : nullptr, keyR ? keyR + done : nullptr);
        done = until;
    }
    for (; next < count; ++next) // events stamped past the block (a host's mistake): applied at its end
        handle (p, in->get (in, next));
    if (p.blockChanged)
    {
        p.blockChanged = false;
        p.askPaint();
    }
    return CLAP_PROCESS_CONTINUE;
}

bool screenPaint (const clap_plugin* plugin, const melodee_screen_canvas* c)
{
    if (!c || !c->pixels || c->width != MELODEE_SCREEN_WIDTH || c->height != MELODEE_SCREEN_HEIGHT || c->stride < c->width) return false;
    auto& p = of (plugin);
    const bool changed = p.paintPending.exchange (false, std::memory_order_acq_rel);
    const bool drew = p.screen.paint (*c, p.values, p.meter, changed);
    // Melodee can suspend a quiet instrument while its two-second history is still travelling.
    if (p.screen.moving && p.hostScreen) p.hostScreen->request_paint (p.host);
    return drew;
}

bool screenInput (const clap_plugin* plugin, const melodee_screen_input* input)
{
    if (!input) return false;
    auto& p = of (plugin);
    if (input->kind == MELODEE_SCREEN_PUSH)
    {
        p.auditions.push (p.screen.voice());
        if (p.host->request_process) p.host->request_process (p.host);
    }
    else if (!p.screen.move (input->kind, input->value)) return false;
    // The page chooses bindings. Melodee writes all parameter values through the usual CLAP events.
    p.askPaint();
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
    if (std::strcmp (id, CLAP_EXT_NOTE_NAME) == 0) return &noteNameExtension;
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
const clap_preset_discovery_provider_descriptor presetProviderDescriptor { CLAP_VERSION, "com.hdslave.foundry.presets",
                                                                           "hdslave Foundry presets", "hdslave" };

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

bool entryInit (const char*)
{
    (void) table(); // the parameter table, built here rather than on the audio thread
    return true;
}
void entryDeinit() {}
const void* entryFactory (const char* id)
{
    if (std::strcmp (id, CLAP_PLUGIN_FACTORY_ID) == 0) return &factory;
    if (std::strcmp (id, CLAP_PRESET_DISCOVERY_FACTORY_ID) == 0 || std::strcmp (id, CLAP_PRESET_DISCOVERY_FACTORY_ID_COMPAT) == 0)
        return &presetFactory;
    return nullptr;
}
} // namespace
} // namespace foundry

extern "C" CLAP_EXPORT const clap_plugin_entry clap_entry { CLAP_VERSION, foundry::entryInit, foundry::entryDeinit, foundry::entryFactory };
