(
var sr=48000, dir=thisProcess.nowExecutingPath.dirname;
var score=[
 [0.0,["/d_recv", SynthDef("al",{|out=0| var o=BlipSync.ar(600,9000,iphase:0.25); Out.ar(out,[o[0],o[1]])}).asBytes]],
 [0.0,["/s_new","al",1000,0,0]], [1.0,["/n_free",1000]], [1.1,["/c_set",0,0]]];
Score.recordNRT(score, dir+/+"a.osc", dir+/+"align.wav", sampleRate:sr, headerFormat:"WAV",
  sampleFormat:"float", options: ServerOptions.new.numOutputBusChannels_(2).sampleRate_(sr),
  action:{0.exit});
)
