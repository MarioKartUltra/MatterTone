# MatterTone POC — Model

MatterTone generates sound by numerically integrating a **driven, dissipative,
finite-dimensional nonlinear dynamical system** per MIDI voice. There is no
sample playback and no bank of conventional oscillators; every audible event —
twang, snap, pop, crackle, creak, pitched tone, subharmonic — emerges from the
state evolution of the equations below.

---

## 1. Structural (metriplectic / port-Hamiltonian) form

Each voice evolves a state vector **z** under

```
ż = [J(z) − R(z)] ∇H(z)  +  B(z) u  +  f_events(z, ξ)
```

with **J antisymmetric** (`Jᵀ = −J`, conservative energy exchange) and
**R symmetric positive-semidefinite** (`Rᵀ = R ⪰ 0`, dissipation). For the
unforced, isolated system (`u = 0`, no stochastic forcing):

```
Ḣ = −∇Hᵀ R ∇H ≤ 0.
```

This energy inequality is the practical **thermodynamic-admissibility** condition
for the POC. The discrete integrator is built so its numerical analogue holds:

```
H_{n+1} ≤ H_n + ε_num
```

(verified in the offline harness: `maxUnforcedRise = 0` to double precision for
the resonator core; see `Tests`/`tools/matter_render.cpp`).

MatterTone does **not** claim to be a quantitatively exact model of any specific
material. It is a *structurally admissible mesoscopic sound model*.

---

## 2. State vector

Per voice:

```
z = ( q₁, p₁, q₂, p₂, ξ, s_snap, s₁ … s_N )     N = 16 (Config::kNumCrackle)
```

| symbol      | meaning                                            |
|-------------|----------------------------------------------------|
| `qᵢ, pᵢ`    | generalized displacement / momentum of mode i (unit mass) |
| `ξ ∈ [0,1]` | slow material-memory variable                      |
| `s_snap`    | primary bistable "snap" element (overdamped)       |
| `s_j`       | heterogeneous crackle-network elements (overdamped) |

---

## 3. Stored energy H (bounded below by construction)

```
H = Σ_{i=1,2} [ pᵢ²/2 + kᵢqᵢ²/2 + βᵢqᵢ⁴/4 + δᵢqᵢ⁶/6 ]      (resonator modes)
  + (γ/2) q₁² q₂²                                            (intermodal coupling)
  + S·V_snap(s_snap)  −  S·g_s·q₁·s_snap                     (snap + its coupling)
  + S·V_crackle(s ; q₁)                                      (crackle net + coupling)
  + V_contact(q₁)                                            (unilateral contact)
```

Bounded-below guarantees (enforced in `Config.h` / the `configure()` methods):

* `δᵢ > 0` **always** (`Config::kSexticFloor`), so each mode's quartic-plus-sextic
  potential is bounded below even when `βᵢ < 0` (softening).
* `γ ≥ 0` (coupling is non-negative), so the cross term is ⪰ 0.
* Bistable quartic coefficient `a > 0` always, so `V_snap`, `V_j` are bounded
  below for every tilt.
* `V_contact(x) = (2/5) k_c ⟨x⟩₊^{5/2} ≥ 0`.

Because H is bounded below and (unforced) non-increasing, the state cannot run
away — the blow-up guard is a safety net, not the stabilizer.

---

## 4. Equations of motion

**Resonator modes** (unit mass, `pᵢ = q̇ᵢ`):

```
q̈ᵢ + cᵢ q̇ᵢ + c₃ᵢ|q̇ᵢ|q̇ᵢ + kᵢqᵢ + βᵢqᵢ³ + δᵢqᵢ⁵ + ∂V_couple/∂qᵢ = F_ext,i
```

giving amplitude-dependent pitch, nonlinear decay, intermodal transfer,
combination tones, and (at strong coupling) subharmonic / mode-locked behavior.

**Snap element** — tilted double well, overdamped:

```
V_snap(s;λ) = (a/4)s⁴ − (b/2)s² − λ s
η_s ṡ = −∂V_snap/∂s + g_s q₁
```

A *snap* is an emergent saddle-node event: as the loading tilt `λ` (Load/Snap
mode) grows, a minimum vanishes and the state falls to the surviving well. The
sudden change in `s_snap` kicks the resonator through `F_snap = +S g_s s_snap`.
Nothing is scripted or sampled.

**Crackle network** — N heterogeneous bistable elements, ring-coupled:

```
V_j(s_j) = (a_j/4)s_j⁴ − (b_j/2)s_j² − ℓ_j(q₁,ξ) s_j
V_net    = (K_c/2) Σ_(i,j)∈ring (s_i − s_j)²
η_j ṡ_j  = −∂V_j/∂s_j + K_c (s_{j−1} + s_{j+1} − 2 s_j)
```

Per-element `(a_j,b_j,η_j,κ_j,bias_j)` are drawn from a **seeded** PRNG
(`seed = f(note, userSeed)`), so a note is reproducible, not per-sample random.
Low `K_c` ⇒ isolated pops; high `K_c` ⇒ cascades / avalanches. The network pushes
back on `q₁` via `+S Σ_j κ_j s_j`.

**Contact** — smooth unilateral Hertz penalty at a wall `x_wall`:

```
F_c = −k_c ⟨x⟩₊^{3/2} − c_c ⟨x⟩₊^{1/2} ẋ ,   x = q₁ − x_wall
```

`⟨x⟩₊` is regularized by a softplus of width `ε` so it is C¹ (no aliasing kink).
The elastic part is conservative (in H); the damping part is folded into the
dissipative sub-step and can only remove energy.

**Memory** `ξ ∈ [0,1]`:

```
ξ̇ = k_heal (1 − ξ) − k_break ξ · g(q₁, q̇₁, s)
```

integrated with clamping. `ξ` modulates the snap/crackle barrier and (mildly) the
stiffness, so repeated notes differ by recent loading history. A shared,
persistent `materialState` carries `ξ` across notes; **Reset Material State**
restores it.

---

## 5. Non-dimensionalization (the scale trick)

A stiff resonator lives at force scale `ω² q ~ k₁`, while a unit-scale bistable
lives at `O(1)`. To make a "small" snap audibly drive a stiff mode **without**
destabilizing either, the snap/crackle/contact subsystems are written at **unit
stiffness scale** and every coupling **back into the audible mode is scaled by
`S = k₁`**. The overdamped `s`-dynamics then run at an `S`-independent timescale
(the S cancels), keeping O(1) barriers, while the back-reaction on `q₁` is
`S`-scaled and therefore audible. This is what lets the events be commensurate
with the resonator. See `MatterVoice::conservativeForces` / `totalEnergy`.

---

## 6. Numerical integration

Integrated at `sampleRate × oversampling` (2/4/8×). Per internal sub-step, an
**operator split** in the order the brief requests:

```
conservative exchange  →  dissipative update  →  internal-variable kinetics
                       →  contact/event update →  radiation
```

* **Conservative exchange**: velocity-Verlet (kick–drift–kick) on `(qᵢ,pᵢ)` using
  only conservative forces — symplectic, so the Hamiltonian part creates no
  systematic energy. (No explicit Euler in the audio path.)
* **Dissipative update**: linear damping is the *exact* exponential
  `p *= exp(−c dt)`; nonlinear `|v|v` drag uses a backward-Euler step
  `p /= (1 + c₃|p| dt)`; contact damping is a state-dependent linear drag
  `p /= (1 + c_c⟨x⟩₊^{1/2} dt)`. Each can only shrink `|p|` ⇒ D ≥ 0.
* **Overdamped variables** (`ξ`, `s_snap`, `s_j`): stable gradient-flow steps with
  a per-substep **maximum-move cap** so a stiff well cannot overshoot.
* **Radiation → anti-alias decimation**: the radiation signal is formed on the
  oversampled grid and passed through a 4th-order Butterworth low-pass (two
  biquads) at `0.45·f_s`; one sample per host sample is taken. This suppresses the
  aliasing that the nonlinear/contact forces would otherwise fold back.

The scheme avoids systematic energy creation because (a) the only energy-changing
sub-steps are the manifestly-dissipative ones, and (b) the symplectic
conservative sub-step has no secular drift. The operator-splitting error between
the (slow) overdamped variables and the resonator is O(dt) and bounded; the
offline monitor confirms it stays below ε_num.

---

## 7. Acoustic radiation

The output is **not** raw displacement. Kinematics are normalized by the note's
reference frequency so level is roughly pitch-independent:

```
y = w_q·q₁  +  w_v·(q̇₁/ω)  +  w_a·(q̈₁/ω²)
  +  w_c·F_contact/S  +  w_s·tanh( (ṡ_snap + Σṡ_j)/ω )  +  w_D·√(D+ε)/ω
```

Defaults emphasize velocity, acceleration, contact force, and internal-event
rate. Post-processing: DC block → safety high-pass → soft limiter (emergency
only) → output gain. An optional body filter / event mix is exposed.

---

## 8. MIDI / excitation

`f_ref = 440·2^((n−69)/12)`; used only to set baseline stiffness `k₁ = (2π f_ref)²`
(unit mass). Output is **not** forced to a sinusoid at `f_ref`.

* **Pluck**: note-on sets an initial deformation distributed between the two modes
  by *Strike Position*; energy then decays into the dynamics.
* **Load / Snap**: note-on slowly ramps the snap tilt `λ` and crackle load until a
  well vanishes and the system snaps.

Velocity injects energy (initial deformation), not just output gain. Pitch bend
scales the stiffness/time-scale smoothly; mod wheel scales coupling; channel
pressure applies continued loading. All controls that enter the ODE are smoothed.

Tuning modes: **Chromatic Reference** (12-TET stiffness), **Ratio Set** (8-degree
rational or inharmonic mode-ratio scale — includes `1/1 9/8 5/4 4/3 3/2 5/3 7/4
2/1` and a deliberately unfamiliar inharmonic set), and **Attractor Scale** (each
scale degree selects a nonlinear parameter state; root preserved, pitch emerges).

---

## 9. Parameter → coefficient map (summary)

| user parameter        | acts on                                             |
|-----------------------|-----------------------------------------------------|
| Tension               | `k₁ = (2πf_ref)²·tension`                            |
| Nonlinear Stiffness   | magnitude of `βᵢ, δᵢ`                               |
| Hardening/Softening   | sign of `βᵢ` (+ hardening, − softening; `δᵢ>0` keeps H bounded) |
| Damping / Nonlinear Damping | `cᵢ`, `c₃ᵢ`                                   |
| Coupling              | `γ` (intermodal), mod-wheel scales it               |
| Brittleness / Snap Depth | snap & crackle barrier `b`, relaxation `η`       |
| Adhesion              | snap/crackle coupling `g_s, κ_j`; contact           |
| Roughness             | seeded spread of crackle `(a_j,b_j,η_j,κ_j)`        |
| Crackle Coupling      | network `K_c` (pops ↔ cascades)                     |
| Contact Hardness / Loss | `k_c`, `c_c`                                       |
| Healing / Memory      | `k_heal`, `k_break`, ξ-modulation strength          |
| Mode Ratio            | `k₂ = k₁·ratio²`                                     |
| Event Mix / Body Mix  | radiation weights `w_c,w_s,w_D` / body filter blend |

Exact scale constants live at the top of `MatterVoice.cpp` (documented inline).

---

## 10. Safety

* Cheap always-on guard per sub-step: `isfinite`, `|q|`/`|p|` bounds, kinetic
  energy vs a frequency-scaled reference `Href`, crackle-state validity.
* On failure: fade the voice to zero over `kFadeOutSamples`, reset its state,
  increment a debug counter — never emit a full-scale NaN burst.
* Denormal flush + `ScopedNoDenormals`. Every ODE parameter is clamped in
  `computeDesign()`. Voices sleep (release CPU) after decaying past silence.
