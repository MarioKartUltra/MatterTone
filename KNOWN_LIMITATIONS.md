# Known Limitations (POC)

## Model / sound
* **Not a quantitative material model.** The equations are *structurally*
  admissible (bounded-below energy, non-negative dissipation), not a calibrated
  model of rubber/plastic/rock. Timbres are engineered, not measured.
* **Operator-splitting energy error.** The overdamped `s`-variables are treated as
  constant during each Verlet sub-step, introducing an O(dt) exchange error. It
  stays below the numerical tolerance in tests, but the strict `maxUnforcedRise=0`
  result is demonstrated for the resonator core (contact/crackle coupling off);
  with all subsystems active, `H` can settle into the (bounded-below) snap/crackle
  double-well floors — legitimate, but it means "H → 0" is not the right decay
  criterion for those presets (the harness checks bounded-below + monotone
  non-increase + motion-ceased instead).
* **Preset levels are only roughly matched** via per-preset `Output` makeup gain.
  Pluck presets are transient-peaky; crackle/snap presets are quieter RMS. A
  couple of presets can approach the soft limiter on their attack transient.
* **Body filter / Body Mix** is a minimal stub; no elaborate resonator body model.
* **Energy-to-Pitch** and some Attractor-Scale mappings are practical
  approximations, not bifurcation-continuation of true attractors.

## Numerics
* Oversampling is fixed per block (2/4/8×) with a single Butterworth decimator;
  extreme nonlinear settings at very high notes can still alias somewhat despite
  the anti-alias filter. No fully-adaptive substepping (by design, for
  predictable real-time cost); only a capped per-substep move limit.
* The debug `EnergyMonitor` (`MATTER_ENABLE_MONITOR`) is compiled into the offline
  harness only, never the release audio path.

## Plugin / GUI
* **The fCWT scalogram is a UI-thread feature.** It refreshes a full-window CWT at
  ~30 Hz; it is not a scrolling spectrogram and is intentionally kept off the
  audio thread. Visual confirmation requires opening the editor (the CWT itself is
  verified headlessly; the plugin is validated with `auval`, which does not open
  the editor).
* **foleys build state persistence.** In the `MATTER_USE_FOLEYS` build the base
  class is `foleys::MagicProcessor`, which saves parameters + GUI layout; the
  custom `program` index persisted by the non-foleys build is not separately
  stored. Factory presets still load via the program menu.
* In the foleys build, the fCWT **Scalogram** widget is registered in the WYSIWYG
  palette (drag it into the layout); it is not placed in the default auto-layout.
* FFTW is linked from `/opt/homebrew/opt/fftw/lib/libfftw3f.3.dylib` with an
  absolute install name — fine locally, but a distributed build would need the
  dylib bundled/relinked.
* MIDI is handled at block granularity (not sample-accurate within a block).

## Verification gaps
* Real-time behavior in Ableton Live 11 (dropouts, host automation) was **not**
  verified from this environment; validation was via `auval` (instantiate,
  parameters, render, MIDI, parameter ramp) plus the offline DSP test suite and
  offline renders. CPU headroom looks comfortable but was not profiled under a
  live host.
