/*
    BlipSync + PhaseLock -- phase networks, from one oscillator to a hierarchy.

    Evaluate the examples one at a time (cmd-enter / ctrl-enter inside the
    parentheses). cmd-period stops.

    The idea throughout: PhaseLock produces PHASE RAMPS, BlipSync reads them
    with freq: 0 and track: 1. Nothing about the coupling lives inside the
    oscillator, so the same network can drive anything else that takes a phase.

    Two units to keep in mind:
      k, mutual  are in Hz. They are the largest frequency pull the term can
                 exert, and for k that is also the capture range: a slave
                 detuned from its locked frequency by less than k Hz locks,
                 more than that and it slips. Sample rate independent.
      ratios/subs are a lock ratio, subs slave cycles per ratios master cycles.
                 Both are rounded to integers, so they can be modulated and
                 will step between ratios rather than slide through
                 undefined ones.
*/

s.boot;
s.options.memSize = 65536;   // banks are RTAlloc'd; large trees want the room


// ---------------------------------------------------------------------------
// 1. THE PLUMBING.  No coupling at all: a bank of free-running phase ramps,
//    one BlipSync each. This is what everything below is made of.
// ---------------------------------------------------------------------------
(
{
    var freqs = [97, 103, 111, 89];
    var ph = PhaseLock.ar(freqs: freqs, iphases: (0..3) / 4);
    Splay.ar(ph.collect { |p|
        BlipSync.ar(0, 6000, phase: p, track: 1, tilt: -2).at(0)
    }) * 0.2
}.play
)


// ---------------------------------------------------------------------------
// 2. ONE LEVEL.  Four voices at 2:3:4:5 against a 25 Hz conductor, each
//    deliberately detuned. Mouse Y is the capture range in Hz, so you hear
//    them lock one at a time as it passes each voice's detuning:
//    2 Hz first, then 3, then 4, then 5.
// ---------------------------------------------------------------------------
(
{
    var cf = 25, ratios = [2, 3, 4, 5], detune = [3, -4, 5, -2];
    var freqs = Array.fill(4, { |i| (cf * ratios[i]) + detune[i] });
    var master = Phasor.ar(0, cf * SampleDur.ir, 0, 1);
    var ph = PhaseLock.ar(master, freqs, MouseY.kr(0, 8), ratios: ratios,
                          iphases: (0..3) / 4);
    Splay.ar(ph.collect { |p|
        BlipSync.ar(0, 5000, phase: p, track: 1, tilt: -3).at(0)
    }) * 0.2
}.play
)


// ---------------------------------------------------------------------------
// 3. RATIONAL RATIOS.  subs is the denominator: 3:2 is three against two,
//    1:3 is one pulse per three conductor cycles. Locked polyrhythm from one
//    clock, no scheduling.
// ---------------------------------------------------------------------------
(
{
    var cf = 24;
    var num = [3, 5, 1, 5, 2], den = [2, 4, 3, 3, 1];
    var freqs = Array.fill(5, { |i| cf * num[i] / den[i] });
    var master = Phasor.ar(0, cf * SampleDur.ir, 0, 1);
    var ph = PhaseLock.ar(master, freqs, 10, ratios: num, subs: den,
                          iphases: (0..4) / 5);
    Splay.ar(ph.collect { |p|
        BlipSync.ar(0, 4000, phase: p, track: 1, tilt: -4).at(0)
    }) * 0.18
}.play
)


// ---------------------------------------------------------------------------
// 4. NO MASTER AT ALL.  mutual is the Kuramoto mean field of the bank itself.
//    Mouse Y from 0 to 8 Hz takes eight independent oscillators through the
//    locking transition into one pulse. Push it negative and they scatter into
//    anti-phase instead -- try replacing the range with (-4, 8).
// ---------------------------------------------------------------------------
(
{
    var n = 8;
    var freqs = Array.fill(n, { |i| 90 + (i * 4.3) });
    var ph = PhaseLock.ar(0, freqs, k: 0, mutual: MouseY.kr(0, 8),
                          iphases: (0..n-1) / n);
    Splay.ar(ph.collect { |p|
        BlipSync.ar(0, 6000, phase: p, track: 1, tilt: -2).at(0)
    }) * 0.15
}.play
)


// ---------------------------------------------------------------------------
// 5. TWO LEVELS.  A tree: one conductor, four group nodes ratio-locked to it,
//    four voices locked under each node. Sixteen oscillators, three PhaseLocks.
//
//    Mouse X  = pull inside a group      Mouse Y = pull of the groups to the top
//      bottom left  : 16 independent oscillators
//      bottom right : four rigid groups drifting against each other
//      top    right : one nested pulse, everything in ratio
//
//    The middle of the Y axis is the interesting place -- some groups captured,
//    others still sliding.
// ---------------------------------------------------------------------------
(
{
    var g = 4, m = 4, cf = 25;
    var groupF = [53, 71, 105, 123], ratios = [2, 3, 4, 5];
    var kin = MouseX.kr(0, 30), klock = MouseY.kr(0, 8);
    var master = Phasor.ar(0, cf * SampleDur.ir, 0, 1);
    var gph = PhaseLock.ar(master, groupF, klock, ratios: ratios,
                           iphases: (0..g-1) / g);
    var leaves = Array.fill(g, { |q|
        PhaseLock.ar(gph[q], Array.fill(m, { |j| groupF[q] + (j * 1.3) - 1.95 }),
                     kin, iphases: ((0..m-1) / m) + 0.05)
    }).flatten;
    Splay.ar(leaves.collect { |p|
        BlipSync.ar(0, 6000, phase: p, track: 1, tilt: -2).at(0)
    }) * 0.2
}.play
)


// ---------------------------------------------------------------------------
// 6. THREE LEVELS.  A slow bar, three sections in ratio to it, three voices in
//    ratio to each section. Every level has its own coupling strength, so you
//    can have a rigid bar, loosely attached sections and voices that shimmer
//    around their section -- or any other combination.
// ---------------------------------------------------------------------------
(
{
    var bar = 2, secR = [3, 4, 5], voiceR = [4, 5, 6];
    var kBar = MouseY.kr(0, 6), kVoice = MouseX.kr(0, 20);
    var master = Phasor.ar(0, bar * SampleDur.ir, 0, 1);
    // level 2: three sections at 6, 8, 10 Hz, slightly off
    var sec = PhaseLock.ar(master, [6.4, 7.7, 10.3], kBar, ratios: secR,
                           iphases: [0, 0.33, 0.66]);
    // level 1: three voices under each section, at 4:5:6 of it
    var voices = Array.fill(3, { |q|
        PhaseLock.ar(sec[q], Array.fill(3, { |j| (bar * secR[q] * voiceR[j]) + (j * 2.3) - 2.3 }),
                     kVoice, ratios: voiceR, iphases: [0.1, 0.4, 0.7])
    }).flatten;
    Splay.ar(voices.collect { |p|
        BlipSync.ar(0, 3500, phase: p, track: 1, tilt: -5, rotate: 0.15).at(0)
    }) * 0.25
}.play
)


// ---------------------------------------------------------------------------
// 7. MODULATED RATIOS.  ratios is a signal and is rounded internally, so an
//    LFO on it steps cleanly from lock to lock -- the rhythm re-articulates
//    without any of the voices losing phase with the conductor.
//
//    Give freqs the same modulator so each voice already sits near its target
//    and k only has to fix the remainder; otherwise the jump has to fall
//    inside the capture range, which needs a much stiffer loop.
// ---------------------------------------------------------------------------
(
{
    var n = 5, cf = 20;
    var r = Array.fill(n, { |i| LFNoise0.kr(ExpRand(0.3, 1.2)).range(0.5, 6.5) });
    var master = Phasor.ar(0, cf * SampleDur.ir, 0, 1);
    var ph = PhaseLock.ar(master, r.collect { |x| cf * x.round(1) }, 6,
                          ratios: r, iphases: (0..n-1) / n);
    Splay.ar(ph.collect { |p|
        BlipSync.ar(0, 4000, phase: p, track: 1, tilt: -4).at(0)
    }) * 0.15
}.play
)


// ---------------------------------------------------------------------------
// 8. THE WHOLE THING.  Two-level tree into the chain these are actually for:
//    a resonant lowpass per voice, tanh, spread, reverb. The band limit stays
//    below the filter so nothing folds before the distortion gets to it.
// ---------------------------------------------------------------------------
(
{
    var g = 3, m = 4, cf = 12;
    var groupF = [24.3, 36.6, 60.4], ratios = [2, 3, 5];
    var klock = 4, kin = LFNoise1.kr(0.07).range(1, 25);
    var master = Phasor.ar(0, cf * SampleDur.ir, 0, 1);
    var gph = PhaseLock.ar(master, groupF, klock, ratios: ratios,
                           iphases: (0..g-1) / g);
    var voices = Array.fill(g, { |q|
        var f = Array.fill(m, { |j| groupF[q] + (j * 0.7) - 1.05 });
        var lph = PhaseLock.ar(gph[q], f, kin, mutual: 3,
                               iphases: ((0..m-1) / m) + (q / 7));
        lph.collect { |p| [q, p] }
    }).flatten(1);
    var sig = voices.collect { |qp|
        var q = qp[0], p = qp[1];
        var raw = BlipSync.ar(0, 2500, phase: p, track: 1,
                              tilt: LFNoise1.kr(0.11).range(-9, -2),
                              rotate: SinOsc.kr(0.03 + (q * 0.017)).range(0, 0.4)).at(0);
        var cut = SinOsc.kr(0.05 + (q * 0.023)).exprange(200, 3000);
        (RLPF.ar(raw, cut, 0.15) * 20).tanh
    };
    var dry = Splay.ar(sig) * 0.09;
    dry + JPverb.ar(dry, t60: 3.5, damp: 0.4, size: 1.8, earlyDiff: 0.7) * 0.35
}.play
)
