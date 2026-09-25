/*
    BlipSync -- morphing between a pulse train and a drum.

    Everything here uses BlipSync.ar directly rather than the perc convenience
    method, because perc fixes a shape and what you usually want is to travel
    between the two. Three arguments do the travelling:

        decay   seconds to fall 60 dB          long = sustained, short = a hit
        damp    seconds for the spectrum to    long = it keeps buzzing,
                fall from tilt to a sine       short = a click
        bend    pitch multiplier at the strike 1 = none

    and sync is the strike: a trigger, or a 0..1 phase ramp under syncMode 1.

    TWO THINGS TO KNOW

    1. Do not sweep decay or damp THROUGH zero. Zero means OFF -- an infinite
       time -- while just above zero means instant. So they jump from "no
       envelope at all" to "silent immediately" as they cross. Morph between a
       long time and a short one instead; the examples below go 8 s -> 0.25 s.

    2. Make the pitch an integer multiple of the strike rate. The strike resets
       the phase, and if the oscillator is already at phase 0 when it arrives
       that reset is a no-op. Measured: freq 46 against a 2 Hz strike (= 23x)
       is indistinguishable from no sync at all, while freq 47 puts a
       discontinuity 14x larger than anything else in the waveform at every
       strike -- an audible click. This is free inside a PhaseLock bank, where
       the ratios are integers by construction (example 5).

    normalize is INIT RATE, so a morph has to commit to 2 (raw). That is the
    right choice anyway -- it is the only mode with an attack -- but it means
    the level follows the band, so the morph carries a gain term. RMS drops
    about 15 dB from the sustained end to the percussive one simply because a
    drum is mostly silence; the gain below compensates the peak, not that.

    Evaluate one block at a time. cmd-period stops.
*/

s.boot;


// ---------------------------------------------------------------------------
// 1. THE MORPH, on one knob. Mouse X from a buzzing pulse train on the left to
//    a kick on the right. Nothing switches; the same UGen travels.
// ---------------------------------------------------------------------------
(
{
    var hit  = MouseX.kr(0, 1);
    var rate = 2;
    var sig = BlipSync.ar(
        freq: 23 * rate,                       // 46 Hz: integer x the strike rate
        maxfreq: 9000,
        tilt: -20,
        normalize: 2,
        sync: Impulse.ar(rate),
        decay: hit.linexp(0, 1, 8, 0.25),
        damp:  hit.linexp(0, 1, 4, 0.02),
        bend:  hit.linlin(0, 1, 1, 4)
    ).at(0);
    sig * hit.linlin(0, 1, 0.02, 0.07) ! 2
}.play
)


// ---------------------------------------------------------------------------
// 2. HEAR THE INTEGER LOCK. Same patch held at the sustained end, mouse Y
//    detunes the pitch off the multiple. Anywhere but exactly on it, the
//    phase reset becomes a click once per strike.
// ---------------------------------------------------------------------------
(
{
    var rate = 2;
    BlipSync.ar(freq: (23 * rate) + MouseY.kr(0, 2), maxfreq: 9000, tilt: -20,
                normalize: 2, sync: Impulse.ar(rate),
                decay: 8, damp: 4, bend: 1).at(0) * 0.02 ! 2
}.play
)


// ---------------------------------------------------------------------------
// 3. PER EVENT. One SynthDef with a hit control, so a pattern can put every
//    note anywhere on the axis. hit 0 is a sustained buzz for the whole event,
//    hit 1 is a drum.
// ---------------------------------------------------------------------------
(
SynthDef(\bsMorph, { |out = 0, amp = 0.05, mult = 23, rate = 2, hit = 1,
                     tilt = -20, top = 9000, beater = 0, sus = 1, pan = 0|
    var sig = BlipSync.ar(
        freq: mult * rate, maxfreq: top, tilt: tilt, normalize: 2, rotate: beater,
        sync: Impulse.ar(rate),
        decay: hit.linexp(0, 1, 8, 0.25),
        damp:  hit.linexp(0, 1, 4, 0.02),
        bend:  hit.linlin(0, 1, 1, 4)
    ).at(0);
    var env = EnvGen.ar(Env([0, 1, 1, 0], [0.005, sus, 0.02]), doneAction: 2);
    Out.ar(out, Pan2.ar(sig * env * amp * hit.linlin(0, 1, 1, 3.5), pan))
}).add;
)

Synth(\bsMorph, [\hit, 0]);      // a buzz
Synth(\bsMorph, [\hit, 0.5]);    // half way: a struck drone
Synth(\bsMorph, [\hit, 1]);      // a kick

(
Pbind(
    \instrument, \bsMorph,
    \dur,  Pseq([1, 0.5, 0.5, 1, 0.5, 0.5], inf),
    \sus,  Pkey(\dur) * 0.9,
    \hit,  Pseq([1, 0.2, 0.6, 1, 0, 0.8], inf),
    \mult, Pseq([23, 23, 30, 23, 46, 30], inf),
    \rate, 2,
    \amp,  0.045,
    \pan,  Pwhite(-0.4, 0.4)
).play
)


// ---------------------------------------------------------------------------
// 4. THE MORPH AS A SIGNAL. An LFO on hit, so the texture breathes between a
//    drone and a rhythm without anyone touching it.
// ---------------------------------------------------------------------------
(
{
    var rate = 3;
    var hit = LFNoise2.kr(0.08).range(0, 1);
    var sig = BlipSync.ar(
        freq: 16 * rate, maxfreq: 8000, tilt: -16, normalize: 2,
        sync: Impulse.ar(rate),
        decay: hit.linexp(0, 1, 6, 0.2),
        damp:  hit.linexp(0, 1, 3, 0.015),
        bend:  hit.linlin(0, 1, 1, 3.5)
    ).at(0);
    sig * hit.linlin(0, 1, 0.025, 0.08) ! 2
}.play
)


// ---------------------------------------------------------------------------
// 5. A BANK, HALF DRUMS AND HALF DRONES. PhaseLock clocks the strikes, and its
//    ratios make the integer lock automatic: each voice's pitch is a whole
//    multiple of its own strike rate, so the sustained voices never click.
//    Mouse X tilts the whole bank from drone to rhythm; mouse Y skews which
//    voices get there first.
// ---------------------------------------------------------------------------
(
{
    var cf = 2, ratios = [1, 2, 3, 5], mults = [23, 24, 30, 38];
    var master = Phasor.ar(0, cf * SampleDur.ir, 0, 1);
    var ph = PhaseLock.ar(master, cf * ratios, 3, ratios: ratios,
                          iphases: [0, 0.25, 0.5, 0.75]);
    var base = MouseX.kr(0, 1), skew = MouseY.kr(-1, 1);
    Splay.ar(Array.fill(4, { |i|
        var hit = (base + (skew * (i - 1.5) / 3)).clip(0, 1);
        var rate = cf * ratios[i];
        BlipSync.ar(
            freq: mults[i] * rate, maxfreq: 8000, tilt: -16, normalize: 2,
            sync: ph[i], syncMode: 1,                  // the wrap is the strike
            decay: hit.linexp(0, 1, 6, 0.2),
            damp:  hit.linexp(0, 1, 3, 0.015),
            bend:  hit.linlin(0, 1, 1, 3.5)
        ).at(0) * hit.linlin(0, 1, 0.02, 0.06) * [1, 0.7, 0.5, 0.35][i]
    })) * 0.8
}.play
)


// ---------------------------------------------------------------------------
// 6. INTO THE CHAIN. The filter follows the morph too, so the drone end is
//    open and the percussive end is a swept thump.
// ---------------------------------------------------------------------------
(
{
    var rate = 2, hit = MouseX.kr(0, 1);
    var sig = BlipSync.ar(
        freq: 22 * rate, maxfreq: 9000, tilt: -14, normalize: 2,
        rotate: 0.12, sync: Impulse.ar(rate),
        decay: hit.linexp(0, 1, 8, 0.3),
        damp:  hit.linexp(0, 1, 4, 0.03),
        bend:  hit.linlin(0, 1, 1, 4.5)
    ).at(0) * hit.linlin(0, 1, 0.02, 0.06);
    var cut = hit.linexp(0, 1, 6000, 900);
    ((RLPF.ar(sig, cut, hit.linlin(0, 1, 0.8, 0.35)) * 3).tanh * 0.5) ! 2
}.play
)
