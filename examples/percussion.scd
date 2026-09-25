/*
    BlipSync -- percussion.

    A struck resonator is a spectrum whose high harmonics die faster than its
    low ones. tilt weights harmonic k by r^(k-1), so an envelope sweeping tilt
    downward IS that decay: w_k(t) = e^(-k*t/tau), the k-th harmonic dying k
    times faster than the fundamental. A drum is a pitch envelope plus a tilt
    envelope.

    The one thing you must change is normalize. Modes 0 and 1 hold the output
    level up as the band collapses -- that is what they are for -- so the bright
    impulse and the sine it turns into come out at the same peak and there is no
    attack. normalize: 2 is raw: the fundamental sits at 1.0, so your gain is
    the level of the body and the attack rises above it by however many
    harmonics tilt opens at t=0.

    Evaluate one block at a time. cmd-period stops.
*/

s.boot;


// ---------------------------------------------------------------------------
// 0. HEAR THE PROBLEM.  The same patch under all three normalisations, no
//    amplitude envelope at all -- everything you hear is the tilt envelope.
//    Mode 0 barely moves. Mode 2 is already a drum.
// ---------------------------------------------------------------------------
(
var nm = 0;        // <- edit this: 0 peak, 1 rms, 2 raw, then re-evaluate
{
    var tr = Impulse.ar(2);
    var f  = EnvGen.ar(Env([200, 47], [0.05], -6), tr);
    var tl = EnvGen.ar(Env([0, -500], [0.035], -4), tr);
    BlipSync.ar(f, 9000, tilt: tl, normalize: nm, sync: tr, syncMode: 0).at(0)
        * [1, 0.15, 0.022][nm] ! 2
}.play
)

// (normalize, like syncMode, iphase and track, is INIT RATE -- it has to be a
//  literal, not a SynthDef argument.)


// ---------------------------------------------------------------------------
// 1. A KICK.  Gain is the body level; the attack is gain * W(tilt at t=0).
//    tilt's starting value is the beater, where it lands is the ring.
// ---------------------------------------------------------------------------
(
SynthDef(\bsKick, { |out = 0, amp = 0.12, f0 = 190, f1 = 46, pdrop = 0.055,
                    open = -6, close = -600, snap = 0.04, rel = 0.42,
                    beater = 0, top = 9000|
    var tr  = Impulse.ar(0);
    var env = EnvGen.ar(Env([1, 0], [rel], -4), doneAction: 2);
    var f   = EnvGen.ar(Env([f0, f1], [pdrop], -7));
    var tl  = EnvGen.ar(Env([open, close], [snap], -4));
    var sig = BlipSync.ar(f, top, tilt: tl, normalize: 2, rotate: beater,
                          sync: tr, syncMode: 0).at(0);
    Out.ar(out, sig * env * amp ! 2)
}).add;
)

Synth(\bsKick);                                          // the plain one
Synth(\bsKick, [\open, 0, \snap, 0.012, \amp, 0.02]);    // wide open = hard click
Synth(\bsKick, [\beater, 0.22, \amp, 0.3]);              // rotate = soft beater
Synth(\bsKick, [\close, -40, \amp, 0.08]);               // parks bright: it rings
Synth(\bsKick, [\f0, 420, \f1, 38, \pdrop, 0.09, \rel, 0.8]);   // long 808 drop


// ---------------------------------------------------------------------------
// 2. THE FAMILY.  Everything below is the same three envelopes with different
//    numbers. Short + high + bright = a click; no pitch drop + a surviving
//    band = a tom; minfreq lifted off the fundamental = a shell-ish rattle.
// ---------------------------------------------------------------------------
(
SynthDef(\bsHit, { |out = 0, amp = 0.1, f0 = 190, f1 = 46, pdrop = 0.05,
                   open = -6, close = -600, snap = 0.04, rel = 0.4, crv = -4,
                   beater = 0, top = 12000, lo = 0, pan = 0|
    var tr  = Impulse.ar(0);
    var env = EnvGen.ar(Env([1, 0], [rel], crv), doneAction: 2);
    var f   = EnvGen.ar(Env([f0, f1], [pdrop], -7));
    var tl  = EnvGen.ar(Env([open, close], [snap], -4));
    var sig = BlipSync.ar(f, top, lo, tilt: tl, normalize: 2, rotate: beater,
                          sync: tr, syncMode: 0).at(0);
    Out.ar(out, Pan2.ar(sig * env * amp, pan))
}).add;
)

Synth(\bsHit, [\f0, 190, \f1, 46, \rel, 0.45]);                              // kick
Synth(\bsHit, [\f0, 300, \f1, 150, \pdrop, 0.02, \close, -60, \rel, 0.35,
               \amp, 0.05]);                                                  // tom
Synth(\bsHit, [\f0, 900, \f1, 700, \pdrop, 0.01, \open, 0, \close, -120,
               \snap, 0.01, \rel, 0.08, \amp, 0.01]);                         // click
Synth(\bsHit, [\f0, 210, \f1, 190, \pdrop, 0.01, \lo, 1400, \open, -2,
               \close, -30, \snap, 0.15, \rel, 0.5, \amp, 0.02]);             // rattle


// ---------------------------------------------------------------------------
// 3. INTO THE CHAIN.  Resonant lowpass swept with the tilt, then tanh. The
//    band limit stays under the filter, so nothing folds before the distortion
//    gets to it.
// ---------------------------------------------------------------------------
(
SynthDef(\bsKickChain, { |out = 0, amp = 0.35, drive = 3|
    var tr  = Impulse.ar(0);
    var env = EnvGen.ar(Env([1, 0], [0.5], -4), doneAction: 2);
    var f   = EnvGen.ar(Env([200, 44], [0.06], -7));
    var tl  = EnvGen.ar(Env([-4, -600], [0.05], -4));
    var cut = EnvGen.ar(Env([4000, 300], [0.05], -5));
    var sig = BlipSync.ar(f, 9000, tilt: tl, normalize: 2, rotate: 0.15,
                          sync: tr, syncMode: 0).at(0) * env * 0.1;
    Out.ar(out, ((RLPF.ar(sig, cut, 0.5) * drive).tanh * amp) ! 2)
}).add;
)

Synth(\bsKickChain);


// ---------------------------------------------------------------------------
// 4. A PATTERN, so you can hear it in time.
// ---------------------------------------------------------------------------
(
Pbind(
    \instrument, \bsHit,
    \dur,    Pseq([0.5, 0.25, 0.25, 0.5, 0.25, 0.25], inf),
    \f0,     Pseq([190, 190, 300, 190, 900, 300], inf),
    \f1,     Pseq([ 46,  46, 150,  46, 700, 150], inf),
    \pdrop,  Pseq([0.055, 0.055, 0.02, 0.055, 0.01, 0.02], inf),
    \close,  Pseq([-600, -600, -60, -600, -120, -60], inf),
    \rel,    Pseq([0.45, 0.3, 0.35, 0.45, 0.08, 0.35], inf),
    \amp,    Pseq([0.12, 0.05, 0.05, 0.12, 0.012, 0.05], inf),
    \beater, Pwhite(0.0, 0.18),
    \pan,    Pseq([0, -0.3, 0.3, 0, 0.5, -0.4], inf)
).play
)


// ---------------------------------------------------------------------------
// 5. PERCUSSION ON A PHASE NETWORK.  The drums are BlipSyncs, so a PhaseLock
//    tree can clock them: one conductor, voices locked at whole ratios, each
//    with its own tilt envelope retriggered by its own phase wrap.
// ---------------------------------------------------------------------------
(
{
    var cf = 2.4, ratios = [1, 2, 3, 5];
    var master = Phasor.ar(0, cf * SampleDur.ir, 0, 1);
    var ph = PhaseLock.ar(master, cf * ratios, 3, ratios: ratios,
                          iphases: [0, 0.25, 0.5, 0.75]);
    var f0 = [190, 300, 520, 900], f1 = [46, 150, 380, 760];
    Splay.ar(Array.fill(4, { |i|
        var trig = Trig1.ar(HPZ1.ar(ph[i]) < -0.25, SampleDur.ir);  // the wrap
        var env  = EnvGen.ar(Env([1, 0], [[0.45, 0.3, 0.12, 0.06][i]], -4), trig);
        var f    = EnvGen.ar(Env([f0[i], f1[i]], [[0.055, 0.02, 0.012, 0.008][i]], -7), trig);
        var tl   = EnvGen.ar(Env([-6, -600], [0.04], -4), trig);
        BlipSync.ar(f, 9000, tilt: tl, normalize: 2, sync: trig, syncMode: 0).at(0)
            * env * [0.12, 0.05, 0.02, 0.008][i]
    })) * 0.8
}.play
)
