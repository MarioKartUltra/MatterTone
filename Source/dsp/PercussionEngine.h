// =============================================================================
//  MatterTone II — PercussionEngine.h
//  Kick / snare percussion engine per AUDIO_V2_PLANNING/PERCUSSION_SPEC.md:
//
//    * N-mode membrane, fixed inharmonic Bessel-root ratio set (j_mn / j_01).
//    * Berger tension modulation (the pitch-glide "thump"): forces derive from
//      ONE fixed quartic potential
//          V_Berger = (alpha/4) T^2,   T = sum_j phiB_j q_j^2
//          F_i      = -alpha phiB_i q_i T
//      NEVER by modulating omega_i on the fly (that would be parametric pumping
//      and Verlet would gain energy — spec pitfall #1).
//    * Tension clamp: T is capped at Tmax IDENTICALLY in force and energy via
//      the matched C1 pair
//          T <= Tmax : V = (alpha/4) T^2,                     F ~ T
//          T >  Tmax : V = (alpha/4) Tmax^2
//                        + (alpha/2) Tmax (T - Tmax),         F ~ Tmax
//      (physically: head-tension saturation). Clamp, don't substep.
//    * Rayleigh damping c_i = a + b (omega_i/omega_0)^2, applied as the exact
//      per-mode exponential (dissipative substep, can only remove energy).
//    * Mallet/beater: one-sided Hertz contact (ContactModel) between a free
//      mallet DOF (q_m, p_m) and the strike-weighted head displacement.
//    * Cavity: ONE Helmholtz resonator mode (q_H, p_H), bilinear coupling to
//      the volume-weighted mean displacement Q = sum phi_i q_i (phi_i = 0 for
//      non-axisymmetric modes — they move no net air).
//          H_cav = p_H^2/2 + omega_H^2 q_H^2 / 2 + kappa q_H Q
//      PD clamp: kappa^2 * sum(phi_i^2 / omega_i^2) < omega_H^2, or H stops
//      being bounded below. Orifice loss = quadratic |v|v drag on p_H via the
//      existing c3-style backward-Euler shrink step.
//    * Snare = collision, NOT bistability: a small bank of light one-sided
//      Hertz contacts (ContactModel, shared threshold spread) against the head
//      displacement. The crackle network is NOT used here (wrong dynamical
//      class for a rattle).
//
//  Integration: the same kick-drift-kick velocity-Verlet + shrink-only
//  dissipative substeps as the modal engine, so the MODEL.md §3 admissibility
//  discipline (unforced H_{n+1} <= H_n + eps_num) carries over unchanged.
//  Header-only, pure C++17, no JUCE.
// =============================================================================
#pragma once

#include <cmath>
#include <cstdint>
#include "Config.h"
#include "MatterParams.h"
#include "ContactModel.h"
#include "Rng.h"

namespace matter
{
class PercussionEngine
{
public:
    static constexpr int kMaxModes = cfg::kMaxMembraneModes;
    static constexpr int kWires    = cfg::kNumWires;

    // ---- fixed inharmonic Bessel-zero ratio set j_mn / j_01 (j_01 = 2.40483).
    // Ordered ascending; the paired flag marks axisymmetric (0,n) modes, the
    // only ones that couple to the cavity (they move net air).
    struct ModeTabEntry { double ratio; double phiAir; };
    static const ModeTabEntry* modeTable()
    {
        // phiAir for (0,n): ~ J1(j_0n)/j_0n normalised to the fundamental
        // (alternating sign, decreasing magnitude); 0 for m != 0.
        static const ModeTabEntry tab[kMaxModes] = {
            { 1.0000,  1.0000 }, { 1.5934, 0.0 }, { 2.1355, 0.0 },
            { 2.2954, -0.2857 }, { 2.6531, 0.0 }, { 2.9174, 0.0 },
            { 3.1555,  0.0 },    { 3.5002, 0.0 }, { 3.5986, 0.1454 },
            { 3.6475,  0.0 },    { 4.0590, 0.0 }, { 4.1318, 0.0 },
            { 4.2305,  0.0 },    { 4.6011, 0.0 }, { 4.6101, 0.0 },
            { 4.8319,  0.0 },    { 4.9033, -0.0913 }, { 5.0836, 0.0 },
            { 5.1308,  0.0 },    { 5.4126, 0.0 }, { 5.5404, 0.0 },
            { 5.5532,  0.0 },    { 5.6508, 0.0 }, { 5.9766, 0.0 },
        };
        return tab;
    }

    // -------------------------------------------------------------------------
    //  Configuration (note-on / parameter edits; NOT per-sample)
    // -------------------------------------------------------------------------
    //  f0     : membrane fundamental (Hz) for this note
    //  dt     : integrator substep (1 / (sampleRate * oversampling))
    //  seed   : deterministic seed for the wire-gap spread
    void configure(const MatterParams& p, double f0, double dt, std::uint64_t seed)
    {
        const auto* tab = modeTable();

        nModes = p.membraneModes < 2 ? 2 : (p.membraneModes > kMaxModes ? kMaxModes : p.membraneModes);

        const double omega0 = 2.0 * M_PI * (f0 > 1.0 ? f0 : 1.0);
        omega0Sq = omega0 * omega0;

        // ---- symplectic stability cap, applied AFTER max Berger stiffening ----
        // omega_eff = omega_i * sqrt(kBergerStiffenMax) must satisfy
        // omega_eff * dt <= kPercOmegaDtMax for the HIGHEST mode.
        const double wMax   = cfg::kPercOmegaDtMax / (dt > 0.0 ? dt : 1.0e-6);
        const double w2Ceil = (wMax * wMax) / cfg::kBergerStiffenMax;

        for (int i = 0; i < nModes; ++i)
        {
            const double r  = tab[i].ratio;
            double w2 = omega0Sq * r * r;
            if (w2 > w2Ceil) w2 = w2Ceil;              // cap, never alias/blow
            omega2[i] = w2;
            // Berger weights ~ |grad psi_i|^2 ~ omega_i^2, normalised by omega_0^2
            // so T is dimensionless and the stiffening RATIO is mode-uniform.
            phiB[i]   = w2 / omega0Sq;
            phiA[i]   = tab[i].phiAir;
        }
        for (int i = nModes; i < kMaxModes; ++i) { omega2[i] = 1.0; phiB[i] = 0.0; phiA[i] = 0.0; }

        // ---- Berger tension quartic (fixed potential; see header) -------------
        // alpha * Tmax = (stiffenMax - 1) * omega_0^2  =>  ratio cap built in.
        const double berger = clamp01(p.bergerAmount);
        alpha = berger * 2.2 * omega0Sq;
        Tmax  = (alpha > 1.0e-12)
                  ? (cfg::kBergerStiffenMax - 1.0) * omega0Sq / alpha
                  : 1.0e30;

        // ---- Rayleigh damping c_i = a + b (omega_i/omega_0)^2 -----------------
        const double a = 1.2 + clamp01(p.damping) * 60.0;
        const double b = clamp01(p.dampTilt) * a * 3.0;
        for (int i = 0; i < nModes; ++i)
            cRay[i] = a + b * (omega2[i] / omega0Sq - 1.0 > 0.0
                                 ? omega2[i] / omega0Sq - 1.0 : 0.0);

        // ---- Helmholtz cavity mode -------------------------------------------
        const double tune = p.cavityTune < 0.1 ? 0.1 : (p.cavityTune > 1.5 ? 1.5 : p.cavityTune);
        omegaH2 = omega0Sq * tune * tune;
        // PD clamp: kappa^2 * S_phi < omega_H^2, S_phi = sum phi_i^2 / omega_i^2.
        double Sphi = 0.0;
        for (int i = 0; i < nModes; ++i)
            Sphi += phiA[i] * phiA[i] / omega2[i];
        const double kapMax = (Sphi > 1.0e-30)
                                ? cfg::kCavityPDMargin * std::sqrt(omegaH2 / Sphi)
                                : 0.0;
        kappa = clamp01(p.cavityAmount) * kapMax;
        // Orifice quadratic drag coefficient (dimensionless vs q-amplitudes).
        cOrifice = clamp01(p.cavityLoss) * 4.0;

        // ---- mallet (one-sided Hertz contact, ContactModel unchanged) ---------
        {
            double km = clamp01(p.malletHardness) * 4.0 * omega0Sq;
            const double kmCeil = 0.25 * wMax * wMax;      // contact-CFL guard
            if (km > kmCeil) km = kmCeil;
            malletContact.kC    = km;
            malletContact.cC    = 8.0 * std::sqrt(omega0Sq) * (0.2 + 0.8 * clamp01(p.contactLoss));
            malletContact.xWall = 0.0;                     // overlap coordinate directly
        }

        // ---- strike weights (phenomenological): centre -> axisymmetric/dark,
        //      edge -> all modes/bright. Deterministic, normalised. -------------
        {
            const double sp = clamp01(p.strikePosition);
            double norm = 0.0;
            int axIdx = 0;
            for (int i = 0; i < nModes; ++i)
            {
                const bool ax = (phiA[i] != 0.0);
                const double centre = ax ? 1.0 / (1.0 + 0.8 * axIdx) : 0.06;
                if (ax) ++axIdx;
                const double edge   = 1.0 / tab[i].ratio;
                strikeW[i] = (1.0 - sp) * centre + sp * edge;
                norm += strikeW[i] * strikeW[i];
            }
            norm = std::sqrt(norm > 1.0e-30 ? norm : 1.0);
            for (int i = 0; i < nModes; ++i) strikeW[i] /= norm;
        }

        // ---- snare wire contact bank (shared threshold, seeded spread) --------
        {
            wireOn = clamp01(p.wireAmount) > 1.0e-4;
            Rng rng(seed ^ 0x51BEBA11C0FFEEULL);   // wire-bank scramble constant
            double kw = clamp01(p.wireAmount) * 1.8 * omega0Sq;
            const double kwCeil = 0.20 * wMax * wMax;
            if (kw > kwCeil) kw = kwCeil;
            for (int j = 0; j < kWires; ++j)
            {
                wires[j].kC    = kw;
                wires[j].cC    = 12.0 * std::sqrt(omega0Sq) * 0.4;
                wires[j].xWall = 0.015 + 0.11 * rng.nextUnit();   // gap below the head
            }
        }

        rateScale = 1.0;
        dirty = false;
    }

    // Cheap per-sample scale (pitch bend): multiplies every stiffness-like
    // coefficient consistently in BOTH force and energy, so admissibility with
    // a static bend is exact (a moving bend is external forcing, as in v1).
    void setRateScale(double mul) { rateScale = (mul > 1.0e-9 ? mul : 1.0e-9); }

    // -------------------------------------------------------------------------
    //  Note lifecycle
    // -------------------------------------------------------------------------
    void reset()
    {
        for (int i = 0; i < kMaxModes; ++i) { q[i] = 0.0; p[i] = 0.0; }
        qH = pH = 0.0;
        qm = 1.0; pm = 0.0;
        malletActive = false;
    }

    // Throw the mallet at the head with the given momentum (>0 = toward head).
    void strike(double momentum)
    {
        qm = 0.35;                    // start above the head (displacement units)
        pm = -std::fabs(momentum);
        malletActive = true;
    }

    // Test hook (unforced-energy harness): finite energy, NO mallet, NO forcing.
    void seedFreeDecay(double q0, double q1init)
    {
        reset();
        malletActive = false;
        q[0] = q0;
        if (nModes > 1) q[1] = q1init;
    }

    // -------------------------------------------------------------------------
    //  Energy (must match conservativeForces EXACTLY — monitored invariant)
    // -------------------------------------------------------------------------
    double totalEnergy() const
    {
        const double rs = rateScale;
        double H = 0.0;
        double T = 0.0, Q = 0.0;
        for (int i = 0; i < nModes; ++i)
        {
            H += 0.5 * p[i] * p[i] + 0.5 * rs * omega2[i] * q[i] * q[i];
            T += phiB[i] * q[i] * q[i];
            Q += phiA[i] * q[i];
        }
        // Berger quartic with the C1-matched tension clamp.
        const double al = rs * alpha;
        if (al > 0.0)
        {
            if (T <= Tmax) H += 0.25 * al * T * T;
            else           H += 0.25 * al * Tmax * Tmax + 0.5 * al * Tmax * (T - Tmax);
        }
        // Cavity (+ PD-clamped bilinear coupling).
        H += 0.5 * pH * pH + 0.5 * rs * omegaH2 * qH * qH + rs * kappa * qH * Q;
        // Mallet: kinetic + one-sided Hertz overlap potential.
        if (malletActive)
        {
            const double Qs = strikeDisp();
            H += 0.5 * pm * pm + malletContact.energy(Qs - qm);
        }
        // Wires: one-sided Hertz potentials against the head displacement.
        if (wireOn)
        {
            const double Qw = airDisp();
            for (int j = 0; j < kWires; ++j)
                H += wires[j].energy(-Qw);
        }
        return H;
    }

    // -------------------------------------------------------------------------
    //  One integrator substep. Returns the raw (pre-decimation) radiation sample.
    //  dissOut accumulates dissipated energy (>= 0) for the monitor.
    // -------------------------------------------------------------------------
    double integrateSubstep(double dtSub, double eventMixW, double& dissOut)
    {
        // ---- 1. conservative exchange: velocity-Verlet (kick-drift-kick) ------
        computeForces();
        for (int i = 0; i < nModes; ++i) p[i] += 0.5 * dtSub * F[i];
        pH += 0.5 * dtSub * FH;
        if (malletActive) pm += 0.5 * dtSub * Fm;

        for (int i = 0; i < nModes; ++i) q[i] += dtSub * p[i];
        qH += dtSub * pH;
        if (malletActive) qm += dtSub * pm;

        computeForces();
        for (int i = 0; i < nModes; ++i) p[i] += 0.5 * dtSub * F[i];
        pH += 0.5 * dtSub * FH;
        if (malletActive) pm += 0.5 * dtSub * Fm;

        // ---- 2. dissipative updates (each can only shrink momenta) ------------
        double diss = 0.0;

        // Rayleigh per-mode exact exponential.
        for (int i = 0; i < nModes; ++i)
        {
            const double pb = p[i];
            p[i] *= std::exp(-cRay[i] * dtSub);
            diss += 0.5 * (pb * pb - p[i] * p[i]);
        }

        // Helmholtz orifice: quadratic |v|v drag, backward-Euler shrink.
        if (cOrifice > 0.0)
        {
            const double pb = pH;
            pH = pH / (1.0 + cOrifice * std::fabs(pH) * dtSub);
            diss += 0.5 * (pb * pb - pH * pH);
        }

        // Mallet contact loss (felt): drag on the mallet momentum while in contact.
        if (malletActive)
        {
            const double cd = malletContact.dampingCoeff(strikeDisp() - qm);
            if (cd > 0.0)
            {
                const double pb = pm;
                pm = pm / (1.0 + cd * dtSub);
                diss += 0.5 * (pb * pb - pm * pm);
            }
            // Catch the mallet once it has clearly rebounded away: park it and
            // account its kinetic energy as dissipated (the player's hand).
            if (qm > 0.6 && pm > 0.0)
            {
                diss += 0.5 * pm * pm;
                pm = 0.0;
                malletActive = false;
            }
        }

        // Wire contact loss: modal drag projected through the air weights.
        double wireForce = 0.0;
        if (wireOn)
        {
            const double Qw = airDisp();
            double cdSum = 0.0;
            for (int j = 0; j < kWires; ++j)
            {
                cdSum     += wires[j].dampingCoeff(-Qw);
                wireForce += -wires[j].elasticForce(-Qw); // >= 0 while in contact
            }
            if (cdSum > 0.0)
            {
                for (int i = 0; i < nModes; ++i)
                {
                    const double ci = cdSum * phiA[i] * phiA[i];
                    if (ci <= 0.0) continue;
                    const double pb = p[i];
                    p[i] = p[i] / (1.0 + ci * dtSub);
                    diss += 0.5 * (pb * pb - p[i] * p[i]);
                }
            }
        }

        dissOut += diss;

        // ---- 3. radiation -----------------------------------------------------
        // Sound ~ volume velocity/acceleration + cavity port flow + wire rattle.
        const double omega0 = std::sqrt(rateScale * omega0Sq);
        double Qdot = 0.0, Qacc = 0.0, sVel = 0.0;
        for (int i = 0; i < nModes; ++i)
        {
            Qdot += phiA[i] * p[i];
            Qacc += phiA[i] * F[i];
            sVel += strikeW[i] * p[i];
        }
        const double y = 0.55 * (Qdot / omega0)
                       + 0.25 * (Qacc / (omega0 * omega0))
                       + 0.30 * (sVel / omega0)
                       + 0.80 * (pH / omega0)
                       + eventMixW * 2.5 * std::tanh(wireForce / (rateScale * omega0Sq));
        return y;
    }

    // ---- diagnostics / safety ----------------------------------------------
    bool statesValid(double maxDisp, double pCap) const
    {
        for (int i = 0; i < nModes; ++i)
            if (!std::isfinite(q[i]) || !std::isfinite(p[i])
                || std::fabs(q[i]) > maxDisp || std::fabs(p[i]) > pCap)
                return false;
        if (!std::isfinite(qH) || !std::isfinite(pH)
            || std::fabs(qH) > maxDisp || std::fabs(pH) > pCap)
            return false;
        if (malletActive && (!std::isfinite(qm) || !std::isfinite(pm)))
            return false;
        return true;
    }

    double kineticEnergy() const
    {
        double k = 0.0;
        for (int i = 0; i < nModes; ++i) k += 0.5 * p[i] * p[i];
        k += 0.5 * pH * pH;
        if (malletActive) k += 0.5 * pm * pm;
        return k;
    }

    int    modeCount() const { return nModes; }
    double cavityDisp() const { return qH; }
    double omega0Squared() const { return omega0Sq; }

private:
    static double clamp01(double x) { return x < 0.0 ? 0.0 : (x > 1.0 ? 1.0 : x); }

    double strikeDisp() const
    {
        double s = 0.0;
        for (int i = 0; i < nModes; ++i) s += strikeW[i] * q[i];
        return s;
    }

    double airDisp() const
    {
        double s = 0.0;
        for (int i = 0; i < nModes; ++i) s += phiA[i] * q[i];
        return s;
    }

    // Conservative forces = -grad H w.r.t. (q_i, q_H, q_m). MUST stay the exact
    // gradient of totalEnergy() (matched pairs) or the monitor flags drift.
    void computeForces()
    {
        const double rs = rateScale;

        double T = 0.0, Q = 0.0, Qs = 0.0, Qw = 0.0;
        for (int i = 0; i < nModes; ++i)
        {
            T  += phiB[i] * q[i] * q[i];
            Q  += phiA[i] * q[i];
            Qs += strikeW[i] * q[i];
            Qw += phiA[i] * q[i];
        }
        const double Tc = (T < Tmax) ? T : Tmax;    // tension clamp (see energy)
        const double al = rs * alpha;

        // Mallet contact force on the overlap u = Qs - qm (one-sided).
        double fMall = 0.0;
        if (malletActive)
            fMall = malletContact.elasticForce(Qs - qm); // <= 0 (xWall = 0: overlap coord)

        // Wire contact force on u = -Qw.
        double fWire = 0.0;
        if (wireOn)
            for (int j = 0; j < kWires; ++j)
                fWire += wires[j].elasticForce(-Qw);                            // <= 0

        for (int i = 0; i < nModes; ++i)
        {
            F[i] = -rs * omega2[i] * q[i]
                 - al * phiB[i] * q[i] * Tc
                 - rs * kappa * qH * phiA[i]
                 + strikeW[i] * fMall          // -dV_mallet/dq_i = +w_i * F_u
                 - phiA[i]   * fWire;          // -dV_wire/dq_i   = -phi_i * F_u
        }
        FH = -rs * omegaH2 * qH - rs * kappa * Q;
        Fm = malletActive ? -fMall : 0.0;      // -dV_mallet/dq_m = -F_u (pushes up)
    }

    // ---- design ---------------------------------------------------------------
    int    nModes   = 6;
    double omega0Sq = 1.0;
    double omega2[kMaxModes] {};
    double phiB[kMaxModes] {};      // Berger weights
    double phiA[kMaxModes] {};      // air-coupling weights (axisymmetric only)
    double strikeW[kMaxModes] {};   // mallet strike distribution
    double cRay[kMaxModes] {};      // Rayleigh damping
    double alpha = 0.0, Tmax = 1.0e30;
    double omegaH2 = 1.0, kappa = 0.0, cOrifice = 0.0;
    ContactModel malletContact;
    ContactModel wires[kWires];
    bool   wireOn = false;
    double rateScale = 1.0;
    bool   dirty = true;

    // ---- state ----------------------------------------------------------------
    double q[kMaxModes] {}, p[kMaxModes] {};
    double qH = 0.0, pH = 0.0;
    double qm = 1.0, pm = 0.0;
    bool   malletActive = false;

    // ---- scratch (per-substep forces) -----------------------------------------
    double F[kMaxModes] {};
    double FH = 0.0, Fm = 0.0;
};
} // namespace matter
