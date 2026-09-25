/*
    BlipSync -- percussion.

    A struck resonator is a spectrum whose high harmonics die faster than its
    low ones. tilt weights harmonic k by r^(k-1), so sweeping tilt down over
    time IS that decay: w_k(t) = e^(-k*t/tau), the k-th harmonic dying k times
    faster than the fundamental. A drum is a pitch envelope plus a tilt
    envelope, and both live in the UGen:

        BlipSync.perc(freq, decay, bend, damp, tilt,
                      sync, syncMode, maxfreq, beater, minfreq, mul, add)

    Three numbers do the work:

        decay   how long it rings   (seconds to fall 60 dB)
        bend    pitch multiplier at the strike, falling to freq
        damp    how fast the spectrum dies. THE shape control:
                0.004 a click, 0.09 a thump, and longer than decay
                means the ring stays bright

    Everything else is a parameter that already existed. tilt is how bright the
    strike is, maxfreq how far that brightness reaches, beater (rotate) how hard
    the stick is. There is no strike input because sync already is one -- and
    the UGen is born struck, so a synth-per-note needs no trigger at all.

    perc uses normalize: 2 (raw), which is what makes any of it audible: the
    other two modes are level-preserving by design, so a collapsing band stays
    as loud as the sine it collapses into and there is no attack to be had. Raw
    means your gain IS the level of the ringing body.

    Evaluate one block at a time. cmd-period stops.
*/

s.boot;


// ---------------------------------------------------------------------------
// 1. A KICK.  Complete. No envelopes, no trigger.
// ---------------------------------------------------------------------------
{ BlipSync.perc(46) * 0.12 ! 2 }.play


// ---------------------------------------------------------------------------
// 2. DAMP IS THE BEATER.  Mouse X from a click to a ring, live.
// ---------------------------------------------------------------------------
(
{ BlipSync.perc(46, damp: MouseX.kr(0.004, 0.6, 1), sync: Impulse.ar(2)) * 0.12 ! 2 }.play
)


// ---------------------------------------------------------------------------
// 3. THE THREE NUMBERS, one at a time.
// ---------------------------------------------------------------------------
(
SynthDef(\bsPerc, { |out = 0, amp = 0.12, freq = 46, decay = 0.45, bend = 4.2,
                    damp = 0.045, tilt = -6, beater = 0, top = 9000, lo = 0, pan = 0|
    var sig = BlipSync.perc(freq, decay, bend, damp, tilt,
                            maxfreq: top, beater: beater, minfreq: lo);
    DetectSilence.ar(sig, 0.0001, 0.05, doneAction: 2);
    Out.ar(out, Pan2.ar(sig * amp, pan))
}).add;
)

Synth(\bsPerc);                                         // the kick
Synth(\bsPerc, [\damp, 0.004, \amp, 0.1]);              // click
Synth(\bsPerc, [\damp, 0.5, \amp, 0.1]);                // rings bright
Synth(\bsPerc, [\beater, 0.22, \amp, 0.3]);             // soft stick
Synth(\bsPerc, [\bend, 1]);                             // no pitch drop
Synth(\bsPerc, [\bend, 9, \decay, 0.9]);                // long 808 drop
Synth(\bsPerc, [\tilt, 0, \amp, 0.02]);                 // brighter strike
Synth(\bsPerc, [\top, 1200, \amp, 0.3]);                // duller strike


// ---------------------------------------------------------------------------
// 4. THE FAMILY.  Same synth, different numbers.
// ---------------------------------------------------------------------------
Synth(\bsPerc, [\freq, 46,  \decay, 0.45, \bend, 4.2, \damp, 0.045]);                    // kick
Synth(\bsPerc, [\freq, 150, \decay, 0.35, \bend, 2.0, \damp, 0.09, \tilt, -4, \amp, 0.07]); // tom
Synth(\bsPerc, [\freq, 800, \decay, 0.08, \bend, 1.2, \damp, 0.008, \tilt, 0, \amp, 0.01]); // click
Synth(\bsPerc, [\freq, 190, \decay, 0.5,  \bend, 1.0, \damp, 0.4, \lo, 1400,
                \tilt, -2, \amp, 0.02]);                                                    // rattle


// ---------------------------------------------------------------------------
// 5. INTO THE CHAIN.  Resonant lowpass, then tanh. The band limit stays under
//    the filter, so nothing folds before the distortion gets to it.
// ---------------------------------------------------------------------------
(
SynthDef(\bsPercChain, { |out = 0, amp = 0.6, drive = 3, freq = 44|
    var tr  = Impulse.ar(0);
    var cut = EnvGen.ar(Env([4000, 300], [0.05], -5), tr);
    var sig = BlipSync.perc(freq, 0.5, 4.5, 0.05, -4, beater: 0.15) * 0.1;
    var wet = (RLPF.ar(sig, cut, 0.5) * drive).tanh * amp;
    DetectSilence.ar(wet, 0.0001, 0.05, doneAction: 2);
    Out.ar(out, wet ! 2)
}).add;
)

Synth(\bsPercChain);


// ---------------------------------------------------------------------------
// 6. A PATTERN.
// ---------------------------------------------------------------------------
(
Pbind(
    \instrument, \bsPerc,
    \dur,    Pseq([0.5, 0.25, 0.25, 0.5, 0.25, 0.25], inf),
    \freq,   Pseq([ 46,  46, 150,  46, 800, 150], inf),
    \decay,  Pseq([0.45, 0.35, 0.35, 0.45, 0.08, 0.35], inf),
    \bend,   Pseq([4.2, 4.2, 2.0, 4.2, 1.2, 2.0], inf),
    \damp,   Pwhite(0.02, 0.09),
    \beater, Pwhite(0.0, 0.18),
    \amp,    Pseq([0.12, 0.05, 0.05, 0.12, 0.01, 0.05], inf),
    \pan,    Pseq([0, -0.3, 0.3, 0, 0.5, -0.4], inf)
).play
)


// ---------------------------------------------------------------------------
// 7. DRUMS ON A PHASE NETWORK.  sync IS the strike, and syncMode 1 takes a
//    0..1 ramp -- so a PhaseLock output strikes a drum on every wrap. One
//    conductor, four voices at whole ratios, no triggers built by hand.
// ---------------------------------------------------------------------------
(
{
    var cf = 2.4, ratios = [1, 2, 3, 5];
    var master = Phasor.ar(0, cf * SampleDur.ir, 0, 1);
    var ph = PhaseLock.ar(master, cf * ratios, 3, ratios: ratios,
                          iphases: [0, 0.25, 0.5, 0.75]);
    var freq  = [46, 150, 380, 800];
    var decay = [0.45, 0.3, 0.12, 0.06];
    var damp  = [0.045, 0.03, 0.012, 0.008];
    var amp   = [0.12, 0.05, 0.02, 0.008];
    Splay.ar(Array.fill(4, { |i|
        BlipSync.perc(freq[i], decay[i], 4.2, damp[i], -6, ph[i], 1) * amp[i]
    })) * 0.8
}.play
)
