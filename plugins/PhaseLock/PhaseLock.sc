/*
    PhaseLock - a bank of phase-locked oscillators driven by an external
    master phase. Outputs n phase ramps, 0..1.

    Chain instances to build a hierarchy: each output phase of one bank can be
    the master of another bank below it, with its own coupling strength and
    ratios. Feed the leaf phases to BlipSync with freq: 0 and track: 1.
*/

PhaseLock : MultiOutUGen {

    // master   - master phase, 0..1. A Phasor, another PhaseLock's output, a
    //            BlipSync phase output. Audio or control rate.
    // freqs    - array of natural frequencies, one per slave. Sets n.
    //            Audio or control rate.
    // k        - pull toward the master, in Hz: the largest frequency pull the
    //            term can exert, which is also the capture range. A slave
    //            detuned from its locked frequency by less than k/subs Hz locks;
    //            more than that and it slips. 0 = free running. Negative pushes
    //            away from the master. The loop settles with a time constant of
    //            1/(2*k) seconds. Audio or control rate.
    // mutual   - pull toward the bank's OWN mean field (Kuramoto), also in Hz.
    //            Makes the slaves cohere with each other as well as with the
    //            master, and a bank with no master at all is a Kuramoto bank.
    //            Negative repels them into anti-phase. 0 skips the work.
    // ratios   - numerator of the lock ratio: subs slave cycles per ratios
    //            master cycles. Rounded to an integer, so it can be modulated
    //            and will step cleanly between lock ratios. Wrap-extended to n.
    // subs     - denominator of the lock ratio, rounded and clamped to >= 1.
    //            ratios: 3 is three pulses per master cycle; subs: 3 is one per
    //            three; ratios: 3, subs: 2 is a 3-against-2. Wrap-extended to n.
    // iphases  - per-slave initial phase in cycles. Wrap-extended to n.
    //            INIT RATE. Starting a bank all at 0 is degenerate; spread it.
    *ar { |master = 0, freqs = #[100], k = 0, mutual = 0, ratios = 1, subs = 1,
          iphases = 0|
        var n;
        freqs = freqs.asArray;
        n = freqs.size;
        if(n < 1) { Error("PhaseLock: freqs must not be empty").throw };
        ratios = ratios.asArray.wrapExtend(n);
        subs = subs.asArray.wrapExtend(n);
        iphases = iphases.asArray.wrapExtend(n);
        ^this.multiNew(*(['audio', n, master, k, mutual] ++ freqs ++ ratios ++ subs ++ iphases))
    }

    init { |... theInputs|
        inputs = theInputs;
        ^this.initOutputs(theInputs[0].asInteger, rate)
    }

    checkInputs {
        var n = inputs[0];
        if(n.rate != 'scalar') { ^"PhaseLock: the number of slaves must be a constant" };
        n = n.asInteger;
        (4 + (3 * n) .. 4 + (4 * n) - 1).do { |i|
            if(inputs[i].rate != 'scalar') {
                ^("PhaseLock: input % (iphases) must be a constant".format(i))
            }
        };
        ^this.checkValidInputs
    }
}
