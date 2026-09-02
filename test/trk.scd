(
// Phase-rate tracking (track: 1) tests.  Render at both rates:
//     sclang trk.scd 48000
//     sclang trk.scd 384000
// then:  python3 trkcheck.py
var sr = (thisProcess.argv[0] ? "48000").asInteger;
var dir = thisProcess.nowExecutingPath.dirname, tag = sr.asString;
var defs = [
    ["A_ref",   { BlipSync.ar(200, 8000).at(0) }],
    ["A_ph",    { BlipSync.ar(0, 8000, phase: Phasor.ar(0, 200 * SampleDur.ir, 0, 1), track: 1).at(0) }],
    ["B_fsweep",{ BlipSync.ar(XLine.ar(100, 800, 4), 8000).at(0) }],
    ["B_psweep",{ BlipSync.ar(0, 8000, phase: Phasor.ar(0, XLine.ar(100, 800, 4) * SampleDur.ir, 0, 1), track: 1).at(0) }],
    ["C_pm0",   { BlipSync.ar(300, 6000, phase: (Phasor.ar(0, 50 * SampleDur.ir, 0, 1) * 2pi).sin * 3, track: 0).at(0) }],
    ["C_pm1",   { BlipSync.ar(300, 6000, phase: (Phasor.ar(0, 50 * SampleDur.ir, 0, 1) * 2pi).sin * 3, track: 1).at(0) }],
];
var score = List.new;
defs.do { |d, i| score.add([0.0, ["/d_recv", SynthDef(d[0], { |out=0| Out.ar(out, d[1].value) }).asBytes]]) };
defs.do { |d, i|
    score.add([i * 5.0, ["/s_new", d[0], 1000 + i, 0, 0]]);
    score.add([(i * 5.0) + 4.5, ["/n_free", 1000 + i]]);
};
score.add([defs.size * 5.0, ["/c_set", 0, 0]]);
Score.recordNRT(score.asArray, dir +/+ "trk" ++ tag ++ ".osc", dir +/+ "trk" ++ tag ++ ".wav",
    sampleRate: sr, headerFormat: "WAV", sampleFormat: "float",
    options: ServerOptions.new.numOutputBusChannels_(1).sampleRate_(sr),
    action: { 0.exit });
)
