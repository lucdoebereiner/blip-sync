s.boot
BlipSync

(
{ 
	var n = 5;
	var pulses = BlipSync.ar(Array.exprand(n, 4, 80), Array.exprand(n, 1000, 8000)).collect(_.at(0));
	var filters = RLPF.ar(pulses, Array.exprand(n, 50, 2000), Array.rand(n, 0.04, 0.9));
	var pl = Splay.ar((filters * 20).tanh) * 0.5;
	//pl
JPverb.ar(pl, 4, 0.82, 4) * 0.2  + pl;
 }.play
 )


(
{ 
	var n = 15;
	var pulses = BlipSync.ar(Array.exprand(n, 4, 50), Array.exprand(n, 9000, 18000)).collect(_.at(0));
	var filters = RLPF.ar(pulses, Array.exprand(n, 50, 12000), Array.rand(n, 0.03, 0.9));
	var pl = Splay.ar((filters * 8).tanh) * 0.5;
	//pl
JPverb.ar(pl, 4, 0.82, 4) * 0.2  + pl;
 }.play
 )


(
{ 
	var n = (4*8);
	var pulses = BlipSync.ar(Array.exprand(n, 4, 20), Array.exprand(n, 9000, 18000)).collect(_.at(0));
	var filters = RLPF.ar(pulses, Array.exprand(n, 1500, 12000), Array.rand(n, 0.001, 0.1));
	var pl = Splay.ar((filters * 8).tanh) * 0.8;
	//pl
JPverb.ar(pl, 5, 0.82, 5) * 0.2  + pl;
 }.play
 )


(
{ 
	var n = (4*8);
	var pulses = BlipSync.ar(Array.exprand(n, 4, 20), Array.exprand(n, 50, 1800)).collect(_.at(0));
	var filters = RLPF.ar(pulses, Array.exprand(n, 100, 2000), Array.rand(n, 0.001, 0.1));
	var pl = Splay.ar((filters * 20).tanh) * 0.8;
	//pl
JPverb.ar(pl, 5, 0.82, 5) * 0.2  + pl;
 }.play
 )


(
{ 
	var n = (4*8);
	var pulses = BlipSync.ar(Array.exprand(n, 20, 60), Array.exprand(n, 1500, 28000)).collect(_.at(0));
	var filters = RLPF.ar(pulses, Array.exprand(n, 50, 9000), Array.rand(n, 0.001, 0.1));
	var pl = Splay.ar((filters * 20).tanh) * 0.8;
	//pl
JPverb.ar(pl, 5, 0.82, 5) * 0.2  + pl;
 }.play
 )


(
{ 
	var n = (3*8);
	var pulses = BlipSync.ar(Array.exprand(n, 4, 50), Array.exprand(n, 9000, 18000)).collect(_.at(0));
	var filters = RLPF.ar(pulses, Array.exprand(n, 1500, 12000), Array.rand(n, 0.03, 0.2));
	var pl = Splay.ar((filters * 8).tanh) * 0.8;
	//pl
JPverb.ar(pl, 4, 0.82, 4) * 0.2  + pl;
 }.play
 )



