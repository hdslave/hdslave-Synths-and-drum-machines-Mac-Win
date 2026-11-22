// hdslave Machine: a multi-machine synth (an FM voice, drum machines, a kit of them, and a barrage of stranger voices)
// as a CLAP plug-in, made for the Akai MPC Sample. This file is the CLAP side: the factory, ports, parameters, state, presets and the process
// call, which cuts the block at each event so every change lands on its frame. The sound is in engine.h.
#include "engine.h"
#include "params.h"
#include "presets.h"
#if MACHINE_SCREEN
#include "machine_ui.h"
#include "melodee_screen.h"
#endif

#include <clap/clap.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <new>

namespace machine
{
namespace
{
const char* const features[] = { CLAP_PLUGIN_FEATURE_INSTRUMENT, CLAP_PLUGIN_FEATURE_SYNTHESIZER, CLAP_PLUGIN_FEATURE_DRUM,
                                 CLAP_PLUGIN_FEATURE_DRUM_MACHINE, CLAP_PLUGIN_FEATURE_STEREO, nullptr };

const clap_plugin_descriptor descriptor {
    CLAP_VERSION, "com.hdslave.machine", "hdslave Machine", "hdslave", "", "", "", MACHINE_VERSION,
    "Eighteen machines: a four-operator FM voice, drum machines and a kit of eight, wave terrain, phaseshaping, VOSIM, "
    "modal, plucked string, dynamic stochastic, pulsar, organ and a resonator kick, each through a filter, drive and "
    "an amp envelope",
    features,
};

struct Plugin
{
    clap_plugin clap {};
    const clap_host* host = nullptr;
    // Every parameter's value, by table row. The audio thread writes it from events; the main thread reads it, and
    // writes it when a state or preset is loaded. The engine notices those at the next block.
    std::atomic<float> values[paramCount];
    std::atomic<bool> mainChanged { false };
    // The main thread's writes come as a set (a state, a preset, an edit and the Data its machine resets): odd while
    // one is being written, so the audio thread never takes half of one (a seqlock).
    std::atomic<uint32_t> mainSeq { 0 };
    float applied[paramCount] {};
    Engine engine;
    bool active = false;
    // The audio thread's, for the block in hand: a machine changed (its Data reset and told), and the block's last
    // frame (events stamped past it answered there).
    bool resetThisBlock = false;
    uint32_t lastFrame = 0;
    // The machine before KIT: going back to it from KIT keeps its Data as they were. A state keeps it; a preset, or a
    // state from before it was kept, leaves it unknown (kindCount), so leaving KIT then resets Data.
    std::atomic<uint8_t> beforeKit { fmTone };
#if MACHINE_SCREEN
    // Melodee's screen (melodee_screen.h, ui/): the page, its model, and what it asks of the audio side.
    const clap_host_melodee_screen* screenHost = nullptr;
    ui::Screen screen;
    ui::Model model {};
    int slotPicked = 0;            // KIT: the slot shown (the last pad played, or SLOT)
    uint32_t hitsSeen = 0;
    std::atomic<bool> animating { true };
    uint32_t sincePaintAsk = 0;    // frames since the audio side last asked for a paint
    std::atomic<float> scope[128] {};
#endif
};

Plugin& of (const clap_plugin* p) { return *static_cast<Plugin*> (p->plugin_data); }

float valueOf (const Plugin& p, uint32_t id)
{
    const int index = paramIndex (id);
    return index >= 0 ? p.values[index].load (std::memory_order_relaxed) : 0.0f;
}
uint8_t machineOf (const Plugin& p) { return (uint8_t) std::min<int> ((int) valueOf (p, ParamId::machine), kindCount - 1); }
uint8_t slotMachineOf (const Plugin& p, uint32_t slot)
{
    return slotKinds[std::min<int> ((int) valueOf (p, kitId (slot, ParamId::kitMachine)), slotKindCount - 1)];
}

// A value the plug-in set itself, told to the host. Not a move of the user's: a recording host leaves it out.
void pushValue (const clap_output_events* out, uint32_t time, uint32_t id, float value)
{
    if (out == nullptr) return;
    clap_event_param_value e {};
    e.header = { sizeof (e), time, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_PARAM_VALUE, CLAP_EVENT_DONT_RECORD };
    e.param_id = id;
    e.note_id = e.port_index = e.channel = e.key = -1;
    e.value = value;
    out->try_push (out, &e.header);
}

// Stores a value; on the audio thread (`live`) the engine takes it at once. The main thread's go between
// beginMain() and endMain(), and the audio thread takes them at its next block.
void store (Plugin& p, uint32_t id, float value, bool live)
{
    const int index = paramIndex (id);
    if (index < 0) return;
    p.values[index].store (value, std::memory_order_relaxed);
    if (live)
    {
        p.applied[index] = value;
        p.engine.set (id, value);
    }
}
void beginMain (Plugin& p)
{
    p.mainSeq.fetch_add (1, std::memory_order_acq_rel);
    std::atomic_thread_fence (std::memory_order_release); // the set's values are stored after the odd count, never before
}
void endMain (Plugin& p)
{
    p.mainSeq.fetch_add (1, std::memory_order_release);
    p.mainChanged.store (true, std::memory_order_release);
}

// A machine changed by hand or automation: its Data start from the new machine's defaults (as on the hardware), and
// the host hears of each. A state or preset sets Data itself, so it never comes here.
void newMachine (Plugin& p, uint32_t id, float before, float after, uint32_t time, const clap_output_events* out, bool live)
{
    if ((int) before == (int) after) return;
    uint32_t slot = 0, field = 0;
    if (id == ParamId::machine)
    {
        const uint8_t kind = (uint8_t) std::min<int> ((int) after, kindCount - 1), was = (uint8_t) std::min<int> ((int) before, kindCount - 1);
        if (kind == kit) // KIT's Data are the slots': Data A-J keep what they had, for the machine left
        {
            p.beforeKit.store (was, std::memory_order_relaxed);
            return;
        }
        if (was == kit && kind == p.beforeKit.load (std::memory_order_relaxed)) return; // back to it from KIT, as it was
        if (live) p.resetThisBlock = true;
        for (uint32_t k = 0; k < dataCount; ++k)
        {
            const float def = dataSpec (kind, k).used() ? dataSpec (kind, k).def : 0.0f;
            store (p, dataId (k), def, live);
            pushValue (out, time, dataId (k), def);
        }
    }
    else if (kitField (id, slot, field) && field == ParamId::kitMachine)
    {
        const uint8_t kind = slotKinds[std::min<int> ((int) after, slotKindCount - 1)];
        if (live) p.resetThisBlock = true;
        for (uint32_t k = 0; k < dataCount; ++k)
        {
            const float def = dataSpec (kind, k).used() ? dataSpec (kind, k).def : 0.0f;
            store (p, kitDataId (slot, k), def, live);
            pushValue (out, time, kitDataId (slot, k), def);
        }
    }
}

// Parameters --------------------------------------------------------------------------------------------------

uint32_t paramsCount (const clap_plugin*) { return paramCount; }

bool paramsInfo (const clap_plugin*, uint32_t index, clap_param_info* info)
{
    if (index >= paramCount) return false;
    const auto& p = paramAt (index);
    *info = {};
    info->id = p.id;
    info->flags = CLAP_PARAM_IS_AUTOMATABLE | (p.kind == ParamKind::choice ? (uint32_t) (CLAP_PARAM_IS_STEPPED | CLAP_PARAM_IS_ENUM) : 0u);
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
    if (index < 0) return false;
    *value = of (plugin).values[index].load (std::memory_order_relaxed);
    return true;
}

bool paramsToText (const clap_plugin* plugin, clap_id id, double value, char* text, uint32_t size)
{
    const int index = paramIndex (id);
    if (index < 0 || size == 0) return false;
    const auto& p = of (plugin);
    uint32_t slot = 0, field = 0;
    const uint8_t slotKind = kitField (id, slot, field) ? slotMachineOf (p, slot) : (uint8_t) bd;
    formatParam (paramAt ((uint32_t) index), (float) value, machineOf (p), slotKind, text, size);
    return true;
}

bool paramsFromText (const clap_plugin* plugin, clap_id id, const char* text, double* value)
{
    const int index = paramIndex (id);
    if (index < 0) return false;
    const auto& p = of (plugin);
    const Param& par = paramAt ((uint32_t) index);
    float v = 0.0f;
    // Data A-J read in their machine's units (a KIT's own Data are plain numbers, as they show).
    uint32_t slot = 0, field = 0;
    const int k = dataOf (id);
    const uint8_t kind = par.text == Text::kitData && kitField (id, slot, field) ? slotMachineOf (p, slot) : machineOf (p);
    if (k >= 0 && kind != kit)
    {
        if (! parseData (dataSpec (kind, (uint32_t) k), text, v)) return false;
    }
    else if (! parseParam (par, text, v))
        return false;
    *value = v;
    return true;
}

// MIDI from a port that speaks it: notes, bend (+-2 semitones), the mod wheel (into the LFO) and all notes off.
void midi (Plugin& p, const uint8_t data[3])
{
    const uint8_t status = data[0] & 0xf0;
    if (status == 0x90 && data[2] > 0) p.engine.noteOn (data[1] & 0x7f, (float) (data[2] & 0x7f) / 127.0f);
    else if (status == 0x80 || (status == 0x90 && data[2] == 0)) p.engine.noteOff (data[1] & 0x7f);
    else if (status == 0xe0) p.engine.bend ((float) (((data[2] & 0x7f) << 7 | (data[1] & 0x7f)) - 8192) / 8192.0f * 2.0f);
    else if (status == 0xb0 && data[1] == 1) p.engine.modWheel ((float) (data[2] & 0x7f) / 127.0f);
    else if (status == 0xb0 && data[1] == 123) for (int k = 0; k < 128; ++k) p.engine.noteOff (k); // all notes off: released
    else if (status == 0xb0 && data[1] == 120) p.engine.allNotesOff();                                // all sound off: choked
}

void handle (Plugin& p, const clap_event_header* e, const clap_output_events* out)
{
    if (e->space_id != CLAP_CORE_EVENT_SPACE_ID) return;
    const uint32_t time = std::min (e->time, p.lastFrame); // answers inside the block, even for an event stamped past it
    switch (e->type)
    {
        case CLAP_EVENT_PARAM_VALUE:
        {
            const auto* v = reinterpret_cast<const clap_event_param_value*> (e);
            const int index = paramIndex (v->param_id);
            if (index < 0) break;
            const auto& info = paramAt ((uint32_t) index);
            const float value = clampParam (info, (float) v->value);
            const float before = p.applied[index];
            store (p, info.id, value, true);
            if (info.text == Text::machine || info.text == Text::kitMachine) newMachine (p, info.id, before, value, time, out, true);
            // A Data value the host sets after a machine's reset in the same block: told back, so the host's copy is
            // this value and not the reset's default it heard a moment before.
            else if (p.resetThisBlock && dataOf (info.id) >= 0) pushValue (out, time, info.id, value);
            break;
        }
        case CLAP_EVENT_NOTE_ON:
        {
            const auto* n = reinterpret_cast<const clap_event_note*> (e);
            if (n->key >= 0 && n->key < 128)
            {
                if (n->velocity > 0.0) p.engine.noteOn (n->key, (float) n->velocity, n->note_id);
                else p.engine.noteOff (n->key);
            }
            break;
        }
        // An off or a choke with a note id (a host that gives them) is that note's alone; -1 (Melodee's) any of the key's.
        case CLAP_EVENT_NOTE_OFF:
        {
            const auto* n = reinterpret_cast<const clap_event_note*> (e);
            if (n->key >= 0 && n->key < 128) p.engine.noteOff (n->key, n->note_id);
            else if (n->key < 0) for (int k = 0; k < 128; ++k) p.engine.noteOff (k, n->note_id);
            break;
        }
        case CLAP_EVENT_NOTE_CHOKE:
        {
            const auto* n = reinterpret_cast<const clap_event_note*> (e);
            p.engine.chokeKey (n->key, n->note_id);
            break;
        }
        case CLAP_EVENT_MIDI: midi (p, reinterpret_cast<const clap_event_midi*> (e)->data); break;
        default: break;
    }
}

void paramsFlush (const clap_plugin* plugin, const clap_input_events* in, const clap_output_events* out)
{
    auto& p = of (plugin);
    const uint32_t count = in->size (in);
    bool reset = false;
    beginMain (p);
    for (uint32_t i = 0; i < count; ++i)
    {
        const auto* e = in->get (in, i);
        if (e->space_id != CLAP_CORE_EVENT_SPACE_ID || e->type != CLAP_EVENT_PARAM_VALUE) continue;
        const auto* v = reinterpret_cast<const clap_event_param_value*> (e);
        const int index = paramIndex (v->param_id);
        if (index < 0) continue;
        const auto& info = paramAt ((uint32_t) index);
        const float value = clampParam (info, (float) v->value);
        const float before = p.values[index].load (std::memory_order_relaxed);
        store (p, info.id, value, false);
        if (info.text == Text::machine || info.text == Text::kitMachine)
        {
            newMachine (p, info.id, before, value, 0, out, false);
            reset |= (int) before != (int) value;
        }
        else if (reset && dataOf (info.id) >= 0) pushValue (out, 0, info.id, value); // as handle() does
    }
    endMain (p);
}

const clap_plugin_params paramsExtension { paramsCount, paramsInfo, paramsValue, paramsToText, paramsFromText, paramsFlush };

#if MACHINE_SCREEN
// Melodee's screen ----------------------------------------------------------------------------------------------
// The page (ui/) draws a Model: the machine across the top, then one row a parameter, grouped. Its rows are built
// here from the values each time it is asked to paint, take an input or name K1-K3 (main-thread calls, one at a
// time). Its edits are values from the main thread, as a state's are: the engine takes them at its next block, and
// the host is told to read them again.
constexpr uint32_t slotRowId = 0xffffff00u; // KIT's SLOT row: which slot the page shows, not a parameter

void addRow (Plugin& p, uint32_t id, ui::Group group, ui::Role role, uint8_t data, const char* name, uint16_t steps, bool bipolar,
             float initial, uint8_t machineKind, uint8_t slotKind)
{
    auto& m = p.model;
    const int index = paramIndex (id);
    if (index < 0 || m.rowCount >= 40) return;
    const Param& par = paramAt ((uint32_t) index);
    const float v = p.values[index].load (std::memory_order_relaxed), span = par.max - par.min;
    auto& row = m.rows[m.rowCount++];
    row = {};
    row.id = id;
    row.group = group;
    row.role = role;
    row.data = data;
    std::snprintf (row.name, sizeof (row.name), "%s", name);
    formatParam (par, v, machineKind, slotKind, row.text, sizeof (row.text));
    row.value = span > 0.0f ? (v - par.min) / span : 0.0f;
    row.initial = span > 0.0f ? (initial - par.min) / span : 0.0f;
    row.bipolar = bipolar;
    row.steps = steps;
}

void addShared (Plugin& p, ui::Group group, ui::Role role, uint32_t id, uint8_t machineKind)
{
    const Param& par = paramAt ((uint32_t) paramIndex (id));
    addRow (p, id, group, role, 255, par.screen, par.kind == ParamKind::choice ? (uint16_t) (par.max - par.min + 1.0f) : 0, par.min < 0.0f,
            par.def, machineKind, bd);
}

// Data A-J a machine uses, as rows (`slot` -1 the main machine's).
void addData (Plugin& p, uint8_t kind, int slot, ui::Group group, uint8_t machineKind)
{
    for (uint32_t k = 0; k < dataCount; ++k)
    {
        const DataSpec& d = dataSpec (kind, k);
        if (! d.used()) continue;
        const uint32_t id = slot >= 0 ? kitDataId ((uint32_t) slot, k) : dataId (k);
        addRow (p, id, group, ui::Role::Data, (uint8_t) k, d.name, d.steps, d.bipolar() || d.unit == Unit::semis, d.def, machineKind, kind);
    }
}

void fillModel (Plugin& p)
{
    using namespace ParamId;
    auto& m = p.model;
    m.rowCount = 0;
    const uint8_t kind = machineOf (p);
    m.machine = kind;
    // KIT: the slot the last pad played, unless SLOT picked another since.
    const uint32_t hits = p.engine.kitHits();
    if (hits != p.hitsSeen) p.hitsSeen = hits, p.slotPicked = p.engine.slotShown();
    m.kitSlot = (uint8_t) p.slotPicked;
    for (uint32_t s = 0; s < kitSlots; ++s) m.kitMachines[s] = slotMachineOf (p, s);
    for (uint32_t i = 0; i < 8; ++i) m.voices[i] = p.engine.meter (i);
    for (uint32_t i = 0; i < 128; ++i) m.scope[i] = p.scope[i].load (std::memory_order_relaxed);

    addRow (p, ParamId::machine, ui::Group::Machine, ui::Role::Machine, 255, "MACHINE", kindCount, false, 0.0f, kind, bd);
    if (kind == kit)
    {
        const uint32_t s = (uint32_t) p.slotPicked;
        const uint8_t slotKind = slotMachineOf (p, s);
        if (m.rowCount < 40)
        {
            auto& row = m.rows[m.rowCount++];
            row = {};
            row.id = slotRowId;
            row.group = ui::Group::Slot;
            row.role = ui::Role::KitSlot;
            row.data = 255;
            std::snprintf (row.name, sizeof (row.name), "SLOT");
            std::snprintf (row.text, sizeof (row.text), "%u", s + 1);
            row.value = (float) s / (float) (kitSlots - 1);
            row.steps = kitSlots;
        }
        addRow (p, kitId (s, kitMachine), ui::Group::Slot, ui::Role::KitMachine, 255, "MACH", slotKindCount, false,
                (float) slotIndexOf (kitDefaultMachines[s]), kind, slotKind);
        addData (p, slotKind, (int) s, ui::Group::Slot, kind);
        addRow (p, kitId (s, kitLevel), ui::Group::Slot, ui::Role::Level, 255, "LEVEL", 0, false, 0.8f, kind, slotKind);
        addRow (p, kitId (s, kitPan), ui::Group::Slot, ui::Role::Pan, 255, "PAN", 0, true, 0.0f, kind, slotKind);
    }
    else
        addData (p, kind, -1, ui::Group::Machine, kind);
    if (kind == fmTone)
    {
        const ui::Role roles[4] = { ui::Role::FmAttack, ui::Role::FmDecay, ui::Role::FmEnd, ui::Role::FmLevel };
        for (uint32_t k = 0; k < 8; ++k)
        {
            const Param& par = paramAt ((uint32_t) paramIndex (fmAAttack + k));
            addRow (p, fmAAttack + k, ui::Group::FmEnv, roles[k % 4], (uint8_t) (k / 4), par.screen, 0, false, par.def, kind, bd);
        }
    }
    addShared (p, ui::Group::Filter, ui::Role::FilterFreq, filterFreq, kind);
    addShared (p, ui::Group::Filter, ui::Role::FilterReso, filterReso, kind);
    addShared (p, ui::Group::Filter, ui::Role::FilterType, filterType, kind);
    addShared (p, ui::Group::Filter, ui::Role::FilterEnv, filterEnv, kind);
    addShared (p, ui::Group::Filter, ui::Role::FilterAttack, filterAttack, kind);
    addShared (p, ui::Group::Filter, ui::Role::FilterDecay, filterDecay, kind);
    addShared (p, ui::Group::Amp, ui::Role::AmpAttack, ampAttack, kind);
    addShared (p, ui::Group::Amp, ui::Role::AmpHold, ampHold, kind);
    addShared (p, ui::Group::Amp, ui::Role::AmpDecay, ampDecay, kind);
    addShared (p, ui::Group::Amp, ui::Role::AmpRelease, ampRelease, kind);
    addShared (p, ui::Group::Amp, ui::Role::AmpDrive, ampDrive, kind);
    addShared (p, ui::Group::Amp, ui::Role::Volume, volume, kind);
    addShared (p, ui::Group::Lfo, ui::Role::LfoSpeed, lfoSpeed, kind);
    addShared (p, ui::Group::Lfo, ui::Role::LfoDepth, lfoDepth, kind);
    addShared (p, ui::Group::Lfo, ui::Role::LfoWave, lfoWave, kind);
    addShared (p, ui::Group::Lfo, ui::Role::LfoDest, lfoDest, kind);
}

// An edit from the page: a value (0..1 of its range), or SLOT.
void applyEdit (Plugin& p, const ui::Edit& e)
{
    if (e.id == slotRowId)
    {
        p.slotPicked = std::min<int> ((int) kitSlots - 1, std::max (0, (int) std::lround (e.value * (float) (kitSlots - 1))));
        return;
    }
    const int index = paramIndex (e.id);
    if (index < 0) return;
    const Param& par = paramAt ((uint32_t) index);
    const float value = clampParam (par, par.min + std::min (1.0f, std::max (0.0f, e.value)) * (par.max - par.min));
    const float before = p.values[index].load (std::memory_order_relaxed);
    store (p, e.id, value, false);
    if (par.text == Text::machine || par.text == Text::kitMachine) newMachine (p, e.id, before, value, 0, nullptr, false);
}

bool screenPaint (const clap_plugin* plugin, const melodee_screen_canvas_t* c)
{
    auto& p = of (plugin);
    if (c == nullptr || c->pixels == nullptr || c->width != (uint32_t) ui::width || c->height != (uint32_t) ui::height) return false;
    fillModel (p);
    ui::Host h {};
    h.nowMs = c->now_ms;
    h.accent = c->accent;
    for (int k = 0; k < 3; ++k) h.pot[k] = c->knobs[k].position, h.engaged[k] = c->knobs[k].engaged;
    h.shift = c->shift;
    h.playing = c->playing;
    const bool drew = p.screen.paint (p.model, h, c->pixels, (int) c->stride, c->whole);
    const bool moving = p.screen.animating (p.model, h);
    p.animating.store (moving, std::memory_order_relaxed);
    // Still moving (the machine's sweep, the cursor's glide): the next paint asked for from here. The audio side
    // asks too while it sounds, but a silent instrument is not processed at all in Melodee (its idle gate).
    if (moving && p.screenHost != nullptr) p.screenHost->request_paint (p.host);
    return drew;
}

bool screenInput (const clap_plugin* plugin, const melodee_screen_input_t* in)
{
    auto& p = of (plugin);
    if (in == nullptr || in->kind > MELODEE_SCREEN_NUDGE) return false;
    fillModel (p);
    ui::Edit edits[4];
    int count = 0;
    const bool used = p.screen.input (p.model, (ui::Key) in->kind, in->value, in->shift, edits, count);
    beginMain (p);
    for (int i = 0; i < std::min (count, 4); ++i) applyEdit (p, edits[i]);
    endMain (p);
    if (count > 0)
        if (const auto* hostParams = static_cast<const clap_host_params*> (p.host->get_extension (p.host, CLAP_EXT_PARAMS)))
            if (hostParams->rescan != nullptr) hostParams->rescan (p.host, CLAP_PARAM_RESCAN_VALUES | CLAP_PARAM_RESCAN_TEXT);
    p.animating.store (true, std::memory_order_relaxed);
    return used;
}

void screenKnobs (const clap_plugin* plugin, clap_id ids[3])
{
    auto& p = of (plugin);
    fillModel (p);
    uint32_t knobs[3] = { 0xffffffffu, 0xffffffffu, 0xffffffffu };
    p.screen.knobs (p.model, knobs);
    for (int k = 0; k < 3; ++k) ids[k] = knobs[k] == 0xffffffffu || knobs[k] == slotRowId ? CLAP_INVALID_ID : (clap_id) knobs[k];
}

const clap_plugin_melodee_screen screenExtension { screenPaint, screenInput, screenKnobs };
#endif


// State: "MACH", a version, the number of values, then (id, value) pairs; unknown ids are skipped and missing ones
// keep their defaults, so a state saved by another version still loads.
constexpr char stateTag[4] = { 'M', 'A', 'C', 'H' };
constexpr uint32_t stateVersion = 1;
// Not a parameter: the machine KIT was entered from, saved after the parameters (a loader that doesn't know the id
// skips it).
constexpr uint32_t beforeKitId = 0x10000;

bool writeAll (const clap_ostream* s, const void* data, uint64_t size)
{
    const auto* at = static_cast<const uint8_t*> (data);
    while (size > 0)
    {
        const int64_t n = s->write (s, at, size);
        if (n <= 0) return false;
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
        if (n <= 0) return false;
        at += n;
        size -= (uint64_t) n;
    }
    return true;
}

// A new picture wanted on Melodee's screen (a state or preset loaded: the page and K1-K3 change with it).
void askPaint (Plugin& p)
{
#if MACHINE_SCREEN
    if (p.screenHost != nullptr) p.screenHost->request_paint (p.host);
#else
    (void) p;
#endif
}

// Every value at once, from the main thread (a state or a preset loaded), and the machine KIT was entered from
// (kindCount: unknown). The machine first, so the engine never resets what comes with it.
void setAll (Plugin& p, const float* values, uint8_t before)
{
    p.beforeKit.store (before, std::memory_order_relaxed);
    beginMain (p);
    for (uint32_t i = 0; i < paramCount; ++i) p.values[i].store (values[i], std::memory_order_relaxed);
    endMain (p);
    if (! p.active) // not processing: the engine takes them now; otherwise at the next block
        for (uint32_t i = 0; i < paramCount; ++i) p.applied[i] = values[i], p.engine.set (paramAt (i).id, values[i]);
}

bool stateSave (const clap_plugin* plugin, const clap_ostream* stream)
{
    auto& p = of (plugin);
    const uint32_t header[2] = { stateVersion, paramCount + 1 };
    if (! writeAll (stream, stateTag, 4) || ! writeAll (stream, header, sizeof (header))) return false;
    for (uint32_t i = 0; i < paramCount; ++i)
    {
        const struct { uint32_t id; float value; } pair { paramAt (i).id, p.values[i].load (std::memory_order_relaxed) };
        if (! writeAll (stream, &pair, sizeof (pair))) return false;
    }
    const struct { uint32_t id; float value; } before { beforeKitId, (float) p.beforeKit.load (std::memory_order_relaxed) };
    return writeAll (stream, &before, sizeof (before));
}

bool stateLoad (const clap_plugin* plugin, const clap_istream* stream)
{
    auto& p = of (plugin);
    char tag[4] = {};
    uint32_t header[2] = {};
    if (! readAll (stream, tag, 4) || std::memcmp (tag, stateTag, 4) != 0 || ! readAll (stream, header, sizeof (header)) || header[0] == 0
        || header[1] > 4096)
        return false;
    float loaded[paramCount];
    for (uint32_t i = 0; i < paramCount; ++i) loaded[i] = paramAt (i).def;
    uint8_t before = kindCount;
    for (uint32_t i = 0; i < header[1]; ++i)
    {
        struct { uint32_t id; float value; } pair {};
        if (! readAll (stream, &pair, sizeof (pair))) return false;
        if (pair.id == beforeKitId)
        {
            if (pair.value >= 0.0f && pair.value < (float) kindCount && (uint8_t) pair.value != kit) before = (uint8_t) pair.value;
            continue;
        }
        const int index = paramIndex (pair.id);
        if (index >= 0) loaded[index] = clampParam (paramAt ((uint32_t) index), pair.value);
    }
    setAll (p, loaded, before);
    askPaint (p);
    return true;
}

const clap_plugin_state stateExtension { stateSave, stateLoad };

// Presets (presets.h): inside the plug-in, listed by the preset-discovery factory below and loaded by their key.
bool presetFromLocation (const clap_plugin* plugin, uint32_t kind, const char*, const char* key)
{
    auto& p = of (plugin);
    const Preset* preset = kind == CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN ? findPreset (key) : nullptr;
    const auto* hostPresets = static_cast<const clap_host_preset_load*> (p.host->get_extension (p.host, CLAP_EXT_PRESET_LOAD));
    if (preset == nullptr)
    {
        if (hostPresets != nullptr && hostPresets->on_error != nullptr) hostPresets->on_error (p.host, kind, nullptr, key, 0, "no such preset");
        return false;
    }
    float values[paramCount];
    presetValuesOf (*preset, values);
    setAll (p, values, kindCount);
    askPaint (p);
    if (const auto* hostParams = static_cast<const clap_host_params*> (p.host->get_extension (p.host, CLAP_EXT_PARAMS)))
        if (hostParams->rescan != nullptr) hostParams->rescan (p.host, CLAP_PARAM_RESCAN_VALUES | CLAP_PARAM_RESCAN_TEXT);
    if (hostPresets != nullptr && hostPresets->loaded != nullptr) hostPresets->loaded (p.host, kind, nullptr, key);
    return true;
}

const clap_plugin_preset_load presetExtension { presetFromLocation };

// Ports: no audio in, one stereo out (KIT pans its slots), one note input.
uint32_t audioCount (const clap_plugin*, bool input) { return input ? 0 : 1; }
bool audioGet (const clap_plugin*, uint32_t index, bool input, clap_audio_port_info* info)
{
    if (input || index != 0) return false;
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
    if (! input || index != 0) return false;
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
#if MACHINE_SCREEN
    p.screenHost = static_cast<const clap_host_melodee_screen*> (p.host->get_extension (p.host, CLAP_EXT_MELODEE_SCREEN_HOST));
#endif
    for (uint32_t i = 0; i < paramCount; ++i)
    {
        p.values[i].store (paramAt (i).def, std::memory_order_relaxed);
        p.applied[i] = paramAt (i).def;
        p.engine.set (paramAt (i).id, paramAt (i).def);
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

// Denormals: flushed to zero for the block (filters and envelopes decay into them), the FPU as it was after.
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
    if (process->audio_outputs_count < 1 || process->audio_outputs[0].channel_count < 1) return CLAP_PROCESS_ERROR;
    float* left = process->audio_outputs[0].data32[0];
    float* right = process->audio_outputs[0].channel_count > 1 ? process->audio_outputs[0].data32[1] : nullptr;

    p.resetThisBlock = false;
    p.lastFrame = frames > 0 ? frames - 1 : 0;
    // Values the main thread changed (a state or preset loaded, a flush, the page's edit): taken as one set, the
    // machine first. A set still being written waits for the next block.
    if (p.mainChanged.exchange (false, std::memory_order_acq_rel))
    {
        const uint32_t seq = p.mainSeq.load (std::memory_order_acquire);
        float set[paramCount];
        for (uint32_t i = 0; i < paramCount; ++i) set[i] = p.values[i].load (std::memory_order_relaxed);
        std::atomic_thread_fence (std::memory_order_acquire);
        if ((seq & 1) != 0 || p.mainSeq.load (std::memory_order_relaxed) != seq)
            p.mainChanged.store (true, std::memory_order_relaxed);
        else
        {
            const int machineRow = paramIndex (ParamId::machine);
            for (int pass = 0; pass < 2; ++pass)
                for (uint32_t i = 0; i < paramCount; ++i)
                {
                    if ((pass == 0) != ((int) i == machineRow)) continue;
                    if (set[i] != p.applied[i]) p.applied[i] = set[i], p.engine.set (paramAt (i).id, set[i]);
                }
        }
    }

    const auto* in = process->in_events;
    const uint32_t count = in != nullptr ? in->size (in) : 0;
    // Whether anything but the host's transport came in (Melodee sends that every block, playing or not).
    bool heard = false;
    auto take = [&] (const clap_event_header* e) {
        heard |= ! (e->space_id == CLAP_CORE_EVENT_SPACE_ID && e->type == CLAP_EVENT_TRANSPORT);
        handle (p, e, process->out_events);
    };
    uint32_t next = 0, done = 0;
    while (done < frames)
    {
        while (next < count)
        {
            const auto* e = in->get (in, next);
            if (e->time > done) break;
            take (e);
            ++next;
        }
        uint32_t until = frames;
        if (next < count) until = std::min<uint32_t> (frames, std::max<uint32_t> (in->get (in, next)->time, done + 1));
        p.engine.render (left + done, right ? right + done : nullptr, until - done);
        done = until;
    }
    for (; next < count; ++next) take (in->get (in, next)); // stamped past the block
#if MACHINE_SCREEN
    if (p.screenHost != nullptr)
    {
        // The page's scope: the last 128 frames. A paint asked for (about 30 a second at most) while something
        // moves, sounds, or came in.
        const uint32_t keep = std::min<uint32_t> (128, frames), shift = 128 - keep;
        for (uint32_t i = 0; i < shift; ++i) p.scope[i].store (p.scope[i + keep].load (std::memory_order_relaxed), std::memory_order_relaxed);
        for (uint32_t i = 0; i < keep; ++i) p.scope[shift + i].store (left[frames - keep + i], std::memory_order_relaxed);
        p.sincePaintAsk += frames;
        const bool moving = heard || p.engine.voicesOn() > 0 || p.animating.load (std::memory_order_relaxed);
        if (moving && p.sincePaintAsk * 30.0 >= p.engine.sampleRate())
        {
            p.sincePaintAsk = 0;
            p.screenHost->request_paint (p.host);
        }
    }
#else
    (void) heard;
#endif
    return p.engine.voicesOn() > 0 ? CLAP_PROCESS_CONTINUE : CLAP_PROCESS_CONTINUE_IF_NOT_QUIET;
}

const void* pluginExtension (const clap_plugin*, const char* id)
{
    if (std::strcmp (id, CLAP_EXT_PARAMS) == 0) return &paramsExtension;
    if (std::strcmp (id, CLAP_EXT_STATE) == 0) return &stateExtension;
    if (std::strcmp (id, CLAP_EXT_AUDIO_PORTS) == 0) return &audioExtension;
    if (std::strcmp (id, CLAP_EXT_NOTE_PORTS) == 0) return &noteExtension;
    if (std::strcmp (id, CLAP_EXT_PRESET_LOAD) == 0 || std::strcmp (id, CLAP_EXT_PRESET_LOAD_COMPAT) == 0) return &presetExtension;
#if MACHINE_SCREEN
    if (std::strcmp (id, CLAP_EXT_MELODEE_SCREEN) == 0) return &screenExtension;
#endif
    return nullptr;
}

void pluginMainThread (const clap_plugin*) {}

// Factory -----------------------------------------------------------------------------------------------------

uint32_t factoryCount (const clap_plugin_factory*) { return 1; }
const clap_plugin_descriptor* factoryDescriptor (const clap_plugin_factory*, uint32_t index) { return index == 0 ? &descriptor : nullptr; }

const clap_plugin* factoryCreate (const clap_plugin_factory*, const clap_host* host, const char* id)
{
    if (! clap_version_is_compatible (host->clap_version) || std::strcmp (id, descriptor.id) != 0) return nullptr;
    void* memory = ::operator new (sizeof (Plugin), std::align_val_t (64), std::nothrow);
    if (memory == nullptr) return nullptr;
    auto* p = new (memory) Plugin();
    p->host = host;
    p->clap = { &descriptor, p, pluginInit, pluginDestroy, pluginActivate, pluginDeactivate, pluginStart, pluginStop,
                pluginReset, pluginProcess, pluginExtension, pluginMainThread };
    return &p->clap;
}

const clap_plugin_factory factory { factoryCount, factoryDescriptor, factoryCreate };

// Preset discovery: one provider, one location inside the plug-in, the presets of presets.h.
const clap_preset_discovery_provider_descriptor presetProviderDescriptor { CLAP_VERSION, "com.hdslave.machine.presets",
                                                                           "hdslave Machine presets", "hdslave" };

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
    if (kind != CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN) return false;
    const clap_universal_plugin_id plugin { "clap", descriptor.id };
    for (const auto& preset : presetTable)
    {
        if (! r->begin_preset (r, preset.name, preset.key)) break;
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
    if (indexer == nullptr || id == nullptr || std::strcmp (id, presetProviderDescriptor.id) != 0) return nullptr;
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
} // namespace machine

extern "C" CLAP_EXPORT const clap_plugin_entry clap_entry { CLAP_VERSION, machine::entryInit, machine::entryDeinit, machine::entryFactory };
