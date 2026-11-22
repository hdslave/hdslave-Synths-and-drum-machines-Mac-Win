# Changes made to the vendored sources

- `stmlib/utils/random.h`, `random.cc`: the shared random state is `thread_local` (each Nexus voice saves and restores
  it around its render, so voices are independent and the output is the same however a host threads its plug-ins).
- `plaits/dsp/dsp.h`: `kCorrectedSampleRate` is 48000 Hz. The module's audio clock runs at 47872.34 Hz and the
  firmware tunes to it; Nexus runs at a true 48 kHz, so with the module's value every note was 4.6 cents sharp.
- `stmlib/dsp/*.py` (table generators) were not copied.

Nothing else in `plaits/` or `stmlib/` is changed. Nexus's own adaptation of Plaits' voice is `src/plaits_voice.*`.
