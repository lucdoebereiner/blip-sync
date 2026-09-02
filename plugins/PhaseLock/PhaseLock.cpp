/*
    PhaseLock -- a bank of phase-locked oscillators driven by an external
    master phase.

    n slaves, each with its own natural frequency, each pulled toward
    ratio*masterPhase by a proportional phase correction, plus an optional
    mutual (Kuramoto) pull toward the bank's own mean field. Outputs n phase
    ramps in 0..1, which is exactly what BlipSync consumes with freq: 0 and
    track: 1.

        ph[j] += f[j]/sr  +  k * wrap(ratio[j]*master - ph[j])
                          +  mutual * R * sin(2*pi*(psi - ph[j]))

    The point of taking the master as a SIGNAL rather than a frequency is that
    the bank can then be chained: a level's output phases each become the master
    of a bank below it, so a hierarchy is a tree of these, one instance per node,
    with its own coupling strength. Per-slave ratios let a level lock in whole
    number ratios to a slower one above it.

    k is a per-sample fraction of the phase error, so it behaves like a
    first-order loop: the lock is stable for 0 < k < 1 and captures a detuning
    of roughly 0.5*k*sr Hz. Negative values push away from the master.
*/

#include "SC_PlugIn.h"
#include "../BlipSync/BlipSyncCore.hpp"

#include <cmath>

static InterfaceTable* ft;

enum {
    kN = 0,     // init-rate: number of slaves
    kMaster,    // master phase, 0..1
    kK,         // pull toward the master
    kMutual,    // pull toward the bank's own mean field
    kNumFixed   // then n freqs, n ratios (init), n iphases (init)
};

// [-0.5, 0.5): the shortest way round the circle.
static inline double wrapHalf(double x) { return x - std::floor(x + 0.5); }

struct Ramp {
    const float* buf;
    double val, slope;
    inline double next() {
        if (buf)
            return (double)(*buf++);
        const double v = val;
        val += slope;
        return v;
    }
};

struct PhaseLock : public Unit {
    int m_n;
    double* m_phase;  // n
    double* m_fprev;  // n
    double* m_ratio;  // n
    double* m_cs;     // 2n scratch for the mean field
    Ramp* m_fr;       // n
    double m_masterPrev, m_kPrev, m_mutPrev;
    double m_sampleDur;
    bool m_ok;
};

extern "C" {
void PhaseLock_Ctor(PhaseLock* unit);
void PhaseLock_Dtor(PhaseLock* unit);
void PhaseLock_next(PhaseLock* unit, int inNumSamples);
}

static inline Ramp scalarRamp(Unit* unit, int index, int n, double& prev) {
    Ramp r;
    if (INRATE(index) == calc_FullRate) {
        r.buf = IN(index);
        r.val = 0.0;
        r.slope = 0.0;
        prev = (double)IN(index)[n - 1];
    } else {
        const double next = (double)IN0(index);
        r.buf = nullptr;
        r.val = prev;
        r.slope = (next - prev) * (double)unit->mRate->mSlopeFactor;
        prev = next;
    }
    return r;
}

void PhaseLock_next(PhaseLock* unit, int inNumSamples) {
    const int n = unit->m_n;
    double* phase = unit->m_phase;
    double* ratio = unit->m_ratio;
    double* cs = unit->m_cs;
    Ramp* fr = unit->m_fr;
    const double sd = unit->m_sampleDur;

    // The master is a phase ramp: interpolating a control-rate one has to take
    // the short way round, or the block containing a wrap sweeps backwards
    // through the whole cycle.
    const float* mbuf = (INRATE(kMaster) == calc_FullRate) ? IN(kMaster) : nullptr;
    double mval = unit->m_masterPrev, mslope = 0.0;
    if (mbuf) {
        unit->m_masterPrev = (double)IN(kMaster)[inNumSamples - 1];
    } else {
        const double nx = (double)IN0(kMaster);
        mslope = wrapHalf(nx - unit->m_masterPrev) * (double)unit->mRate->mSlopeFactor;
        unit->m_masterPrev = nx;
    }

    Ramp kr = scalarRamp(unit, kK, inNumSamples, unit->m_kPrev);
    Ramp mr = scalarRamp(unit, kMutual, inNumSamples, unit->m_mutPrev);
    for (int j = 0; j < n; ++j)
        fr[j] = scalarRamp(unit, kNumFixed + j, inNumSamples, unit->m_fprev[j]);

    for (int i = 0; i < inNumSamples; ++i) {
        const double master = mbuf ? (double)mbuf[i] : mval;
        double k = kr.next();
        double mut = mr.next();
        mval += mslope;

        if (k > 1.0) k = 1.0; else if (k < -1.0) k = -1.0;
        if (mut > 1.0) mut = 1.0; else if (mut < -1.0) mut = -1.0;

        // Mean field of the bank, from the phases as they stand at the top of
        // the sample, so every slave sees the same field (synchronous update).
        double mc = 0.0, ms = 0.0;
        if (mut != 0.0) {
            for (int j = 0; j < n; ++j) {
                const double c = blipsync::cospi(2.0 * phase[j]);
                const double s = blipsync::sinpi(2.0 * phase[j]);
                cs[j] = c;
                cs[n + j] = s;
                mc += c;
                ms += s;
            }
            mc /= (double)n;
            ms /= (double)n;
        }

        for (int j = 0; j < n; ++j) {
            double p = phase[j] + fr[j].next() * sd;
            p += k * wrapHalf(ratio[j] * master - phase[j]);
            if (mut != 0.0)
                p += mut * ((ms * cs[j]) - (mc * cs[n + j]));
            p -= std::floor(p);
            if (!std::isfinite(p))
                p = 0.0;
            phase[j] = p;
            OUT(j)[i] = blipsync::wrap01f(p);
        }
    }
}

void PhaseLock_Ctor(PhaseLock* unit) {
    const int n = (int)IN0(kN);
    unit->m_n = n < 1 ? 1 : n;
    unit->m_sampleDur = (double)SAMPLEDUR;
    unit->m_ok = false;

    const int m = unit->m_n;
    unit->m_phase = (double*)RTAlloc(unit->mWorld, m * sizeof(double));
    unit->m_fprev = (double*)RTAlloc(unit->mWorld, m * sizeof(double));
    unit->m_ratio = (double*)RTAlloc(unit->mWorld, m * sizeof(double));
    unit->m_cs = (double*)RTAlloc(unit->mWorld, 2 * m * sizeof(double));
    unit->m_fr = (Ramp*)RTAlloc(unit->mWorld, m * sizeof(Ramp));

    if (!unit->m_phase || !unit->m_fprev || !unit->m_ratio || !unit->m_cs || !unit->m_fr) {
        Print("PhaseLock: RTAlloc failed, increase the real time memory size\n");
        SETCALC(ClearUnitOutputs);
        ClearUnitOutputs(unit, 1);
        return;
    }
    unit->m_ok = true;

    for (int j = 0; j < m; ++j) {
        unit->m_ratio[j] = (double)IN0(kNumFixed + m + j);
        const double ip = (double)IN0(kNumFixed + 2 * m + j);
        unit->m_phase[j] = ip - std::floor(ip);
        unit->m_fprev[j] = (double)IN0(kNumFixed + j);
    }
    unit->m_masterPrev = (double)IN0(kMaster);
    unit->m_kPrev = (double)IN0(kK);
    unit->m_mutPrev = (double)IN0(kMutual);

    SETCALC(PhaseLock_next);
    for (int j = 0; j < m; ++j)
        OUT(j)[0] = blipsync::wrap01f(unit->m_phase[j]);
}

void PhaseLock_Dtor(PhaseLock* unit) {
    if (!unit->m_ok)
        return;
    RTFree(unit->mWorld, unit->m_phase);
    RTFree(unit->mWorld, unit->m_fprev);
    RTFree(unit->mWorld, unit->m_ratio);
    RTFree(unit->mWorld, unit->m_cs);
    RTFree(unit->mWorld, unit->m_fr);
}

PluginLoad(PhaseLockUGens) {
    ft = inTable;
    DefineDtorCantAliasUnit(PhaseLock);
}
