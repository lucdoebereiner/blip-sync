/*
    BlipSync -- percussion.

    A struck resonator is a spectrum whose high harmonics die faster than its
    low ones. tilt weights harmonic k by r^(k-1), so sweeping tilt downward over
    time IS that decay: w_k(t) = e^(-k*t/tau), the k-th harmonic dying k times
    faster than the fundamental. A drum is a pitch envelope plus a tilt
    envelope -- and both live inside the UGen, so it is one call:

        BlipSync.perc(strike, freq, decay, bend, damp, snap, tilt,
                      maxfreq, minfreq, beater, mul, add)

        strike  trigger; restarts the envelopes and puts the impulse on the edge
        decay   amplitude, seconds to fall 60 dB
        bend    pitch multiplier at the strike, falling to freq
        damp    dB/kHz of extra tilt by the end: the attack
        snap    seconds for bend and damp to complete. THE beater control
        tilt    where the spectrum sits once it has rung out
        beater  rotate; spreads the impulse in time. 0 hard, 0.25 soft

    perc uses normalize: 2 (raw), which is what makes any of it audible: the
    other two modes are level-preserving by design, so a collapsing band stays
    as loud as the sine it collapses into and there is no attack to be had. Raw
    means your gain IS the level of the ringing body, and the attack rises above
    it by however many harmonics the band opens at t=0.

    Evaluate one block at a time. cmd-period stops.
*/

s.boot;


// ---------------------------------------------------------------------------
// 1. A KICK, in one UGen. No envelopes, no trigger -- it is born struck, so a
//    one-shot synth just plays.
// ---------------------------------------------------------------------------
(
{ BlipSync.perc(freq: 46, decay: 0.45, bend: 4.2, damp: 600, snap: 0.045,
                tilt: -6) * 0.12 ! 2 }.play
)


// ---------------------------------------------------------------------------
// 2. SNAP IS THE BEATER.  Mouse X from a click to a thump, live.
// ---------------------------------------------------------------------------
(
{
    BlipSync.perc(Impulse.ar(2), 46, 0.45, 4.2, 600, MouseX.kr(0.004, 0.08),
                  tilt: -6) * 0.12 ! 2
}.play
)


// ---------------------------------------------------------------------------
// 3. THE FOUR CONTROLS, one at a time. Play them and move one number.
// ---------------------------------------------------------------------------
(
SynthDef(\bsPerc, { |out = 0, amp = 0.12, freq = 46, decay = 0.45, bend = 4.2,
                    damp = 600, snap = 0.045, tilt = -6, beater = 0,
                    top = 9000, lo = 0, pan = 0|
    var sig = BlipSync.perc(Impulse.ar(0), freq, decay, bend, damp, snap,
                            tilt, top, lo, beater);
    DetectSilence.ar(sig, 0.0001, 0.05, doneAction: 2);
    Out.ar(out, Pan2.ar(sig * amp, pan))
}).add;
)

Synth(\bsPerc);                                            // the kick
Synth(\bsPerc, [\snap, 0.006, \tilt, 0, \amp, 0.02]);      // wide open: a click
Synth(\bsPerc, [\snap, 0.07, \beater, 0.22, \amp, 0.3]);   // soft beater
Synth(\bsPerc, [\damp, 40, \amp, 0.05]);                   // barely damps: it rings
Synth(\bsPerc, [\bend, 1, \amp, 0.12]);                    // no pitch drop
Synth(\bsPerc, [\bend, 9, \decay, 0.9, \amp, 0.12]);       // long 808 drop


// ---------------------------------------------------------------------------
// 4. THE FAMILY.  Same synth, different numbers.
// ---------------------------------------------------------------------------
Synth(\bsPerc, [\freq, 46,  \decay, 0.45, \bend, 4.2, \damp, 600, \snap, 0.045]);          // kick
Synth(\bsPerc, [\freq, 150, \decay, 0.35, \bend, 2.0, \damp,  60, \snap, 0.03,
                \tilt, -4, \amp, 0.07]);                                                   // tom
Synth(\bsPerc, [\freq, 800, \decay, 0.08, \bend, 1.2, \damp, 120, \snap, 0.008,
                \tilt, 0, \amp, 0.01]);                                                    // click
Synth(\bsPerc, [\freq, 190, \decay, 0.5,  \bend, 1.0, \damp,  30, \snap, 0.15,
                \lo, 1400, \tilt, -2, \amp, 0.02]);                                        // rattle


// ---------------------------------------------------------------------------
// 5. INTO THE CHAIN.  Resonant lowpass, then tanh. The band limit stays under
//    the filter, so nothing folds before the distortion gets to it.
// ---------------------------------------------------------------------------
(
SynthDef(\bsPercChain, { |out = 0, amp = 0.6, drive = 3, freq = 44|
    var tr  = Impulse.ar(0);
    var cut = EnvGen.ar(Env([4000, 300], [0.05], -5), tr);
    var sig = BlipSync.perc(tr, freq, 0.5, 4.5, 600, 0.05, -4, beater: 0.15) * 0.1;
    var out_ = (RLPF.ar(sig, cut, 0.5) * drive).tanh * amp;
    DetectSilence.ar(out_, 0.0001, 0.05, doneAction: 2);
    Out.ar(out, out_ ! 2)
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
    \damp,   Pseq([600, 600,  60, 600, 120,  60], inf),
    \snap,   Pwhite(0.02, 0.05),
    \beater, Pwhite(0.0, 0.18),
    \amp,    Pseq([0.12, 0.05, 0.05, 0.12, 0.01, 0.05], inf),
    \pan,    Pseq([0, -0.3, 0.3, 0, 0.5, -0.4], inf)
).play
)


// ---------------------------------------------------------------------------
// 7. DRUMS ON A PHASE NETWORK.  strike takes any trigger, so a PhaseLock tree
//    can clock them: one conductor, voices locked at whole ratios, each struck
//    by its own phase wrap.
// ---------------------------------------------------------------------------
(
{
    var cf = 2.4, ratios = [1, 2, 3, 5];
    var master = Phasor.ar(0, cf * SampleDur.ir, 0, 1);
    var ph = PhaseLock.ar(master, cf * ratios, 3, ratios: ratios,
                          iphases: [0, 0.25, 0.5, 0.75]);
    var freq  = [46, 150, 380, 800];
    var decay = [0.45, 0.3, 0.12, 0.06];
    var snap  = [0.045, 0.03, 0.012, 0.008];
    var amp   = [0.12, 0.05, 0.02, 0.008];
    Splay.ar(Array.fill(4, { |i|
        var trig = Trig1.ar(HPZ1.ar(ph[i]) < -0.25, SampleDur.ir);
        BlipSync.perc(trig, freq[i], decay[i], 4.2, 600, snap[i], -6) * amp[i]
    })) * 0.8
}.play
)
