(
var sr=48000, dir=thisProcess.nowExecutingPath.dirname;
var defs=[
 ["r_rot000", { BlipSync.ar(200, 8000, rotate: 0.0).at(0) }],
 ["r_rot125", { BlipSync.ar(200, 8000, rotate: 0.125).at(0) }],
 ["r_rot250", { BlipSync.ar(200, 8000, rotate: 0.25).at(0) }],
 ["r_rot500", { BlipSync.ar(200, 8000, rotate: 0.5).at(0) }],
 ["r_rotsw",  { BlipSync.ar(200, 8000, rotate: Line.ar(0, 1, 4)).at(0) }],
 ["r_rotar",  { BlipSync.ar(200, 8000, rotate: SinOsc.ar(37) * 0.25).at(0) }],
 ["r_tilt0",  { BlipSync.ar(100, 12000, tilt: 0).at(0) }],
 ["r_tilt1",  { BlipSync.ar(100, 12000, tilt: -1).at(0) }],
 ["r_tilt4",  { BlipSync.ar(100, 12000, tilt: -4).at(0) }],
 ["r_tiltsw", { BlipSync.ar(100, 12000, tilt: Line.ar(0, -12, 4)).at(0) }],
 ["r_both",   { BlipSync.ar(100, 12000, rotate: 0.25, tilt: -3).at(0) }],
 ["r_rmsrot", { BlipSync.ar(100, 12000, normalize: 1, rotate: Line.ar(0, 0.5, 4)).at(0) }],
 ["r_stress", { BlipSync.ar(LFNoise0.ar(700).range(-2000, 2000), LFNoise1.ar(30).range(-1000, 40000), LFNoise1.ar(7).range(-500, 9000), LFNoise2.ar(500), Impulse.ar(300), LFNoise1.ar(3), 0, 0, 0, LFNoise2.ar(200), LFNoise1.ar(11).range(-200, 200)).at(0) }],
];
var score=List.new;
defs.do{|d,i| score.add([0.0,["/d_recv", SynthDef(d[0],{|out=0| Out.ar(out, d[1].value)}).asBytes]])};
defs.do{|d,i| score.add([i*5.0,["/s_new",d[0],1000+i,0,0]]); score.add([(i*5.0)+4.5,["/n_free",1000+i]])};
score.add([defs.size*5.0,["/c_set",0,0]]);
Score.recordNRT(score.asArray, dir+/+"r.osc", dir+/+"rt.wav", sampleRate:sr, headerFormat:"WAV",
  sampleFormat:"float", options: ServerOptions.new.numOutputBusChannels_(1).sampleRate_(sr), action:{0.exit});
)
