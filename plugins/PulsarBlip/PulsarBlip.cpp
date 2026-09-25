/*
    PulsarBlip -- polyphonic pulsar generator built on the BlipSync kernel.

    Each trigger starts a GRAIN on one of n internal voices. A grain is a
    band-limited blip train at `freq`, decaying in amplitude over `decay` and in
    brightness over `damp` -- exactly the percussion machinery of BlipSync, but
    with its own phase and its own copy of every parameter, so grains overlap
    instead of replacing one another and each one keeps the settings it was born
    with.

    That is pulsar synthesis in its usual terms:

        emission rate   the trigger              pitch above ~20 Hz, rhythm below
        pulsaret freq   freq                     a formant, NOT the pitch
        pulsaret dur    decay                    blips per grain ~= decay * freq
        pulsaret shape  maxfreq / tilt / damp / rotate
        duty cycle      decay * emission rate    > 1 means grains overlap

    Two things this does that n separate oscillators cannot:

    * Every parameter is SNAPSHOT at the moment the grain is born, so modulating
      freq or decay scatters the cloud instead of bending all of it together.
    * The onset is band-matched. A grain would otherwise start with its
      amplitude stepping 0 -> 1 at the exact instant the blip is at its peak,
      which is a discontinuity and therefore broadband. Instead the phase starts
      a few samples EARLY and a raised cosine of the same length brings the
      amplitude up, so the impulse arrives precisely as the window reaches 1.
      The length is tied to the band limit -- two periods of maxfreq, which is
      the only timescale the waveform has -- so it is always just long enough
      and never audible.

    Normalisation is raw: the fundamental of each grain sits at 1.0 and the peak
    is the harmonic count, so n overlapping grains can reach n times that. Scale
    outside.
*/

#include "SC_PlugIn.h"
#include "../BlipSync/BlipSyncCore.hpp"

#include <cmath>

static InterfaceTable* ft;

enum {
    kTrig = 0,  // emission trigger, or a 0..1 phase ramp under kTrigMode 1
    kFreq,      // pulsaret frequency -- the formant. Snapshot per grain.
    kDecay,     // grain duration, seconds to fall 60 dB. Snapshot.
    kDamp,      // seconds for the spectrum to fall to a sine. Snapshot.
    kBend,      // pitch multiplier at the onset. Snapshot.
    kTilt,      // spectral slope at the onset, dB/kHz. Snapshot.
    kRotate,    // impulse asymmetry, cycles. Snapshot.
    kMaxFreq,   // band limit -- live, because it is the anti-aliasing limit
    kMinFreq,   // live
    kNumVoices, // init-rate
    kTrigMode,  // init-rate: 0 = trigger, 1 = 0..1 phase ramp, fires on the wrap
    kNumInputs
};

static constexpr int kMaxVoices = 64;
static constexpr double kOnsetPeriods = 2.0; // of maxfreq
static constexpr double kMinDecay = 0.0005;  // s; a voice must be able to die
static constexpr double kVoiceFloor = 1.0e-6;

struct Voice {
    double phase, inc;
    double amp, bendEnv, tiltEnv;
    double ampCoef, envCoef, tiltStep;
    double freq, tilt, bend, dampTo;
    double rotCos, rotSin;
    double onsetPos, onsetInc; // 0 -> 1 across the raised-cosine window
    bool shaped, plainRot, frozen, active;
    blipsync::Band band;
};

struct PulsarBlip : public Unit {
    Voice* m_v;
    int m_n;
    double m_nyq, m_sampleDur, m_sampleRate;
    double m_trigPrev;
    int m_trigMode;
    bool m_ok;
};

extern "C" {
void PulsarBlip_Ctor(PulsarBlip* unit);
void PulsarBlip_Dtor(PulsarBlip* unit);
void PulsarBlip_next(PulsarBlip* unit, int inNumSamples);
}

static inline double readIn(Unit* unit, int index, int i) {
    return (INRATE(index) == calc_FullRate) ? (double)IN(index)[i] : (double)IN0(index);
}

// Start a grain on the quietest voice, taking a copy of everything.
static void startGrain(PulsarBlip* unit, int i, double maxf) {
    const int n = unit->m_n;
    Voice* vs = unit->m_v;
    int pick = 0;
    double quietest = 1.0e300;
    for (int j = 0; j < n; ++j) {
        if (!vs[j].active) { pick = j; quietest = -1.0; break; }
        if (vs[j].amp < quietest) { quietest = vs[j].amp; pick = j; }
    }
    Voice& v = vs[pick];

    const double sd = unit->m_sampleDur;
    double decay = readIn(unit, kDecay, i);
    if (decay < kMinDecay) decay = kMinDecay;
    const double damp = readIn(unit, kDamp, i);
    const double rot = readIn(unit, kRotate, i);

    v.freq = readIn(unit, kFreq, i);
    v.tilt = readIn(unit, kTilt, i);
    v.bend = readIn(unit, kBend, i);
    v.shaped = damp > 0.0;
    v.dampTo = 60000.0 / (std::fabs(v.freq) > 1.0 ? std::fabs(v.freq) : 1.0);

    v.ampCoef = std::exp(-6.907755278982137 * sd / decay);
    v.envCoef = v.shaped ? std::exp(-6.907755278982137 * sd / damp) : 0.0;
    v.tiltStep = v.shaped ? sd / damp : 0.0;

    v.plainRot = (rot == 0.0);
    v.rotCos = v.plainRot ? 1.0 : blipsync::cospi(2.0 * rot);
    v.rotSin = v.plainRot ? 0.0 : blipsync::sinpi(2.0 * rot);

    v.amp = 1.0;
    v.bendEnv = 1.0;
    v.tiltEnv = 1.0;

    // Band-matched onset: the only timescale the waveform has is 1/maxfreq, so
    // the window is two of those. Start the phase that far early and the blip
    // peak lands exactly as the window reaches 1.
    //
    // The window is a DURATION, never a sample count. Rounding it to samples
    // would make the grain's onset time depend on the sample rate -- which
    // sounds harmless and is not: it shifts every grain by a few microseconds,
    // and for impulses that narrow a few microseconds is a large error.
    const double top = maxf > 1.0 ? maxf : 1.0;
    double onsetDur = kOnsetPeriods / top;
    const double maxDur = 1024.0 * sd;
    if (onsetDur > maxDur)
        onsetDur = maxDur;
    v.onsetInc = sd / onsetDur;
    v.onsetPos = 0.0;

    const double f0 = v.freq * v.bend;
    v.inc = f0 * sd;
    v.phase = -(onsetDur * f0);
    v.phase -= std::floor(v.phase);

    v.frozen = false;
    v.active = true;
}

void PulsarBlip_next(PulsarBlip* unit, int inNumSamples) {
    float* out = OUT(0);
    if (!unit->m_ok) {
        for (int i = 0; i < inNumSamples; ++i) out[i] = 0.f;
        return;
    }
    const int n = unit->m_n;
    Voice* vs = unit->m_v;
    const double sd = unit->m_sampleDur;
    const double nyq = unit->m_nyq;
    const int trigMode = unit->m_trigMode;
    double trigPrev = unit->m_trigPrev;

    // A voice may only freeze its band while the band limits are standing still.
    const bool bandStatic =
        INRATE(kMaxFreq) != calc_FullRate && INRATE(kMinFreq) != calc_FullRate;

    for (int i = 0; i < inNumSamples; ++i) {
        const double maxf = readIn(unit, kMaxFreq, i);
        const double minf = readIn(unit, kMinFreq, i);
        const double tv = readIn(unit, kTrig, i);

        bool fire = false;
        if (trigMode == 0)
            fire = (trigPrev <= 0.0 && tv > 0.0);
        else
            fire = (tv < trigPrev - 0.5);
        trigPrev = tv;
        if (fire)
            startGrain(unit, i, maxf);

        double acc = 0.0;
        for (int j = 0; j < n; ++j) {
            Voice& v = vs[j];
            if (!v.active)
                continue;

            if (!v.frozen) {
                const double be = v.shaped ? v.bendEnv : 0.0;
                const double f = v.freq * (1.0 + ((v.bend - 1.0) * be));
                const double t = v.tilt - (v.shaped ? v.dampTo * (1.0 - v.tiltEnv) : 0.0);
                v.band = blipsync::makeBand(f, minf, maxf, t, nyq, 2);
                v.inc = f * sd;
                // Once both shaping envelopes have run out the band stops
                // moving, so most of a grain's life costs only the waveform.
                if (bandStatic && v.tiltEnv <= 0.0 && v.bendEnv < 1.0e-7)
                    v.frozen = true;
            }

            double y = v.plainRot ? blipsync::evalBand(v.band, v.phase)
                                  : blipsync::evalBandRot(v.band, v.phase, v.rotCos, v.rotSin);
            double g = v.amp;
            if (v.onsetPos < 1.0) {
                g *= 0.5 * (1.0 - blipsync::cospi(v.onsetPos)); // raised cosine 0 -> 1
                v.onsetPos += v.onsetInc;
            }
            acc += y * g;

            v.phase += v.inc;
            v.phase -= std::floor(v.phase);
            v.amp *= v.ampCoef;
            v.bendEnv *= v.envCoef;
            v.tiltEnv -= v.tiltStep;
            if (v.tiltEnv < 0.0)
                v.tiltEnv = 0.0;
            if (v.amp < kVoiceFloor || !std::isfinite(v.amp))
                v.active = false;
        }
        out[i] = (float)acc;
    }
    unit->m_trigPrev = trigPrev;
}

void PulsarBlip_Ctor(PulsarBlip* unit) {
    int n = (int)IN0(kNumVoices);
    if (n < 1) n = 1;
    if (n > kMaxVoices) n = kMaxVoices;
    unit->m_n = n;
    unit->m_sampleDur = (double)SAMPLEDUR;
    unit->m_sampleRate = (double)SAMPLERATE;
    unit->m_nyq = (double)SAMPLERATE * 0.5 * 0.995;
    unit->m_trigMode = (int)IN0(kTrigMode);
    unit->m_trigPrev = (double)IN0(kTrig);
    unit->m_ok = false;

    unit->m_v = (Voice*)RTAlloc(unit->mWorld, n * sizeof(Voice));
    if (!unit->m_v) {
        Print("PulsarBlip: RTAlloc failed, raise s.options.memSize\n");
        SETCALC(ClearUnitOutputs);
        ClearUnitOutputs(unit, 1);
        return;
    }
    unit->m_ok = true;
    for (int j = 0; j < n; ++j) {
        unit->m_v[j] = Voice();
        unit->m_v[j].active = false;
        unit->m_v[j].amp = 0.0;
    }
    SETCALC(PulsarBlip_next);
    OUT(0)[0] = 0.f;
}

void PulsarBlip_Dtor(PulsarBlip* unit) {
    if (unit->m_ok)
        RTFree(unit->mWorld, unit->m_v);
}

PluginLoad(PulsarBlipUGens) {
    ft = inTable;
    DefineDtorCantAliasUnit(PulsarBlip);
}
