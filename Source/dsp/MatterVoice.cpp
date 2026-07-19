// =============================================================================
//  MatterTone POC — MatterVoice.cpp
//  See MatterVoice.h and MODEL.md for the model. Implementation notes:
//    * The conservative exchange is integrated with velocity-Verlet (symplectic),
//      so the Hamiltonian part creates no systematic energy. Damping is a separate
//      sub-step that can only remove energy. Internal (overdamped) variables use
//      capped stable updates. This is the operator-splitting order requested by
//      the brief: conservative -> dissipative -> internal kinetics -> events ->
//      radiation.
//    * All snap/crackle/contact couplings back into the audible mode are scaled
//      by S = k1 so a unit-scale bistable can audibly drive a stiff resonator.
// =============================================================================
#include "MatterVoice.h"
#include "AttractorScale.h"

#include <cmath>
#include <algorithm>

namespace matter
{
namespace
{
    // -------- design scale constants (documented in MODEL.md) -----------------
    constexpr double kBetaScale   = 0.55;  // cubic strength relative to k
    constexpr double kDeltaBase   = 0.12;  // sextic floor ratio (keeps H bounded below)
    constexpr double kDeltaNL     = 0.35;  // extra sextic with nonlinear stiffness
    constexpr double kDampAbsMin  = 0.45;  // linear damping at damping=0 (1/s)
    constexpr double kDampAbsSpan = 26.0;  // extra linear damping at damping=1
    constexpr double kC3NScale    = 7.0;   // nonlinear damping (normalised by omega)
    constexpr double kGammaScale  = 0.6;   // intermodal coupling relative to k1
    constexpr double kContactKc   = 2.2;   // contact stiffness relative to S
    constexpr double kContactCc   = 45.0;  // contact damping (abs)
    constexpr double kContactWall = 0.62;  // wall position (displacement units)
    constexpr double kCrackleKappaMin = 0.04;
    constexpr double kCrackleKappaSpan = 0.22;
    constexpr double kAgitAmpScale = 0.06; // agitation force amplitude relative to S

    inline double clampd(double x, double lo, double hi) { return x < lo ? lo : (x > hi ? hi : x); }

    constexpr double kOmegaDtMax = 0.6;    // symplectic-stability cap: omega*dt <= this

    inline double softLimit(double x)
    {
        if (!std::isfinite(x)) return 0.0;  // never let a NaN/Inf pass the limiter
        // ~unity for |x| <= 0.8, gentle knee above (emergency limiter only).
        const double a = std::fabs(x);
        if (a <= 0.8) return x;
        const double s = (x < 0.0) ? -1.0 : 1.0;
        return s * (0.8 + 0.2 * std::tanh((a - 0.8) / 0.2));
    }
}

// -----------------------------------------------------------------------------
void MatterVoice::prepare(double sr)
{
    sampleRate = (sr > 1.0) ? sr : 48000.0;
    setOversampling(os);
    dcR = std::exp(-2.0 * M_PI * cfg::kDCBlockHz  / sampleRate);
    hpA = std::exp(-2.0 * M_PI * cfg::kSafetyHPHz / sampleRate);
    agcRelCoeff = std::exp(-1.0 / (cfg::kAgcReleaseSecs * sampleRate));
    reset();
    active = false;
}

void MatterVoice::setOversampling(int factor)
{
    os = std::min(std::max(factor, cfg::kOversampleMin), cfg::kOversampleMax);
    dt = 1.0 / (sampleRate * os);
    decimator.prepare(sampleRate, os);
    // The percussion stability cap depends on dt — reconfigure a live perc voice.
    if (percConfigured)
        perc.configure(p_, percF0, dt, percSeed);
}

// -----------------------------------------------------------------------------
void MatterVoice::reset()
{
    q1 = p1 = q2 = p2 = 0.0;
    xi = 1.0;                 // intact
    sSnap = 0.0;
    crackle.reset();
    perc.reset();
    percConfigured = false;
    agcPeak = 0.0;
    lambdaCur = 0.0;
    loadBias  = 0.0;
    attackEnv = 1.0;
    released  = false;
    fading    = false;
    fadeCounter = 0;
    envPeak = 0.0;
    silenceCounter = 0;
    dcX1 = dcY1 = hpX1 = hpY1 = 0.0;
    decimator.reset();
    monitor.reset(0.0);
}

// -----------------------------------------------------------------------------
void MatterVoice::computeDesign()
{
    // Baseline frequency / stiffness for this note under the active tuning mode.
    const AttractorState st = AttractorScale::stateForNote(p_, note);
    const double fBase = AttractorScale::baseFrequency(p_, note);
    const double omegaBase = 2.0 * M_PI * fBase;

    k1_0 = omegaBase * omegaBase * clampd(p_.tension, 0.25, 4.0) * st.stiffnessMul;
    const double r2 = (p_.tuningMode == TuningMode::AttractorScale)
                        ? clampd(st.modeRatio, 0.25, 8.0)
                        : clampd(p_.modeRatio, 0.25, 8.0);
    k2_0 = k1_0 * r2 * r2;

    const double nl   = clampd(p_.nonlinStiffness, 0.0, 1.0) * st.nonlinStiffMul;
    const double hard = clampd(p_.hardening, -1.0, 1.0);

    betaRatio1  = hard * nl * kBetaScale;
    betaRatio2  = hard * nl * kBetaScale;
    deltaRatio1 = kDeltaBase + kDeltaNL * nl;   // > 0 always => H bounded below
    deltaRatio2 = kDeltaBase + kDeltaNL * nl;

    c1_ = kDampAbsMin + clampd(p_.damping, 0.0, 1.0) * kDampAbsSpan;
    c2_ = c1_ * 1.15;   // mode 2 slightly more damped
    c3n1 = clampd(p_.nonlinDamping, 0.0, 1.0) * kC3NScale;
    c3n2 = c3n1;

    gammaRatio = clampd(p_.coupling, 0.0, 1.0) * st.couplingMul * kGammaScale;

    // Snap element (unit scale barrier; coupling & energy scaled by S at use).
    const double britt = clampd(p_.brittleness, 0.0, 1.0);
    snapBarrier0 = (0.3 + 1.7 * clampd(p_.snapDepth, 0.0, 1.0)) * st.snapBarrierMul;
    tauSnap = std::max(cfg::kEtaFloor, 2.0e-3 / (0.2 + britt));
    gsnSnap = 0.5 + 1.5 * clampd(p_.adhesion, 0.0, 1.0);

    // Contact.
    kcRatio     = kContactKc * clampd(p_.contactHardness, 0.0, 1.0);
    ccAbs       = kContactCc * clampd(p_.contactLoss, 0.0, 1.0);
    contactWall = kContactWall;

    // Crackle network coupling (means/heterogeneity applied in noteOn::configure).
    const double kc = clampd(p_.crackleCoupling, 0.0, 1.0) * 3.0;
    crackle.setCoupling(kc);

    // Memory kinetics.
    kHeal       = 0.2 + clampd(p_.healing, 0.0, 1.0) * 4.0;
    kBreak      = clampd(p_.memory, 0.0, 1.0) * 8.0;
    memStrength = clampd(p_.memory, 0.0, 1.0);

    // Radiation weights (emphasise velocity, acceleration, contact, event rate).
    const double ev = clampd(p_.eventMix, 0.0, 1.0);
    wq = 0.12; wv = 0.42; wa = 0.22;
    wc = 0.90 * ev; ws = 1.60 * ev; wD = 0.15 * ev;
    eventScale = 8.0; contactRadScale = 3.0; dissRadScale = 0.20;
    outScale = 0.09;

    // Onset ramp (de-click / soft attack): 0.3 ms floor .. ~0.5 s.
    // v2: a positive attackMsOverride (from the ms/beats trio) wins over the v1
    // normalised mapping; 0 keeps v1 behaviour exactly.
    const double atk  = clampd(p_.attack, 0.0, 1.0);
    double atkT = 0.0003 + atk * atk * 0.5;
    if (p_.attackMsOverride > 0.0)
        atkT = clampd(p_.attackMsOverride, 0.3, 10000.0) * 1.0e-3;
    attackInc = 1.0 / (atkT * sampleRate);

    // Loading ramp (Load/Snap excitation).
    loadRateHz = 0.5 + clampd(p_.loadingRate, 0.0, 1.0) * 22.0;
    loadMax    = snapBarrier0 * 2.5;   // enough tilt to annihilate a well -> snap
    // v2: loadTimeMsOverride = time for the tilt to reach full load (ms).
    if (p_.loadTimeMsOverride > 0.0)
        loadRateHz = loadMax / (clampd(p_.loadTimeMsOverride, 20.0, 20000.0) * 1.0e-3);

    agitAmp = kAgitAmpScale;  // multiplied by S and agitation param at use
}

void MatterVoice::refreshDynamic()
{
    const double bendMult = std::pow(2.0, clampd(pitchBendSemis, -48.0, 48.0) / 12.0);
    const double memStiffMul = (memoryModStiffness && memStrength > 0.0)
                                 ? (1.0 + memStrength * (xi - 0.7) * 0.3)
                                 : 1.0;
    const double kMul = bendMult * bendMult * memStiffMul;

    // Velocity-Verlet is only conditionally stable (omega*dt < 2). Cap BOTH modes'
    // stiffness so omega*dt <= kOmegaDtMax, well below the limit even after
    // nonlinear stiffening. This bounds the highest playable frequency at a given
    // oversampling instead of letting a high note / high tension / pitch bend
    // diverge. Mode 2 (up to modeRatio^2 stiffer) is capped by the same bound.
    const double kMax = (kOmegaDtMax / dt) * (kOmegaDtMax / dt);
    const double k1 = std::min(kMax, std::max(1.0e-9, k1_0 * kMul));
    const double k2 = std::min(kMax, std::max(1.0e-9, k2_0 * kMul));
    S = k1;
    omegaRef = std::sqrt(k1);
    const double omega2 = std::sqrt(k2);

    res1.k = k1; res1.omega = omegaRef; res1.beta = betaRatio1 * k1; res1.delta = deltaRatio1 * k1;
    res1.c = c1_; res1.c3 = c3n1 / omegaRef;
    res2.k = k2; res2.omega = omega2;   res2.beta = betaRatio2 * k2; res2.delta = deltaRatio2 * k2;
    res2.c = c2_; res2.c3 = c3n2 / omega2;

    const double modMult = 1.0 + clampd(modWheel, 0.0, 1.0) * 2.0;
    gammaCur = gammaRatio * k1 * modMult;

    // Memory modulates the snap barrier (healed => higher barrier => harder to snap).
    const double memBarrierMul = (memStrength > 0.0) ? (0.5 + 0.8 * xi) : 1.0;
    snap.a = cfg::kBistableQuartic;
    snap.b = snapBarrier0 * memBarrierMul;
    snap.eta = tauSnap;
    snap.lambda = lambdaCur;

    contact.kC = kcRatio * S;
    contact.cC = ccAbs;
    contact.xWall = contactWall;
}

// -----------------------------------------------------------------------------
void MatterVoice::conservativeForces(double q1_, double q2_, double& F1, double& F2) const
{
    // Mode self-forces + intermodal coupling + snap/crackle back-reaction + contact.
    F1 = res1.selfForce(q1_)
       - gammaCur * q1_ * q2_ * q2_          // -d/dq1 (gamma/2 q1^2 q2^2)
       + S * gsnSnap * sSnap                  // snap coupling (V = -S gsn q1 s)
       + S * crackle.backReactionForce()      // crackle coupling (unit) * S
       + contact.elasticForce(q1_);           // conservative contact (Hertz)

    F2 = res2.selfForce(q2_)
       - gammaCur * q2_ * q1_ * q1_;
}

double MatterVoice::totalEnergy() const
{
    double H = res1.energy(q1, p1) + res2.energy(q2, p2)
             + 0.5 * gammaCur * q1 * q1 * q2 * q2
             + S * snap.energy(sSnap)
             - S * gsnSnap * q1 * sSnap
             + S * crackle.energy(q1, loadBias)
             + contact.energy(q1);
    return H;
}

// -----------------------------------------------------------------------------
void MatterVoice::integrateSubstep(double dtSub, double& yOut, bool& blewUp, bool forced)
{
    // ---- 1. Conservative exchange: velocity Verlet (kick-drift-kick) ----------
    double F1, F2;
    conservativeForces(q1, q2, F1, F2);
    p1 += 0.5 * dtSub * F1;  p2 += 0.5 * dtSub * F2;
    q1 += dtSub * p1;        q2 += dtSub * p2;
    double F1b, F2b;
    conservativeForces(q1, q2, F1b, F2b);
    p1 += 0.5 * dtSub * F1b; p2 += 0.5 * dtSub * F2b;

    // ---- 2. Dissipative update (strictly removes kinetic energy) --------------
    double diss = 0.0;
    diss += res1.applyDamping(p1, dtSub);
    diss += res2.applyDamping(p2, dtSub);
    const double cd = contact.dampingCoeff(q1);
    if (cd > 0.0)
    {
        const double pB = p1;
        p1 = p1 / (1.0 + cd * dtSub);
        diss += 0.5 * (pB * pB - p1 * p1);
    }

    // ---- optional bounded agitation forcing (external; marks step 'forced') ---
    double inj = 0.0;
    if (p_.agitation > 0.0)
    {
        const double f = p_.agitation * agitAmp * S * agitationRng.nextGaussian();
        const double pB = p1;
        p1 += dtSub * f;
        inj += 0.5 * (p1 * p1 - pB * pB);
        forced = true;
    }

    // ---- 3. Internal-variable kinetics ---------------------------------------
    // snap (overdamped gradient flow, drive = gsn q1)
    const double sBefore = sSnap;
    const double snapDissUnit = snap.step(sSnap, gsnSnap * q1, dtSub, 0.5);
    const double sSnapDot = (sSnap - sBefore) / dtSub;
    diss += S * snapDissUnit;

    // crackle network
    double crackEvent = 0.0, crackDissUnit = 0.0;
    crackle.step(q1, loadBias, dtSub, 0.5, crackEvent, crackDissUnit);
    diss += S * crackDissUnit;

    // memory xi in [0,1]
    const double vhat = p1 / omegaRef;
    const double activity = std::fabs(vhat)
                          + 0.3 * std::fabs(sSnapDot) / omegaRef
                          + 0.1 * std::fabs(crackEvent) / omegaRef;
    xi += dtSub * (kHeal * (1.0 - xi) - kBreak * xi * activity);
    xi = clampd(xi, 0.0, 1.0);

    // ---- 4. Loading ramp (Load/Snap excitation + aftertouch) ------------------
    {
        double tiltTarget = 0.0, biasTarget = 0.0;
        if (p_.excitation == ExcitationMode::LoadSnap && !released)
        {
            tiltTarget = loadMax;
            biasTarget = loadMax * 0.6;
        }
        // aftertouch applies continued loading regardless of mode
        tiltTarget += clampd(aftertouch, 0.0, 1.0) * loadMax * 0.5;
        biasTarget += clampd(aftertouch, 0.0, 1.0) * loadMax * 0.3;

        const double rate = loadRateHz * dtSub;
        lambdaCur += clampd(tiltTarget - lambdaCur, -rate, rate);
        loadBias  += clampd(biasTarget - loadBias,  -rate, rate);
    }

    // ---- 5. Radiation ---------------------------------------------------------
    const double qhat = q1;
    const double ahat = F1b / (omegaRef * omegaRef);          // elastic-accel proxy
    const double eventSig = std::tanh(eventScale * (sSnapDot + crackEvent) / omegaRef);
    const double fc = contact.elasticForce(q1) / (S + 1.0e-9) * contactRadScale;
    const double dissSig = std::sqrt(diss / dtSub + cfg::kDissipationEps) * dissRadScale / omegaRef;

    double yraw = wq * qhat + wv * vhat + wa * ahat
                + wc * fc + ws * eventSig + wD * dissSig;
    yraw *= outScale;

    yOut = decimator.processOversampled(yraw);

#ifdef MATTER_ENABLE_MONITOR
    // Loading (Load/Snap ramp, aftertouch) is EXTERNAL forcing — flag it so its
    // H increases are not scored against the unforced-monotonicity statistic.
    const bool loadingActive = (std::fabs(lambdaCur) > 1.0e-9) || (std::fabs(loadBias) > 1.0e-9)
                             || (p_.excitation == ExcitationMode::LoadSnap && !released)
                             || (aftertouch > 0.0);
    const double H = totalEnergy();
    monitor.update(H, diss, inj, std::fabs(yraw), forced || loadingActive);
#else
    (void) inj; (void) forced;
#endif

    // ---- cheap always-on numerical-safety check -------------------------------
    const double kinetic = 0.5 * (p1 * p1 + p2 * p2);
    const double pCap = cfg::kMaxMomentumScale * omegaRef;
    if (!std::isfinite(q1) || !std::isfinite(p1) || !std::isfinite(q2) || !std::isfinite(p2)
        || !std::isfinite(sSnap)
        || std::fabs(q1) > cfg::kMaxDisplacement || std::fabs(q2) > cfg::kMaxDisplacement
        || std::fabs(p1) > pCap || std::fabs(p2) > pCap
        || kinetic > cfg::kEnergyBlowupFactor * Href
        || !crackle.statesValid())
    {
        blewUp = true;
    }

    // denormal flush
    if (std::fabs(q1) < cfg::kDenormalFloor) q1 = 0.0;
    if (std::fabs(p1) < cfg::kDenormalFloor) p1 = 0.0;
    if (std::fabs(q2) < cfg::kDenormalFloor) q2 = 0.0;
    if (std::fabs(p2) < cfg::kDenormalFloor) p2 = 0.0;
}

// -----------------------------------------------------------------------------
//  v2 percussion engine: (re)configuration + one integrator sub-step wrapper.
// -----------------------------------------------------------------------------
void MatterVoice::configurePercussion()
{
    // Head tension tunes the membrane (f ~ sqrt(T)) — a kick preset with
    // tension 0.25 sits an octave below the played note.
    percF0 = AttractorScale::baseFrequency(p_, note)
           * std::sqrt(clampd(p_.tension, 0.25, 4.0));
    perc.configure(p_, percF0, dt, percSeed);
    percConfigured = true;
    // Safety scales: q amplitudes are O(1); momenta scale with omega.
    omegaRef = std::sqrt(perc.omega0Squared());
    Href     = perc.omega0Squared() * 10.0 + 1.0;
    percOutScale = 2.2;
}

void MatterVoice::integrateSubstepPerc(double dtSub, double& yOut, bool& blewUp)
{
    double diss = 0.0;
    const double eventW = clampd(p_.eventMix, 0.0, 1.0);
    const double yraw = perc.integrateSubstep(dtSub, eventW, diss) * percOutScale;
    yOut = decimator.processOversampled(yraw);

#ifdef MATTER_ENABLE_MONITOR
    // The mallet's incoming momentum is an initial condition (its kinetic energy
    // is part of H), so a percussion strike is monitored as UNFORCED throughout.
    monitor.update(perc.totalEnergy(), diss, 0.0, std::fabs(yraw), false);
#endif

    const double pCap = cfg::kMaxMomentumScale * omegaRef;
    if (!perc.statesValid(cfg::kMaxDisplacement, pCap)
        || perc.kineticEnergy() > cfg::kEnergyBlowupFactor * Href)
        blewUp = true;
}

// -----------------------------------------------------------------------------
float MatterVoice::renderSample()
{
    if (!active) return 0.0f;

    const bool isPerc = percConfigured && p_.engine == EngineMode::Percussion;
    if (isPerc)
    {
        // Cheap per-sample scale: pitch bend only (no memory / mod coupling here).
        const double bendMult = std::pow(2.0, clampd(pitchBendSemis, -48.0, 48.0) / 12.0);
        perc.setRateScale(bendMult * bendMult);
    }
    else
        refreshDynamic();

    bool blew = false;
    double decimated = 0.0;
    // Once a blow-up has been detected we FREEZE integration: continuing to
    // integrate a runaway state would overflow to +/-Inf then NaN during the fade.
    if (!fading)
    {
        for (int i = 0; i < os; ++i)
        {
            double y = 0.0;
            if (isPerc) integrateSubstepPerc(dt, y, blew);
            else        integrateSubstep(dt, y, blew, false);
            decimated = y;   // decimator's running output; last sub-step = this sample
            if (blew) break;
        }
    }
    if (!std::isfinite(decimated)) decimated = 0.0;

    // On the FIRST detected blow-up, hard-zero the runaway state + filter memory so
    // the fade plays clean silence (never a NaN burst) and the poisoned IIR state
    // cannot propagate.
    if (blew && !fading)
    {
        fading = true;
        fadeCounter = cfg::kFadeOutSamples;
        ++resetCount;
        q1 = p1 = q2 = p2 = 0.0; sSnap = 0.0;
        crackle.reset();
        perc.reset();
        decimator.reset();
        dcX1 = dcY1 = hpX1 = hpY1 = 0.0;
        decimated = 0.0;
    }

    // Onset ramp (de-click / soft attack) applied before the limiter.
    if (attackEnv < 1.0) attackEnv = std::min(1.0, attackEnv + attackInc);

    // DC block then safety high-pass (both x - x1 + R*y1 form).
    double out = decimated;
    const double dcOut = out - dcX1 + dcR * dcY1; dcX1 = out; dcY1 = dcOut;
    const double hpOut = dcOut - hpX1 + hpA * hpY1; hpX1 = dcOut; hpY1 = hpOut;

    // ---- v2 per-voice AGC: keep attacks below the limiter knee ---------------
    // Fast peak follower (instant attack, ~kAgcReleaseSecs release) on the
    // pre-limiter signal; downward-only gain toward kAgcTarget, blended by p.agc.
    double pre = hpOut * attackEnv;
    {
        const double a = std::fabs(pre);
        agcPeak = (a > agcPeak) ? a : agcPeak * agcRelCoeff;
        if (agcPeak > cfg::kAgcTarget)
        {
            const double g = cfg::kAgcTarget / agcPeak;             // < 1
            pre *= 1.0 + clampd(p_.agc, 0.0, 1.0) * (g - 1.0);
        }
    }
    out = softLimit(pre);
    if (!std::isfinite(out)) out = 0.0;   // final guarantee: never return NaN/Inf

    const double a = std::fabs(out);
    envPeak = (a > envPeak) ? a : envPeak * 0.9995;

    // ---- graceful numerical-failure handling ---------------------------------
    if (fading)
    {
        const double g = (double) fadeCounter / (double) cfg::kFadeOutSamples;
        out *= g;
        if (--fadeCounter <= 0)
        {
            reset();
            active = false;   // free the voice; a new note will re-trigger it
        }
    }

    // ---- sleep when silent after release --------------------------------------
    if (released)
    {
        if (envPeak < cfg::kSilenceThreshold)
        {
            if (++silenceCounter > cfg::kSilenceHoldSamples) active = false;
        }
        else silenceCounter = 0;
    }

    return (float) out;
}

// -----------------------------------------------------------------------------
void MatterVoice::noteOn(int midiNote, double velocity01, const MatterParams& params)
{
    p_ = params;
    note = midiNote;
    velocity = clampd(velocity01, 0.0, 1.0);

    reset();

    // Deterministic per-note material seed -> reproducible. v2: when the synth's
    // SeedPolicy supplied a note seed (noteSeed != 0) it drives ALL of the
    // voice's randomness; 0 falls back to the v1 (seed, note) hash exactly.
    const std::uint64_t seed = (p_.noteSeed != 0)
        ? p_.noteSeed
        : ((std::uint64_t) p_.seed * 0x9E3779B97F4A7C15ull
         ^ ((std::uint64_t) (midiNote + 1) * 0xD1B54A32D192ED03ull));
    agitationRng.reseed(seed ^ 0xA5A5A5A5A5A5A5A5ull);
    percSeed = seed ^ 0xC0AC0AC0AC0AC0Aull;

    computeDesign();

    // Configure the heterogeneous crackle bank once per note (deterministic).
    const double britt = clampd(p_.brittleness, 0.0, 1.0);
    const double meanKappa   = kCrackleKappaMin + kCrackleKappaSpan * clampd(p_.adhesion, 0.0, 1.0);
    const double etaMeanCr   = std::max(cfg::kEtaFloor, 1.5e-3 / (0.2 + britt));
    const double meanBarrier = snapBarrier0;
    crackle.configure(seed, clampd(p_.roughness, 0.0, 1.0), meanBarrier,
                      meanKappa, etaMeanCr, clampd(p_.crackleCoupling, 0.0, 1.0) * 3.0);

    refreshDynamic();
    Href = k1_0 + 1.0;
    monitor.reset(0.0);

    // ---- excitation ----------------------------------------------------------
    const double velScaled = 1.0 - clampd(p_.velocitySens, 0.0, 1.0) * (1.0 - velocity);

    // v2 percussion engine: configure the membrane + throw the mallet. The
    // mallet's momentum is the ONLY energy input (an initial condition).
    if (p_.engine == EngineMode::Percussion)
    {
        configurePercussion();
        perc.reset();
        const double omega0 = std::sqrt(perc.omega0Squared());
        const double momentum = (0.25 + 2.75 * clampd(p_.pluckEnergy, 0.0, 1.0) * velScaled)
                              * omega0 * 0.5;
        perc.strike(momentum);
        xi = clampd(initialMemory, 0.0, 1.0);
        released = false;
        active = true;
        fading = false;
        silenceCounter = 0;
        envPeak = 0.0;
        attackEnv = 0.0;
        return;
    }

    if (p_.excitation == ExcitationMode::Pluck)
    {
        // Pluck = initial deformation, distributed between the two modes by the
        // strike position (modal balance). Energy then decays into the dynamics.
        const double amp = (0.4 + 3.6 * clampd(p_.pluckEnergy, 0.0, 1.0)) * velScaled;
        const double bal = clampd(p_.strikePosition, 0.0, 1.0);
        q1 = amp * (1.0 - 0.7 * bal);
        q2 = amp * (0.7 * bal);
        p1 = 0.0; p2 = 0.0;
    }
    else
    {
        // Load/Snap = start near rest and slowly load the bistable subsystem
        // (lambdaCur / loadBias ramp in integrateSubstep) until a well vanishes.
        const double amp = 0.15 * velScaled;
        q1 = amp; q2 = 0.0;
        lambdaCur = 0.0; loadBias = 0.0;
    }

    // Start from the shared, persistent material memory (history-dependent).
    xi = clampd(initialMemory, 0.0, 1.0);

    released = false;
    active = true;
    fading = false;
    silenceCounter = 0;
    envPeak = 0.0;
    attackEnv = 0.0;   // start the onset ramp
}

void MatterVoice::noteOff()
{
    released = true;
    // Loading targets fall to zero in integrateSubstep; natural dissipation decays
    // the voice. No forced release envelope needed for stable presets.
}

void MatterVoice::updateParameters(const MatterParams& params)
{
    // Live edits keep the voice's note seed (params from the synth carry the
    // synth-global noteSeed of the LAST note-on — not this voice's).
    const std::uint64_t keepSeed = p_.noteSeed;
    p_ = params;
    p_.noteSeed = keepSeed;
    computeDesign();   // recompute scalar design; crackle heterogeneity kept as-is
    // Percussion design follows live edits too (state q,p preserved).
    if (percConfigured && p_.engine == EngineMode::Percussion)
        perc.configure(p_, percF0, dt, percSeed);
}

void MatterVoice::seedFreeDecay(double q1init, double p1init)
{
    reset();
    computeDesign();

    if (p_.engine == EngineMode::Percussion)
    {
        // Percussion unforced-energy harness: finite modal energy, NO mallet,
        // NO forcing — Berger / cavity / orifice / wires all as parameterised.
        percSeed = 0xF00DFACEDEADBEEFull;
        configurePercussion();
        perc.seedFreeDecay(q1init, p1init);
        monitor.reset(perc.totalEnergy());
        active = true;
        released = false;
        return;
    }

    refreshDynamic();
    Href = k1_0 + 1.0;
    // Seed the state BEFORE capturing the initial energy (crackle stays inert:
    // this test targets the symplectic + dissipative Hamiltonian core).
    q1 = q1init; p1 = p1init; q2 = 0.0; p2 = 0.0;
    monitor.reset(totalEnergy());
    active = true;
    released = false;
}
} // namespace matter
