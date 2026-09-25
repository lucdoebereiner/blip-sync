/*
    BlipSync -- band-limited harmonic-band oscillator with phase I/O and sync.

    A replacement for SuperCollider's Blip with four differences that matter:

      * the band edges are given in Hz, not as a harmonic count, so the
        spectral width stays put when the fundamental moves;
      * the edges are fractional, so sweeping freq / minfreq / maxfreq never
        steps a harmonic on or off and never triggers a crossfade;
      * every input is read per sample, so audio-rate FM and phase modulation
        behave properly;
      * phase is an input *and* an output, and there is a sub-sample-accurate
        hard sync input with polyBLEP step correction.

    Outputs: [ waveform, phase ]   (phase in cycles, 0..1)

    See BlipSyncCore.hpp for the DSP and README.md for measurements.
*/

#include "SC_PlugIn.h"
#include "BlipSyncCore.hpp"

static InterfaceTable* ft;

enum {
    kFreq = 0,
    kMaxFreq,
    kMinFreq,
    kPhase,
    kSync,
    kSyncPhase,
    kSyncMode,  // init-rate: 0 = trigger, 1 = phase ramp
    kIPhase,    // init-rate
    kNormalize, // init-rate: 0 = peak, 1 = RMS, 2 = raw
    kRotate,
    kTilt,
    kTrack,  // init-rate: 0 = band width from freq, 1 = from total phase velocity
    // --- percussion: a strike and the three envelopes it fires. Every one of
    // these is inert at its default, and the arithmetic then reduces to an
    // exact multiply by 1 or add of 0, so the untouched UGen stays bit-exact.
    kStrike, // trigger: restart the envelopes and put the impulse on the edge
    kDecay,  // amplitude, seconds to fall 60 dB. 0 = no amplitude envelope
    kBend,   // pitch multiplier at the strike. 1 = no pitch envelope
    kDamp,   // dB/kHz of extra tilt reached by the end. 0 = no tilt envelope
    kSnap,   // seconds for bend and damp to travel 99% of the way
    kNumInputs
};

// Coefficient of a one-pole decay covering lnFrac of its travel in t seconds.
// Both envelopes are exponential because that is what a struck resonator does,
// and because it costs one multiply per sample. `degenerate` is what a
// non-positive time means, and it differs between the two: an amplitude decay
// of 0 is OFF and must hold the envelope at 1, while a snap of 0 is INSTANT and
// must drop it to 0.
static inline double decayCoef(double t, double sampleDur, double lnFrac, double degenerate) {
    if (!(t > 0.0))
        return degenerate;
    return std::exp(lnFrac * sampleDur / t);
}

// Numerical guard only, never a musical limit: with the phase frozen the model
// asks for an infinitely narrow impulse, i.e. an unbounded harmonic count. This
// caps it far above anything audible (a 65536-harmonic pulse is one sample wide
// at any sane rate) so the closed form stays well conditioned instead of
// producing inf/NaN.
static constexpr double kMaxHarm = 65536.0;

// Phase arrives over a float32 bus, so differencing it toggles by one LSB from
// sample to sample -- differentiation turns quantisation into high-frequency
// noise. Fed straight into the band width that dithers the top edge and sprays
// spurs at about -96 dBFS, so the velocity estimate gets a one-pole at 640 Hz,
// which is exactly where that noise lives and far above anything a musical
// drive signal does. Cost: the band lags a genuinely fast change in drive rate
// by a quarter of a millisecond.
static constexpr double kTrackTau = 0.00025;

struct BlipSync : public Unit {
    double m_phase;
    double m_freq, m_maxfreq, m_minfreq, m_phaseoff, m_syncphase;
    double m_rotate, m_tilt;
    double m_syncPrev;
    double m_strikePrev;
    double m_ampEnv, m_bendEnv;
    double m_bend, m_damp;
    double m_poffPrev;
    double m_trackVel, m_trackCoef;
    bool m_trackInit;
    double m_blep0, m_blep1;
    double m_nyq, m_sampleDur, m_sampleRate;
    blipsync::Band m_band;
    bool m_bandValid;
    int m_syncMode;
    int m_norm;
    bool m_track;
};

// A single input, read either straight from an audio-rate block or ramped
// across the block from its previous control-rate value.
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
    inline bool constant() const { return buf == nullptr && slope == 0.0; }
};

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

extern "C" {
void BlipSync_Ctor(BlipSync* unit);
void BlipSync_next(BlipSync* unit, int inNumSamples);
}

void BlipSync_next(BlipSync* unit, int inNumSamples) {
    float* outSig = OUT(0);
    float* outPhase = OUT(1);

    Ramp fr = makeRamp(unit, kFreq, inNumSamples, unit->m_freq);
    Ramp mx = makeRamp(unit, kMaxFreq, inNumSamples, unit->m_maxfreq);
    Ramp mn = makeRamp(unit, kMinFreq, inNumSamples, unit->m_minfreq);
    Ramp po = makeRamp(unit, kPhase, inNumSamples, unit->m_phaseoff);
    Ramp sp = makeRamp(unit, kSyncPhase, inNumSamples, unit->m_syncphase);
    Ramp ro = makeRamp(unit, kRotate, inNumSamples, unit->m_rotate);
    Ramp tl = makeRamp(unit, kTilt, inNumSamples, unit->m_tilt);
    Ramp bd = makeRamp(unit, kBend, inNumSamples, unit->m_bend);
    Ramp dm = makeRamp(unit, kDamp, inNumSamples, unit->m_damp);

    // The sync input is never ramped: smearing it across the block would move
    // the edge. A control-rate sync signal simply holds for the whole block,
    // which is the usual trigger semantics.
    const float* syncBuf = (INRATE(kSync) == calc_FullRate) ? IN(kSync) : nullptr;
    const double syncK = (double)IN0(kSync);
    const float* strikeBuf = (INRATE(kStrike) == calc_FullRate) ? IN(kStrike) : nullptr;
    const double strikeK = (double)IN0(kStrike);

    // Envelope times are read once per block. They set a rate of change, not a
    // value, so smearing them across a block would be meaningless, and the exp()
    // they cost belongs outside the sample loop.
    const double ampCoef = decayCoef((double)IN0(kDecay), unit->m_sampleDur,
                                     -6.907755278982137, 1.0); // 60 dB; 0 = off
    const double envCoef = decayCoef((double)IN0(kSnap), unit->m_sampleDur,
                                     -4.605170185988091, 0.0); // 99%; 0 = instant

    const bool track = unit->m_track;

    // The band description only has to be rebuilt when one of the three
    // frequencies actually moves; holding it still costs ~2/3 of the CPU.
    // Tracking makes the band depend on the phase input too, so then that input
    // has to be standing still as well.
    // A moving bend or damp envelope moves the band with it.
    const bool percStatic = bd.constant() && dm.constant()
        && (double)IN0(kBend) == 1.0 && (double)IN0(kDamp) == 0.0;
    const bool staticBand = unit->m_bandValid && fr.constant() && mx.constant() && mn.constant()
        && tl.constant() && percStatic && (!track || po.constant());

    blipsync::Band band = unit->m_band;
    const double nyq = unit->m_nyq;
    const double sampleDur = unit->m_sampleDur;
    const double sampleRate = unit->m_sampleRate;
    const int norm = unit->m_norm;
    const int syncMode = unit->m_syncMode;

    double phase = unit->m_phase;
    double syncPrev = unit->m_syncPrev;
    double strikePrev = unit->m_strikePrev;
    double ampEnv = unit->m_ampEnv;
    double bendEnv = unit->m_bendEnv;
    double poffPrev = unit->m_poffPrev;
    double trackVel = unit->m_trackVel;
    bool trackInit = unit->m_trackInit;
    const double trackCoef = unit->m_trackCoef;
    double blep0 = unit->m_blep0;
    double blep1 = unit->m_blep1;

    for (int i = 0; i < inNumSamples; ++i) {
        const double freqIn = fr.next();
        const double maxf = mx.next();
        const double minf = mn.next();
        const double poff = po.next();
        const double sval = syncBuf ? (double)syncBuf[i] : syncK;
        const double stval = strikeBuf ? (double)strikeBuf[i] : strikeK;
        const double starget = sp.next();
        const double rot = ro.next();
        const double tiltIn = tl.next();
        const double bend = bd.next();
        const double damp = dm.next();

        // --- the strike, detected before anything reads the envelopes -------
        bool struck = false;
        double sd = 0.0;
        if (strikePrev <= 0.0 && stval > 0.0) {
            struck = true;
            const double den = stval - strikePrev;
            sd = den > 0.0 ? (-strikePrev / den) : 0.0;
            ampEnv = 1.0;
            bendEnv = 1.0;
        }
        strikePrev = stval;

        // bend rides the envelope down to freq; damp rides it the other way, so
        // the spectrum starts at tilt and darkens to tilt - damp as it rings.
        const double freq = freqIn * (1.0 + ((bend - 1.0) * bendEnv));
        const double tilt = tiltIn - (damp * (1.0 - bendEnv));

        // How fast the waveform is actually being read: freq plus whatever the
        // phase input is doing. Differencing poff rather than the total phase
        // keeps a hard-sync jump (which lands in phase, not poff) out of the
        // estimate, so no special case is needed there. The wrap makes a
        // 0..1 ramp input recover its true increment across the period edge.
        double bandf = freq;
        if (track) {
            double dp = poff - poffPrev;
            dp -= std::floor(dp + 0.5);
            // makeBand only looks at the magnitude, so smooth |velocity|.
            const double v = std::fabs(freq + dp * sampleRate);
            if (trackInit) {
                trackVel += (v - trackVel) * trackCoef;
            } else {
                // Before the drive has moved there is no velocity to filter
                // towards; seeding the state on the first real motion avoids an
                // opening transient where the band is wildly wrong.
                trackVel = v;
                trackInit = (dp != 0.0);
            }
            const double lim = std::min(maxf, nyq) * (1.0 / kMaxHarm);
            bandf = trackVel < lim ? lim : trackVel;
        }
        poffPrev = poff;

        if (!staticBand)
            band = blipsync::makeBand(bandf, minf, maxf, tilt, nyq, norm);

        // With neither tilt nor rotation the original real-valued kernel runs,
        // so the default configuration is bit-for-bit unchanged.
        double rotCos = 1.0, rotSin = 0.0;
        bool plain = band.plain;
        if (rot != 0.0) {
            plain = false;
            rotCos = blipsync::cospi(2.0 * rot);
            rotSin = blipsync::sinpi(2.0 * rot);
        }
        const auto ev = [&](double q) {
            return plain ? blipsync::evalBand(band, q) : blipsync::evalBandRot(band, q, rotCos, rotSin);
        };

        const double inc = freq * sampleDur;

        // --- sync edge detection, with the sub-sample crossing position ----
        bool fired = false;
        double d = 0.0;
        if (syncMode == 0) {
            // trigger: rising crossing of zero
            if (syncPrev <= 0.0 && sval > 0.0) {
                fired = true;
                const double den = sval - syncPrev;
                d = den > 0.0 ? (-syncPrev / den) : 0.0;
            }
        } else {
            // phase ramp master (0..1): a downward jump is the period wrap.
            // For a linear ramp this recovers the crossing time exactly.
            if (sval < syncPrev - 0.5) {
                fired = true;
                const double den = sval + 1.0 - syncPrev;
                d = den > 1.0e-12 ? (1.0 - syncPrev) / den : 0.0;
            }
        }
        syncPrev = sval;
        // A strike lands the impulse on its own edge. If sync fired on the same
        // sample it already owns the reset, so leave that one alone.
        if (struck && !fired) {
            fired = true;
            d = sd;
        }
        if (d < 0.0) d = 0.0;
        if (d > 1.0) d = 1.0;

        const double ph = phase + poff;
        double y = ev(ph);

        if (fired) {
            // The crossing was found between the previous sample and this one,
            // but the correction can only be scheduled into [i, i+1) -- hard
            // sync therefore lands one sample after the edge. Step amplitude is
            // measured between the two trajectories at the crossing instant.
            const double before = ev(phase + d * inc + poff);
            const double after = ev(starget + poff);
            const double D = after - before;
            blep0 += D * (1.0 - d) * (1.0 - d) * 0.5;
            blep1 += -D * d * d * 0.5;
        }

        outSig[i] = (float)((y + blep0) * ampEnv);
        outPhase[i] = blipsync::wrap01f(ph);
        blep0 = blep1;
        blep1 = 0.0;

        if (fired)
            phase = starget + (1.0 - d) * inc;
        else
            phase += inc;
        phase -= std::floor(phase);

        ampEnv *= ampCoef;
        bendEnv *= envCoef;
    }

    if (!std::isfinite(phase))
        phase = 0.0;
    if (!std::isfinite(blep0))
        blep0 = 0.0;
    if (!std::isfinite(blep1))
        blep1 = 0.0;
    if (!std::isfinite(ampEnv))
        ampEnv = 0.0;
    if (!std::isfinite(bendEnv))
        bendEnv = 0.0;

    unit->m_phase = phase;
    unit->m_syncPrev = syncPrev;
    unit->m_strikePrev = strikePrev;
    unit->m_ampEnv = ampEnv;
    unit->m_bendEnv = bendEnv;
    unit->m_poffPrev = poffPrev;
    unit->m_trackVel = trackVel;
    unit->m_trackInit = trackInit;
    unit->m_blep0 = blep0;
    unit->m_blep1 = blep1;
    unit->m_band = band;
    unit->m_bandValid = true;
}

void BlipSync_Ctor(BlipSync* unit) {
    unit->m_sampleDur = (double)SAMPLEDUR;
    unit->m_sampleRate = (double)SAMPLERATE;
    // Keep the top of the band strictly below Nyquist. When the fundamental
    // itself climbs past this, the highest harmonic index falls below 1 and the
    // oscillator fades out rather than folding over.
    unit->m_nyq = (double)SAMPLERATE * 0.5 * 0.995;

    unit->m_syncMode = (int)IN0(kSyncMode);
    {
        int nm = (int)std::floor((double)IN0(kNormalize) + 0.5);
        unit->m_norm = nm < 0 ? 0 : (nm > 2 ? 2 : nm);
    }
    unit->m_track = IN0(kTrack) > 0.5f;

    const double iphase = (double)IN0(kIPhase);
    unit->m_phase = iphase - std::floor(iphase);

    unit->m_freq = (double)IN0(kFreq);
    unit->m_maxfreq = (double)IN0(kMaxFreq);
    unit->m_minfreq = (double)IN0(kMinFreq);
    unit->m_phaseoff = (double)IN0(kPhase);
    unit->m_poffPrev = (double)IN0(kPhase);
    unit->m_trackVel = 0.0;
    unit->m_trackInit = false;
    unit->m_trackCoef = 1.0 - std::exp(-1.0 / (kTrackTau * unit->m_sampleRate));
    unit->m_syncphase = (double)IN0(kSyncPhase);
    unit->m_syncPrev = (double)IN0(kSync);
    unit->m_strikePrev = (double)IN0(kStrike);
    unit->m_bend = (double)IN0(kBend);
    unit->m_damp = (double)IN0(kDamp);
    // Struck at birth, so a one-shot synth needs no trigger at all: just give
    // it a decay and it fires once when it starts.
    unit->m_ampEnv = 1.0;
    unit->m_bendEnv = 1.0;
    unit->m_rotate = (double)IN0(kRotate);
    unit->m_tilt = (double)IN0(kTilt);
    unit->m_blep0 = 0.0;
    unit->m_blep1 = 0.0;
    unit->m_bandValid = false;

    SETCALC(BlipSync_next);

    // Emit one sample, then rewind the state the way Blip does.
    const double savedPhase = unit->m_phase;
    const double savedSync = unit->m_syncPrev;
    const double savedStrike = unit->m_strikePrev;
    const double savedPoff = unit->m_poffPrev;
    BlipSync_next(unit, 1);
    unit->m_phase = savedPhase;
    unit->m_syncPrev = savedSync;
    unit->m_strikePrev = savedStrike;
    unit->m_poffPrev = savedPoff;
    unit->m_ampEnv = 1.0;
    unit->m_bendEnv = 1.0;
    unit->m_trackVel = 0.0;
    unit->m_trackInit = false;
    unit->m_blep0 = 0.0;
    unit->m_blep1 = 0.0;
}

PluginLoad(BlipSyncUGens) {
    ft = inTable;
    DefineSimpleUnit(BlipSync);
}
