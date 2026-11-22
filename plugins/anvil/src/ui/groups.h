#pragma once
#include "../params.h"
#include "page.h"

namespace anvil::layout
{
inline constexpr hdui::Field vco[] = {
    { ParamId::vcoDecay, "VCO DECAY" }, { ParamId::fmAmt, "1-2 FM AMT" }, { ParamId::egFmAmt, "EG>FM 1-2" },
    { ParamId::vco1Freq, "VCO1 FREQ" }, { ParamId::vco1EgAmt, "VCO1 EG" }, { ParamId::vco1Wave, "VCO1 WAVE" },
    { ParamId::vco2Freq, "VCO2 FREQ" }, { ParamId::vco2EgAmt, "VCO2 EG" }, { ParamId::vco2Wave, "VCO2 WAVE" },
    { ParamId::seqPitchMod, "SEQ PITCH" }, { ParamId::hardSync, "HARD SYNC" },
};
inline constexpr hdui::Field mixer[] = {
    { ParamId::vco1Level, "VCO1 LEVEL" }, { ParamId::noiseLevel, "NOISE LEVEL" }, { ParamId::vco2Level, "VCO2 LEVEL" },
    { ParamId::noiseColor, "NOISE COLOR" },
};
inline constexpr hdui::Field vcf[] = {
    { ParamId::cutoff, "CUTOFF" }, { ParamId::resonance, "RESONANCE" }, { ParamId::vcfMode, "VCF MODE" },
    { ParamId::vcfDecay, "VCF DECAY" }, { ParamId::vcfEgAmt, "VCF EG AMT" }, { ParamId::noiseVcfMod, "NOISE>VCF" },
};
inline constexpr hdui::Field vca[] = {
    { ParamId::vcaEg, "VCA EG" }, { ParamId::vcaDecay, "VCA DECAY" }, { ParamId::volume, "VOLUME" },
};
inline constexpr hdui::Field patch[] = {
    { ParamId::velCutoff, "VEL>CUTOFF" }, { ParamId::pitchCutoff, "PITCH>CUT" }, { ParamId::vcfEgVco2, "VCF EG>VCO2" },
};
inline constexpr hdui::Field seq[] = {
    { ParamId::clockDiv, "CLOCK DIV" }, { ParamId::length, "LENGTH" }, { ParamId::swing, "SWING" },
    { ParamId::direction, "DIRECTION" }, { ParamId::chance, "CHANCE" }, { ParamId::ratchet, "RATCHET" },
    { ParamId::quantize, "QUANTIZE" }, { ParamId::noteMode, "NOTES" }, { ParamId::run, "RUN" },
    { ParamId::trigger, "TRIGGER" }, { ParamId::advance, "ADVANCE" },
};
inline constexpr hdui::Group groups[] = {
    { "STEPS", nullptr, 0 }, { "VCO", vco, 11 }, { "MIX", mixer, 4 }, { "VCF", vcf, 6 }, { "VCA", vca, 3 },
    { "PATCH", patch, 3 }, { "SEQ", seq, 11 },
};
} // namespace anvil::layout
