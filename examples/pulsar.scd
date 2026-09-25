/*
    PulsarBlip -- polyphonic pulsar synthesis.

    Each trigger starts a grain on one of numVoices internal voices, so grains
    overlap instead of replacing one another, and each keeps the parameters it
    was born with.

        PulsarBlip.ar(trig, freq, decay, damp, bend, tilt, rotate,
                      maxfreq, minfreq, numVoices, trigMode, mul, add)

    The vocabulary, if you have met pulsar synthesis before:

        emission rate   the trigger            pitch above ~20 Hz, rhythm below
        pulsaret freq   freq                   a FORMANT, not the pitch
        pulsaret dur    decay                  blips per grain ~= decay * freq
        pulsaret shape  maxfreq, tilt, damp, rotate
        duty cycle      decay * emission rate  above 1 the grains overlap

    The two rates are independent -- that is the whole point of the model, and
    it is the thing that is confusing about a single BlipSync, where freq is
    both at once.

    Levels: normalisation is raw, so a grain's fundamental is at 1.0 and its
    peak is the harmonic count. Overlapping grains add. Scale accordingly.

    Evaluate one block at a time. cmd-period stops.
*/

s.boot;


// ---------------------------------------------------------------------------
// 1. THE DEMONSTRATION.  The emission rate sweeps 4 -> 200 Hz: the pitch climbs
//    all the way through the boundary between rhythm and tone. The formant
//    does not move, because freq never changed.
// ---------------------------------------------------------------------------
(
{ PulsarBlip.ar(Impulse.ar(XLine.ar(4, 200, 12)), 600, 0.035, 0.02,
                numVoices: 16) * 0.03 ! 2 }.play
)

// the same thing under the mouse, both rates on separate axes
(
{
    PulsarBlip.ar(Impulse.ar(MouseX.kr(2, 300, 1)), MouseY.kr(120, 3000, 1),
                  0.04, 0.02, numVoices: 16) * 0.03 ! 2
}.play
)


// ---------------------------------------------------------------------------
// 2. DUTY CYCLE.  Emission fixed at 20 Hz, grain length 8 ms -> 1.2 s. Grains
//    start separate, then start overlapping, and the texture fills in. Watch
//    numVoices: once the grain is longer than numVoices/rate the oldest is
//    stolen, which is its own (useful) sound.
// ---------------------------------------------------------------------------
(
{ PulsarBlip.ar(Impulse.ar(20), 300, MouseX.kr(0.008, 1.2, 1), 0.03,
                numVoices: 24) * 0.008 ! 2 }.play
)


// ---------------------------------------------------------------------------
// 3. ONE BLIP PER GRAIN.  decay <= 1/freq leaves exactly one impulse, which is
//    the classic single-pulsaret case. No special mode: it is continuous with
//    everything above.
// ---------------------------------------------------------------------------
(
{
    var freq = 400;
    PulsarBlip.ar(Impulse.ar(11), freq, MouseX.kr(0.0008, 0.06, 1), 0.01,
                  tilt: -3, numVoices: 12) * 0.02 ! 2
}.play
)


// ---------------------------------------------------------------------------
// 4. A CLOUD.  Everything except maxfreq is snapshot per grain, so noise on
//    freq and rotate scatters the cloud rather than bending it as a whole.
// ---------------------------------------------------------------------------
(
{
    PulsarBlip.ar(Dust.ar(40), LFNoise0.ar(200).exprange(120, 1600), 0.25, 0.04,
                  rotate: LFNoise0.ar(200).range(0, 0.3), numVoices: 24) * 0.02 ! 2
}.play
)

// wider, slower, with the band moving underneath
(
{
    var sig = PulsarBlip.ar(
        Dust.ar(MouseX.kr(2, 200, 1)),
        LFNoise0.ar(400).exprange(60, 2400),
        LFNoise0.ar(400).exprange(0.05, 0.9),
        LFNoise0.ar(400).exprange(0.004, 0.3),
        tilt: -4,
        rotate: LFNoise0.ar(400).range(0, 0.4),
        maxfreq: SinOsc.kr(0.05).exprange(1200, 9000),
        numVoices: 32);
    Splay.ar([sig, DelayN.ar(sig, 0.05, 0.017)]) * 0.02
}.play
)


// ---------------------------------------------------------------------------
// 5. DRUMS, with tails that survive the next hit. This is the percussion patch
//    from percussion.scd, except that the ring of one hit is no longer cut off
//    by the next -- which is what numVoices buys you.
// ---------------------------------------------------------------------------
(
{ PulsarBlip.ar(Impulse.ar(2.4), 46, 0.5, 0.045, 4.2, -6, numVoices: 6) * 0.012 ! 2 }.play
)


// ---------------------------------------------------------------------------
// 6. CLOCKED BY A PHASE NETWORK.  trigMode 1 takes a 0..1 ramp directly, so a
//    PhaseLock bank emits grains on its wraps -- one conductor, four streams in
//    whole ratios, each with its own formant and grain length.
// ---------------------------------------------------------------------------
(
{
    var cf = 3, ratios = [1, 2, 3, 5];
    var master = Phasor.ar(0, cf * SampleDur.ir, 0, 1);
    var ph = PhaseLock.ar(master, cf * ratios, 4, ratios: ratios,
                          iphases: [0, 0.25, 0.5, 0.75]);
    var freq  = [180, 320, 700, 1400];
    var decay = [0.5, 0.3, 0.15, 0.07];
    Splay.ar(Array.fill(4, { |i|
        PulsarBlip.ar(ph[i], freq[i], decay[i], 0.03, 1, -5,
                      numVoices: 8, trigMode: 1) * [1, 0.7, 0.5, 0.35][i]
    })) * 0.02
}.play
)


// ---------------------------------------------------------------------------
// 7. INTO THE CHAIN.
// ---------------------------------------------------------------------------
(
{
    var sig = PulsarBlip.ar(Dust.ar(60), LFNoise0.ar(300).exprange(90, 900),
                            0.3, LFNoise0.ar(300).exprange(0.006, 0.2),
                            tilt: -5, rotate: LFNoise0.ar(300).range(0, 0.35),
                            maxfreq: 4000, numVoices: 32) * 0.02;
    var cut = SinOsc.kr(0.04).exprange(250, 3500);
    var dry = (RLPF.ar(sig, cut, 0.2) * 4).tanh * 0.4;
    Splay.ar([dry, DelayN.ar(dry, 0.1, 0.031)])
}.play
)
