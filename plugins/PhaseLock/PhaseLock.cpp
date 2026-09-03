/*
    PhaseLock -- a bank of phase-locked oscillators driven by an external
    master phase.

    n slaves, each with its own natural frequency, each pulled toward the master
    at a rational ratio, plus an optional mutual (Kuramoto) pull toward the
    bank's own mean field. Outputs n phase ramps in 0..1, which is exactly what
    BlipSync consumes with freq: 0 and track: 1.

        e      = wrap(num[j]*master - den[j]*ph[j])      // in [-0.5, 0.5)
        ph[j] += freq[j]/sr  +  (k/den[j]) * e  +  mutual * R*sin(2pi*(psi - ph[j]))

    Locking num:den means den slave cycles per num master cycles, so 1:1 is
    unison, 3:1 is three pulses per master cycle and 1:3 is one per three. Both
    are rounded to integers: theta -> n*theta is a well defined map of the circle
    only for integer n, and a fractional one puts a jump of frac(n) into the
    target at every wrap of the master -- a periodic kick, not a lock. Rounding
    means num and den can be modulated: they step cleanly between lock ratios
    instead of sliding through undefined ones.

    The point of taking the master as a SIGNAL rather than a frequency is that
    the bank can then be chained: a level's output phases each become the master
    of a bank below it, so a hierarchy is a tree of these, one instance per node,
    with its own coupling strength.

    k and mutual are both in Hz, and both mean the same thing: the largest
    frequency pull the term can exert, which for k is also its capture range --
    a slave detuned from num/den*masterFreq by less than k/den Hz locks, one
    detuned by more slips. Being frequencies they are sample rate independent.
    The loop settles with a time constant of 1/(2k) seconds.
*/

#include "SC_PlugIn.h"
#include "../BlipSync/BlipSyncCore.hpp"

#include <cmath>

static InterfaceTable* ft;

enum {
    kN = 0,     // init-rate: number of slaves
    kMaster,    // master phase, 0..1
    kK,         // pull toward the master, Hz
    kMutual,    // pull toward the bank's own mean field, Hz
    kNumFixed   // then n freqs, n ratios, n subs, n iphases (iphases init-rate)
};

// [-0.5, 0.5): the shortest way round the circle.
static inline double wrapHalf(double x) { return x - std::floor(x + 0.5); }

// Deterministic round-half-up, independent of the FPU rounding mode.
static inline double roundInt(double x) { return std::floor(x + 0.5); }

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
    double* m_phase; // n
    double* m_prev;  // 3n: freqs, ratios, subs
    double* m_cs;    // 2n scratch for the mean field
    Ramp* m_ramp;    // 3n
    double m_masterPrev, m_kPrev, m_mutPrev;
    double m_sampleDur;
    bool m_ok;
};

extern "C" {
void PhaseLock_Ctor(PhaseLock* unit);
void PhaseLock_Dtor(PhaseLock* unit);
void PhaseLock_next(PhaseLock* unit, int inNumSamples);
}

static inline Ramp makeRamp(Unit* unit, int index, int n, double& prev) {
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
    double* cs = unit->m_cs;
    Ramp* rp = unit->m_ramp;
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

    Ramp kr = makeRamp(unit, kK, inNumSamples, unit->m_kPrev);
    Ramp mr = makeRamp(unit, kMutual, inNumSamples, unit->m_mutPrev);
    for (int j = 0; j < 3 * n; ++j)
        rp[j] = makeRamp(unit, kNumFixed + j, inNumSamples, unit->m_prev[j]);

    for (int i = 0; i < inNumSamples; ++i) {
        const double master = mbuf ? (double)mbuf[i] : mval;
        mval += mslope;

        // Hz -> per-sample coefficients. The k term acts on an error bounded by
        // 0.5 and the mutual term on one bounded by 1, so the factor of two
        // makes both mean "this many Hz of pull at most".
        double k = 2.0 * kr.next() * sd;
        double mut = mr.next() * sd;
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
            const double num = roundInt(rp[n + j].next());
            double den = roundInt(rp[2 * n + j].next());
            if (den < 1.0)
                den = 1.0;

            double p = phase[j] + rp[j].next() * sd;
            p += (k / den) * wrapHalf((num * master) - (den * phase[j]));
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
    unit->m_prev = (double*)RTAlloc(unit->mWorld, 3 * m * sizeof(double));
    unit->m_cs = (double*)RTAlloc(unit->mWorld, 2 * m * sizeof(double));
    unit->m_ramp = (Ramp*)RTAlloc(unit->mWorld, 3 * m * sizeof(Ramp));

    if (!unit->m_phase || !unit->m_prev || !unit->m_cs || !unit->m_ramp) {
        Print("PhaseLock: RTAlloc failed, raise s.options.memSize\n");
        SETCALC(ClearUnitOutputs);
        ClearUnitOutputs(unit, 1);
        return;
    }
    unit->m_ok = true;

    for (int j = 0; j < 3 * m; ++j)
        unit->m_prev[j] = (double)IN0(kNumFixed + j);
    for (int j = 0; j < m; ++j) {
        const double ip = (double)IN0(kNumFixed + 3 * m + j);
        unit->m_phase[j] = ip - std::floor(ip);
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
    RTFree(unit->mWorld, unit->m_prev);
    RTFree(unit->mWorld, unit->m_cs);
    RTFree(unit->mWorld, unit->m_ramp);
}

PluginLoad(PhaseLockUGens) {
    ft = inTable;
    DefineDtorCantAliasUnit(PhaseLock);
}
