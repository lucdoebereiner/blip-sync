(
var sr=48000, dir=thisProcess.nowExecutingPath.dirname;
var defs=[
 ["e_hifund", { BlipSync.ar(XLine.ar(2000, 30000, 4), 20000).at(0) }],   // fundamental past Nyquist
 ["e_lofund", { BlipSync.ar(2, 20000).at(0) }],                          // ~10000 harmonics
 ["e_narrow", { BlipSync.ar(200, Line.ar(150, 260, 4)).at(0) }],         // maxfreq crossing the fundamental
 ["e_pm",     { BlipSync.ar(220, 8000, phase: SinOsc.ar(220, 0, 2)).at(0) }],
];
var score=List.new;
defs.do{|d,i| score.add([0.0,["/d_recv", SynthDef(d[0],{|out=0| Out.ar(out, d[1].value)}).asBytes]])};
defs.do{|d,i| score.add([i*5.0,["/s_new",d[0],1000+i,0,0]]); score.add([(i*5.0)+4.5,["/n_free",1000+i]])};
score.add([defs.size*5.0,["/c_set",0,0]]);
Score.recordNRT(score.asArray, dir+/+"e.osc", dir+/+"edge.wav", sampleRate:sr, headerFormat:"WAV",
  sampleFormat:"float", options: ServerOptions.new.numOutputBusChannels_(1).sampleRate_(sr), action:{0.exit});
)
