/*
    PulsarBlip - polyphonic pulsar generator built on the BlipSync kernel.

    Each trigger starts a grain on one of n internal voices, so grains OVERLAP
    instead of replacing one another, and each keeps the parameters it was born
    with. One output channel.
*/

PulsarBlip : UGen {

    // trig      - emission trigger. Each rising edge starts a grain. With
    //             trigMode 1 this is a 0..1 phase ramp instead (a Phasor, a
    //             PhaseLock output) and a grain starts on every wrap.
    //             The emission rate is the PITCH above about 20 Hz and the
    //             RHYTHM below it.
    // freq      - pulsaret frequency: the blip rate INSIDE a grain, which is a
    //             formant, not the pitch. Snapshot when the grain is born.
    // decay     - grain duration, seconds to fall 60 dB. Snapshot.
    //             Blips per grain is about decay * freq, so decay <= 1/freq
    //             gives a single blip and there is no separate mode for it.
    // damp      - seconds for the grain's spectrum to fall from tilt to a sine.
    //             Short is a click, long stays bright. Snapshot.
    // bend      - pitch multiplier at the onset, falling to freq. Snapshot.
    // tilt      - spectral slope at the onset, dB/kHz. Snapshot.
    // rotate    - impulse asymmetry in cycles, 0.25 being the Hilbert
    //             transform. Snapshot.
    // maxfreq   - band limit in Hz. LIVE, not snapshot: it is the
    //             anti-aliasing limit and has to follow the sample rate, not
    //             the grain. Also sets the impulse width (about 0.6/maxfreq)
    //             and the onset window.
    // minfreq   - bottom of the band. LIVE.
    // numVoices - how many grains may sound at once. Older grains are stolen
    //             quietest-first. INIT RATE.
    // trigMode  - 0: trig is a trigger. 1: trig is a 0..1 phase ramp and a
    //             grain starts on the wrap. INIT RATE.
    //
    // Normalisation is raw, so a grain's fundamental is at 1.0 and its peak is
    // the harmonic count; n overlapping grains reach n times that. Scale with
    // mul, or divide by numVoices.
    *ar { |trig = 0, freq = 200, decay = 0.1, damp = 0.02, bend = 1, tilt = -6,
           rotate = 0, maxfreq = 9000, minfreq = 0, numVoices = 8, trigMode = 0,
           mul = 1, add = 0|
        ^this.multiNew('audio', trig, freq, decay, damp, bend, tilt, rotate,
                       maxfreq, minfreq, numVoices, trigMode).madd(mul, add)
    }

    checkInputs {
        if(inputs[9].rate != 'scalar') { ^"PulsarBlip: numVoices must be a constant" };
        if(inputs[10].rate != 'scalar') { ^"PulsarBlip: trigMode must be a constant" };
        ^this.checkValidInputs
    }
}
