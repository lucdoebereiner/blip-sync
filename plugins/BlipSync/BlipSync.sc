/*
    BlipSync - band-limited harmonic-band oscillator with phase I/O and sync.

    Returns TWO channels: [ waveform, phase ].

    The band edges are given in Hz rather than as a harmonic count, and they
    are fractional, so the spectrum is a continuous function of every input:
    sweeping freq, minfreq or maxfreq produces no stepping and no crossfade.
    All inputs are read per sample, so audio-rate FM and PM work properly.
*/

BlipSync : MultiOutUGen {

    // freq       - fundamental in Hz. Audio or control rate. May be negative.
    // maxfreq    - top of the harmonic band in Hz (the band limit).
    // minfreq    - bottom of the harmonic band in Hz. 0 = start at the
    //              fundamental, i.e. an ordinary impulse train.
    // phase      - phase offset in CYCLES (0..1), added, not accumulated.
    //              Audio rate -> phase modulation.
    // sync       - sync input, see syncMode.
    // syncPhase  - phase (in cycles) to jump to when sync fires.
    // syncMode   - 0: sync is a trigger, fires on a rising crossing of zero.
    //              1: sync is a 0..1 phase ramp (another BlipSync's phase
    //                 output, Phasor, LFSaw.range(0,1)); fires on the wrap and
    //                 recovers the crossing time exactly. INIT RATE.
    // iphase     - initial phase in cycles. INIT RATE.
    // normalize  - 0: peak (waveform peaks at 1.0, like Blip)
    //              1: RMS  (roughly constant loudness as the band changes)
    //              2: raw  (no scaling; the fundamental sits at 1.0 and the
    //                 output level follows the band). Modes 0 and 1 are
    //                 level-preserving BY DESIGN, so a collapsing band stays
    //                 as loud as a sine and there is no attack to be had -- use
    //                 2 for percussion, where a tilt envelope becomes the
    //                 attack. Peak is then W, the number of harmonics in the
    //                 band, so scale outside.
    //              INIT RATE.
    // rotate    - constant phase rotation of every harmonic, in CYCLES.
    //             0    = symmetric impulse (identical to before this existed)
    //             0.25 = its Hilbert transform: sharp edge one side, slow tail
    //                    the other -- an asymmetric impulse
    //             0.5  = inverted impulse
    //             The magnitude spectrum is untouched, so this cannot alias and
    //             is safe at audio rate. Audible on impulsive material; much
    //             less so on steady tones at high fundamentals.
    // tilt      - exponential spectral tilt in dB per kHz, 0 = flat (default),
    //             negative = darker. A straight line in dB against linear
    //             frequency, referenced to the fundamental, applied UNDER the
    //             maxfreq brick wall. Softens the pulse and removes the
    //             brick-wall ringing.
    // track     - 0: the band width is derived from freq alone (default,
    //                unchanged behaviour).
    //             1: it is derived from the TOTAL phase velocity, i.e.
    //                freq + d(phase)/dt. Set freq to 0 and drive phase from a
    //                Phasor / another BlipSync's phase output / anything else,
    //                and the band limit follows that source's rate. Also makes
    //                fast phase modulation band-limit itself instead of
    //                aliasing. INIT RATE.
    //
    // --- percussion. Two times and a pitch multiplier. There is no separate
    //     strike input: sync already is one. Any sync event restarts these,
    //     and the UGen is born struck, so a synth-per-note needs no trigger.
    // decay     - amplitude: seconds to fall 60 dB. 0 = no amplitude envelope.
    // bend      - pitch multiplier at the strike, falling to freq. 1 = none.
    // damp      - seconds for the spectrum to collapse from tilt to a sine,
    //             and for bend to land. Short is a click, long is a thump, and
    //             longer than decay means the ring stays bright. 0 = no strike
    //             shaping at all.
    //             decay and damp are read once per block.
    //
    // The amounts are the parameters that already exist: tilt is how bright the
    // strike is, maxfreq is how far that brightness reaches, and the damp/decay
    // ratio is how bright the ring stays.
    //
    // Percussion wants normalize: 2. The other two modes are level-preserving
    // by design, so the band collapsing under damp stays as loud as the sine it
    // collapses into and there is no attack to be had.
    *ar { |freq = 440, maxfreq = 20000, minfreq = 0, phase = 0, sync = 0,
          syncPhase = 0, syncMode = 0, iphase = 0, normalize = 0,
          rotate = 0, tilt = 0, track = 0,
          decay = 0, bend = 1, damp = 0|
        ^this.multiNew('audio', freq, maxfreq, minfreq, phase, sync,
                       syncPhase, syncMode, iphase, normalize, rotate, tilt, track,
                       decay, bend, damp)
    }

    // Convenience: just the waveform.
    *arSig { |freq = 440, maxfreq = 20000, minfreq = 0, phase = 0, sync = 0,
              syncPhase = 0, syncMode = 0, iphase = 0, normalize = 0,
              rotate = 0, tilt = 0, track = 0,
              decay = 0, bend = 1, damp = 0, mul = 1, add = 0|
        ^BlipSync.ar(freq, maxfreq, minfreq, phase, sync, syncPhase,
                     syncMode, iphase, normalize, rotate, tilt, track,
                     decay, bend, damp).at(0).madd(mul, add)
    }

    // A struck drum in one call: raw normalisation and the envelopes, waveform
    // only. Born struck, so with no sync it plays once -- which is all a
    // synth-per-note needs. Give it a trigger (or a 0..1 phase ramp with
    // syncMode: 1) to retrigger it.
    *perc { |freq = 46, decay = 0.45, bend = 4.2, damp = 0.045, tilt = -6,
             sync = 0, syncMode = 0, maxfreq = 9000, beater = 0, minfreq = 0,
             mul = 1, add = 0|
        ^BlipSync.ar(freq, maxfreq, minfreq, 0, sync, 0, syncMode, 0, 2,
                     beater, tilt, 0, decay, bend, damp).at(0).madd(mul, add)
    }

    init { |... theInputs|
        inputs = theInputs;
        ^this.initOutputs(2, rate)
    }

    checkInputs {
        #[6, 7, 8, 11].do { |i|
            if(inputs[i].rate != 'scalar') {
                ^("BlipSync: input % (%) must be a constant"
                    .format(i, ["syncMode", "iphase", "normalize", "track"]
                        .at(#[6, 7, 8, 11].indexOf(i))))
            }
        };
        ^this.checkValidInputs
    }
}
