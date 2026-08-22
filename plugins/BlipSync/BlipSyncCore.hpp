/*
    BlipSyncCore.hpp -- band-limited harmonic-band oscillator kernel.

    Evaluates, in closed form and at arbitrary (double) phase,

        y(p) = (1 / W) * sum_{k>=1} w_k * cos(2*pi*k*p)

    with a trapezoidal weight window over harmonic number k:

        w_k = clip(highN - k + 1, 0, 1) * clip(k - lowN + 1, 0, 1)

    where lowN = minfreq/freq and highN = maxfreq/freq are *fractional*
    harmonic indices derived from band edges given in Hz. W = sum_k w_k, so
    the waveform peaks at 1.0 regardless of how many harmonics are active.

    Because the band edges are fractional, a harmonic fades in/out linearly
    instead of switching on/off. That is the whole trick: the spectrum is a
    continuous function of freq, minfreq and maxfreq, so sweeping any of them
    produces no stepping, no crossfade transients and no zipper noise.

    Closed form -- with s = pi*p, A the lowest full-weight harmonic and B the
    highest:

        sum_{k=A}^{B} cos(2*k*s) = [sin((2B+1)s) - sin((2A-1)s)] / (2*sin(s))

    Evaluated naively this has a 0/0 singularity at every period (exactly
    where the impulse peak is, i.e. the loudest part of the waveform).
    Factoring pi*p out of numerator and denominator via sincpi(y)=sin(pi*y)/(pi*y)
    removes it analytically:

        = [(2B+1)*sincpi((2B+1)p) - (2A-1)*sincpi((2A-1)p)] / (2*sincpi(p))

    The denominator sincpi(p) only ever ranges over [2/pi, 1] for p in
    [-1/2, 1/2], so the expression is well conditioned everywhere. No
    reciprocal table, no bad-value guards, no special cases.
*/

#ifndef BLIPSYNC_CORE_HPP
#define BLIPSYNC_CORE_HPP

#include <cmath>
#include <algorithm>

namespace blipsync {

constexpr double kPi = 3.14159265358979323846;

// ---------------------------------------------------------------------------
// sin(pi*t), t in [-0.5, 0.5].  Taylor through t^15; max abs error 6.0e-12
// (-225 dB), i.e. below the double-precision noise of everything around it and
// ~3 times faster than a libm sin() call.
// ---------------------------------------------------------------------------
inline double sinpiHalf(double t) {
    const double z = t * t;
    double a = -2.19153534478302173e-05;
    a = a * z + 4.66302805767612554e-04;
    a = a * z + -7.37043094571435044e-03;
    a = a * z + 8.21458866111282326e-02;
    a = a * z + -5.99264529320792105e-01;
    a = a * z + 2.55016403987734552e+00;
    a = a * z + -5.16771278004997026e+00;
    a = a * z + 3.14159265358979312e+00;
    return a * t;
}

// sin(pi*y) for arbitrary y. Reduces mod 2 then folds into [-0.5, 0.5].
inline double sinpi(double y) {
    double w = y - 2.0 * std::floor(y * 0.5 + 0.5); // -> [-1, 1]
    if (w > 0.5)
        w = 1.0 - w;
    else if (w < -0.5)
        w = -1.0 - w;
    return sinpiHalf(w);
}

inline double cospi(double y) { return sinpi(y + 0.5); }

// sin(pi*y)/(pi*y), exact in the limit y -> 0.
inline double sincpi(double y) {
    if (std::fabs(y) < 1.0e-3) {
        const double u = kPi * y;
        const double u2 = u * u;
        return 1.0 - u2 * (1.0 / 6.0) * (1.0 - u2 * (1.0 / 20.0));
    }
    return sinpi(y) / (kPi * y);
}

inline double clip01(double x) { return x < 0.0 ? 0.0 : (x > 1.0 ? 1.0 : x); }

// ---------------------------------------------------------------------------
// Band description, recomputed per sample (cheap: a divide and two floors).
// ---------------------------------------------------------------------------
struct Band {
    double aFull; // lowest full-weight harmonic
    double bFull; // highest full-weight harmonic (bFull < aFull => empty)
    double loIdx; // index of the partial low-edge harmonic (0 => unused)
    double loAmp; // its weight
    double hiIdx; // index of the partial high-edge harmonic
    double hiAmp; // its weight
    double norm;  // output scaling
};

// nyq: highest permissible harmonic frequency (a hair under sr/2).
// rms:  false -> peak-normalised (waveform peaks at 1, like Blip)
//       true  -> RMS-normalised (roughly constant loudness across bandwidth)
inline Band makeBand(double freq, double minfreq, double maxfreq, double nyq, bool rms) {
    const double af = std::fabs(freq);
    Band b;

    // freq == 0: nothing to build; the oscillator sits still and silent.
    const double inv = af > 1.0e-9 ? 1.0 / af : 0.0;

    // Fractional harmonic indices of the two band edges.
    double highN = std::min(maxfreq, nyq) * inv;
    double lowN = minfreq * inv;
    if (!(highN > 0.0))
        highN = 0.0;
    if (!(lowN > 1.0))
        lowN = 1.0; // harmonic 0 is DC and is never included

    const double bF = std::floor(highN);
    const double hiFrac = highN - bF;
    const double lF = std::floor(lowN);
    double aF = lF + 1.0;
    double loFrac = 1.0 - (lowN - lF);

    // lowN sitting exactly on a harmonic means the partial term has weight 1;
    // absorb it into the closed-form sum and save a cosine.
    if (loFrac >= 1.0) {
        aF = lF;
        loFrac = 0.0;
    }

    b.aFull = aF;
    b.bFull = bF;
    b.loIdx = lF;
    b.hiIdx = bF + 1.0;

    // Each partial edge harmonic carries BOTH window factors, so a band
    // narrower than one harmonic -- or an inverted one, minfreq above maxfreq --
    // degrades to the right answer instead of a spurious tone.
    b.loAmp = loFrac * clip01(highN - lF + 1.0);
    b.hiAmp = hiFrac * clip01(b.hiIdx - lowN + 1.0);

    // The interior harmonics are all at or below min(maxfreq, nyq) by
    // construction, but the fractional top edge sits one harmonic higher and
    // can cross Nyquist when maxfreq is pushed right up against it. Drop it
    // when it does: a fractional harmonic above Nyquist is exactly the fold-over
    // this UGen exists to avoid. Consequence worth knowing: keeping maxfreq at
    // least one harmonic below Nyquist is what buys the smooth top edge.
    if (b.hiIdx * af > nyq)
        b.hiAmp = 0.0;
    if (b.loIdx * af > nyq)
        b.loAmp = 0.0;

    double W = b.loAmp + b.hiAmp;
    double W2 = b.loAmp * b.loAmp + b.hiAmp * b.hiAmp;
    if (bF >= aF) {
        const double n = bF - aF + 1.0;
        W += n;
        W2 += n;
    }

    if (rms) {
        // sum w_k cos(...) has RMS sqrt(W2/2), so 1/sqrt(W2) puts the output at
        // the RMS of a unit-amplitude sine (0.707) for any bandwidth. The peak
        // is then sqrt(W2) and can be well above 1 -- that is the price of
        // constant loudness for an impulse train. Below W2 = 1 the band is
        // fading out and the scaling is left at unity so it can reach silence.
        b.norm = W2 > 1.0 ? 1.0 / std::sqrt(W2) : 1.0;
    } else {
        // Peak normalisation: y(0) = W/max(W,1). Dividing by max(W,1) rather
        // than W lets the last harmonic fade to silence instead of being
        // renormalised back up to full level.
        b.norm = W > 1.0 ? 1.0 / W : 1.0;
    }
    return b;
}

// ---------------------------------------------------------------------------
// Waveform. p is phase in cycles; any real value is accepted.
// ---------------------------------------------------------------------------
inline double evalBand(const Band& b, double pIn) {
    // Reduce to [-0.5, 0.5) first. cos(2*pi*k*p) has period 1 and the closed
    // form is even in p, so this is exact -- and it is what keeps sincpi(p)
    // bounded in [2/pi, 1]. Without it, a phase one ulp below 1.0 makes
    // sin(pi*p) cancel to exactly zero and the whole expression blows up.
    const double p = pIn - std::floor(pIn + 0.5);
    double acc = 0.0;

    if (b.bFull >= b.aFull) {
        const double hi = 2.0 * b.bFull + 1.0;
        const double lo = 2.0 * b.aFull - 1.0;
        acc = (hi * sincpi(hi * p) - lo * sincpi(lo * p)) / (2.0 * sincpi(p));
    }
    if (b.loAmp > 0.0)
        acc += b.loAmp * cospi(2.0 * b.loIdx * p);
    if (b.hiAmp > 0.0)
        acc += b.hiAmp * cospi(2.0 * b.hiIdx * p);

    return acc * b.norm;
}

// Wrap to [0, 1). The clamp matters: a double just under 1.0 rounds up to
// exactly 1.0 in float, which would put a spurious value at the top of the
// phase output's range.
inline float wrap01f(double p) {
    const float f = (float)(p - std::floor(p));
    return f >= 1.0f ? 0.99999994f : f;
}

} // namespace blipsync

#endif
