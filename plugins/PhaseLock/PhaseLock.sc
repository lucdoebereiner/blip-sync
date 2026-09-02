/*
    PhaseLock - a bank of phase-locked oscillators driven by an external
    master phase. Outputs n phase ramps, 0..1.

    Chain instances to build a hierarchy: each output phase of one bank can be
    the master of another bank below it, with its own coupling strength and
    ratios. Feed the leaf phases to BlipSync with freq: 0 and track: 1.
*/

PhaseLock : MultiOutUGen {

    // master   - master phase, 0..1. A Phasor, another PhaseLock's output, a
    //            BlipSync phase output, anything that ramps.
    // freqs    - array of natural frequencies, one per slave. Sets n.
    //            Audio or control rate.
    // k        - pull toward the master, as a fraction of the phase error
    //            applied per sample. 0 = free running. Stable over 0..1;
    //            negative pushes away from the master. Capture range is
    //            0.5 * k * sampleRate Hz of detuning.
    // mutual   - pull toward the bank's OWN mean field (Kuramoto), same units.
    //            Makes the slaves cohere with each other as well as with the
    //            master. Negative repels them into anti-phase.
    // ratios   - per-slave ratio to the master: slave j locks ratios[j] cycles
    //            per master cycle. Wrap-extended to n. INIT RATE.
    // iphases  - per-slave initial phase in cycles. Wrap-extended to n.
    //            INIT RATE. Starting a bank all at 0 is degenerate; spread it.
    *ar { |master = 0, freqs = #[100], k = 0.02, mutual = 0, ratios = 1, iphases = 0|
        var n;
        freqs = freqs.asArray;
        n = freqs.size;
        if(n < 1) { Error("PhaseLock: freqs must not be empty").throw };
        ratios = ratios.asArray.wrapExtend(n);
        iphases = iphases.asArray.wrapExtend(n);
        ^this.multiNew(*(['audio', n, master, k, mutual] ++ freqs ++ ratios ++ iphases))
    }

    init { |... theInputs|
        inputs = theInputs;
        ^this.initOutputs(theInputs[0].asInteger, rate)
    }

    checkInputs {
        var n = inputs[0];
        if(n.rate != 'scalar') { ^"PhaseLock: the number of slaves must be a constant" };
        n = n.asInteger;
        (4 + n .. 4 + (3 * n) - 1).do { |i|
            if(inputs[i].rate != 'scalar') {
                ^("PhaseLock: input % (%) must be a constant"
                    .format(i, if(i < (4 + (2 * n))) { "ratios" } { "iphases" }))
            }
        };
        ^this.checkValidInputs
    }
}
