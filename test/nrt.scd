(
var sr = 48000, dir = thisProcess.nowExecutingPath.dirname;
var defs = [
    ["t_static",   { BlipSync.ar(440, 17600).at(0) }],
    ["t_phase",    { var o = BlipSync.ar(440, 17600); [o[0], o[1]].sum * 0 + o[1] }],
    ["t_fm",       { BlipSync.ar(300 + SinOsc.ar(600, 0, 120), 6000).at(0) }],
    ["t_band",     { BlipSync.ar(80, 3200, 2400).at(0) }],
    ["t_rms",      { BlipSync.ar(110, 6000, normalize: 1).at(0) }],
    ["t_sync",     { BlipSync.ar(800, 8000, sync: Phasor.ar(0, (48000/512) * SampleDur.ir, 0, 1), syncMode: 1).at(0) }],
    ["t_sweepbw",  { BlipSync.ar(200, Line.ar(1000, 12000, 4)).at(0) }],
    ["t_sweepf",   { BlipSync.ar(XLine.ar(100, 800, 4), 8000).at(0) }],
    ["t_stress",   { BlipSync.ar(LFNoise0.ar(2000).range(-3000, 3000), LFNoise1.ar(30).range(-1000, 40000), LFNoise1.ar(7).range(-500, 9000), LFNoise2.ar(500), Impulse.ar(LFNoise0.ar(4).range(1, 900)), LFNoise1.ar(3)).at(0) }],
    ["t_negzero",  { BlipSync.ar(Line.ar(-200, 200, 4), 8000).at(0) }],
];
var score = List.new;
defs.do { |d, i|
    score.add([0.0, ["/d_recv", SynthDef(d[0], { |out=0| Out.ar(out, d[1].value) }).asBytes]]);
};
defs.do { |d, i|
    score.add([i * 5.0, ["/s_new", d[0], 1000 + i, 0, 0]]);
    score.add([(i * 5.0) + 4.5, ["/n_free", 1000 + i]]);
};
score.add([defs.size * 5.0, ["/c_set", 0, 0]]);
Score.recordNRT(score.asArray, dir +/+ "score2.osc", dir +/+ "bs_tests.wav",
    sampleRate: sr, headerFormat: "WAV", sampleFormat: "float",
    options: ServerOptions.new.numOutputBusChannels_(1).sampleRate_(sr),
    action: { "DONE".postln; 0.exit });
)
