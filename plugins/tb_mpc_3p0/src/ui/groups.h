// Parameter order and hardware labels for the five pages.
#pragma once
#include "../params.h"
#include "page.h"
#include <iterator>

namespace tbmpc::layout
{
inline constexpr hdui::Field voice[] = {
    { ParamId::tuning, "TUNING" }, { ParamId::cutoff, "CUTOFF" }, { ParamId::resonance, "RESONANCE" },
    { ParamId::envMod, "ENV MOD" }, { ParamId::decay, "DECAY" }, { ParamId::accent, "ACCENT" },
    { ParamId::waveform, "WAVEFORM" }, { ParamId::volume, "VOLUME" },
};
inline constexpr hdui::Field tone[] = {
    { ParamId::filter, "FILTER" }, { ParamId::peakSpread, "PEAK SPREAD" }, { ParamId::drive, "DRIVE" },
    { ParamId::glide, "SLIDE" }, { ParamId::slideTime, "SLIDE TIME" }, { ParamId::accentDecay, "ACCENT DECAY" },
};
inline constexpr hdui::Field sequencer[] = {
    { ParamId::mode, "MODE" }, { ParamId::seedMode, "SEED MODE" }, { ParamId::seed, "SEED" }, { ParamId::regen, "REGEN" },
    { ParamId::seedBack, "SEED BACK" }, { ParamId::mutate, "MUTATE" }, { ParamId::density, "DENSITY" },
    { ParamId::scale, "SCALE" }, { ParamId::root, "ROOT" }, { ParamId::octave, "OCTAVE" }, { ParamId::steps, "STEPS" },
    { ParamId::gate, "GATE" }, { ParamId::transpose, "TRANSPOSE" },
};
inline constexpr hdui::Field clock[] = {
    { ParamId::clockDiv, "CLOCK DIV" }, { ParamId::swing, "SWING" }, { ParamId::reset, "RESET" },
    { hdui::noParam, "THEME" },
};
inline constexpr hdui::Group groups[] = {
    { "PATTERN", nullptr, 0 }, { "VOICE", voice, 8 }, { "TONE", tone, 6 }, { "TB-3PO", sequencer, 13 }, { "CLOCK", clock, int (std::size (clock)) },
};
} // namespace tbmpc::layout
