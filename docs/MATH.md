# MatterTone II — the math behind the sound

Terse technical notes. Full derivations and design rationale: `MODEL.md`,
`AUDIO_V2_PLANNING/PERCUSSION_SPEC.md`. Everything below is implemented in
`Source/dsp/` (pure C++17, no JUCE) and exercised by `tools/matter_render.cpp`.

## 1. Modal engine (v1 core)

Per voice, two driven dissipative nonlinear resonator modes `q1, q2`:

    m_i q̈_i = −k_i q_i − β_i q_i³ − ε_i q_i⁵ − c_i q̇_i − c3_i |q̇_i| q̇_i
              − γ (q_i − q_j) + F_contact + F_snap + F_crackle + F_drive

- `k_i > 0` from Tension; `β_i` signed (Hardening/Softening) with a quintic
  `ε_i > 0` guard so the potential stays bounded below for any β.
- Bistable snap element: double-well `V(s) = a s⁴/4 − b s²/2` (`a > 0`),
  loaded slowly (Load/Snap mode) until it snaps between wells.
- Crackle network: 16 seeded threshold elements with nearest-neighbour
  coupling `K_c`; each fires a damped pulse when its local strain crosses a
  seeded threshold (heterogeneity = Roughness). Avalanches = cascades.
- Unilateral contact: one-sided Hertz spring `F = k_c ⌊g⌋_+^{3/2} − c_c ġ`
  active only while the gap `g < 0`.
- Bounded memory (material state ξ ∈ [0,1]): `ξ̇ = −k_dam ρ(strain) + k_heal (1−ξ)`
  modulating stiffness/damping — never a runaway.

### Energy admissibility

Hamiltonian `H = Σ (p_i²/2m_i + V_i(q_i)) + V_snap + V_contact + ...` with every
potential bounded below. Integration is operator-split:

1. **Symplectic Verlet** on the conservative part (preserves H to O(dt²),
   no systematic drift);
2. **Dissipative sub-step**: exact/implicit updates for the damping terms only —
   each is provably non-increasing in H (c_i ≥ 0, c3_i |v| v · v ≥ 0);
3. Forcing enters only via the excitation terms.

So unforced `Ḣ ≤ 0` by construction ("thermodynamically admissible"): the
offline harness asserts unforced energy decay and dissipation sign per element.
Oversampling (2/4/8×) keeps the Verlet step inside its stability bound
`ω dt < 2` for the stiffest active mode (a hard cap clamps dt regardless).

## 2. Percussion engine (v2): membrane + Berger + Helmholtz + wires

Selected per voice by `engine = Percussion`. N membrane modes (2..24)
`u_n` with ideal-membrane frequency ratios `ω_n = ω_0 λ_n`:

    ü_n = −ω_n²(T) u_n − (c + b ω_n²) u̇_n + mallet + wires + cavity coupling

- **Berger tension nonlinearity**: the head tension rises with total RMS
  deflection, `T = T_0 (1 + α Σ_n u_n²)`, so every mode's ω_n glides down as
  the strike decays — the kick "thump". α from `bergerAmount`. The associated
  potential `½ω_n²(T)u_n²` integrated consistently keeps H bounded.
- **Rayleigh damping tilt** `b ω_n²` (from `dampTilt`): high modes die faster,
  as in a real head.
- **Helmholtz cavity**: one air mode `p_H`, `p̈_H = −ω_H² p_H − c_H |ṗ_H| ṗ_H
  + κ Σ_n u̇_n`, with the reciprocal force −κ ṗ_H on the modes (skew coupling —
  energy-neutral; the quadratic orifice drag `c_H|ṗ|ṗ` is the only cavity loss).
  `ω_H = cavityTune · ω_0`; κ is PD-clamped for passivity.
- **Mallet**: Hertz contact `F = k_m ⌊x_m − u⌋_+^{3/2}` from a one-shot mallet
  mass; `malletHardness` sets k_m (soft beater → dark, hard → click).
- **Snare wires**: a bank of light oscillators resting against the head through
  one-sided contacts; the head throws them off and they rattle back
  (`wireAmount` engages the bank; 8× oversampling recommended — collisions
  alias). Same admissibility argument: contacts are one-sided springs with
  loss, wires have their own linear damping.

Energy accounting for the whole coupled system was validated with the
unforced-energy harness (see PERCUSSION_SPEC): all coupling terms are either
symplectic or skew-symmetric; all losses have a definite sign.

## 3. Per-voice AGC (v2)

The strike energy injected at note-on predicts the radiated peak; each voice
scales its output by `g = 1 / (1 + agc · (E_inj/E_ref − 1)⌋_+)` smoothed over
the onset, so hot presets never slam the soft-limiter. `agc = 0` restores v1.

## 4. Seed policy (v2)

`splitmix64`-based (`acv2::SeedPolicy`): **Fixed** = global seed;
**Per-Key** = `hash(global, midiNote)` (v1 behaviour — each key reproducible);
**Random-on-strike** = fresh seed per note-on, last seed reported to the UI so
a good one can be locked. Audio path stays fully deterministic given the seed.

## 5. LFO modulation & tempo sync (v2)

Two block-rate LFOs (`acv2::Lfo`: sine/tri/saws/square/S&H/smoothed-random/
drift; free Hz or the frozen 16-division beat grid; free/key/transport phase).
Applied in the JUCE layer as bounded offsets on the *normalised* parameter
value BEFORE it enters `MatterParams` (destinations: drive, damping, coupling,
crackle), then `computeDesign` clamps as always — the DSP core never sees an
LFO. Timed attack/load overrides use the same grid:
`ms = divisionToMs(div, bpm)`; `0` means "keep the v1 normalised knob".
