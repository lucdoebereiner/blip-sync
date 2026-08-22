# BlipSync

A SuperCollider UGen in the spirit of `Blip` — a band-limited impulse train —
rebuilt so that the band is specified in Hz, the spectrum is a continuous
function of every parameter, all inputs run at audio rate, and phase is both an
input and an output.

```
[ sig, phase ] = BlipSync.ar(freq, maxfreq, minfreq, phase, sync,
                             syncPhase, syncMode, iphase, normalize)
```

Build:

```sh
mkdir -p build && cd build
cmake -DSC_PATH=/path/to/supercollider ..
make && make install
```

---

## 1. Is `Blip` actually well antialiased?

Partly. The maths is exact — `Blip` evaluates the Dirichlet kernel

```
sum_{k=1..N} cos(kx) = ( sin((2N+1)·x/2) / sin(x/2) − 1 ) / 2
```

which is alias-free by construction. The problems are all in *how* it is
evaluated and *how it changes over time*. Measured against an exact long-double
reference (`test/harness.cpp`, transcribed verbatim from
`server/plugins/OscUGens.cpp`), at 48 kHz:

| | Blip | BlipSync |
|---|---|---|
| static tone, 440 Hz × 40 harmonics: peak error | **−51.5 dB** | −218.2 dB |
| ” worst spurious component above the 40th harmonic | **−103.6 dBFS** | −152.4 dBFS |
| bandwidth sweep at fixed pitch, energy in the empty 14–24 kHz band | **−71.7 dB** | −111.3 dB |
| pitch sweep 100→800 Hz with the band top held at 8 kHz, energy in the empty 10–24 kHz band | **−68.2 dB** | −109.3 dB |
| audio-rate FM (fc 300, fm 600, dev 120), worst artefact off the harmonic grid | **−27.4 dBFS** | −146.8 dBFS |

Four distinct causes:

**Table interpolation.** `Blip` reads a 8192-point sine table and a
*cosecant* (1/sin) table with linear interpolation. The interpolation error of
the numerator term — which is looked up at `(2N+1)·phase`, i.e. moving very fast
— lands as broadband junk around −72 dBc, and it is genuinely aliased content,
not just distortion.

**The singularity is guarded, not solved.** `sin(x/2)` vanishes once per
period — exactly at the impulse peak, the loudest part of the waveform. The
cosecant table is poisoned with `kBadValue` near its poles, and `Blip` falls
back to a division; when the denominator is smaller than 5e-4 it simply outputs
`1.0`. So the top of every impulse is a small flat plateau of the wrong shape.

**The harmonic count is an integer and it jumps.** `numharm` is clamped to
`floor(sr/2/freq)`, so during a pitch sweep, or whenever the requested count
exceeds Nyquist, `N` steps. Each step is covered by a crossfade lasting exactly
one control block, during which *every* harmonic's amplitude changes (`scale =
0.5/N`). That is a click, once per step. During the crossfade `Blip` also runs
the phase increment at `max(prevFreq, freq)` rather than the requested
frequency, so the pitch is briefly wrong too.

**Frequency is read once per control block.** `Blip` uses `ZIN0(0)`, so the
frequency input is sampled at block rate and held. Audio-rate FM does not work:
the modulator is effectively sampled at `sr/blockSize` (750 Hz at 48 kHz/64) and
its aliases appear as sidebands at −27 dBFS — plainly audible.

The first two are fixable with better arithmetic. The last two are consequences
of the interface: an integer harmonic count and a control-rate frequency.

---

## 2. What BlipSync does instead

### Band edges in Hz, and fractional

The window over harmonic number is a trapezoid whose edges are set in Hz:

```
w_k  = clip(maxfreq/freq − k + 1, 0, 1) · clip(k − minfreq/freq + 1, 0, 1)
y(p) = (1/W) · sum_k w_k · cos(2π k p)          W = sum_k w_k
```

Because `maxfreq/freq` is fractional, the topmost harmonic fades linearly rather
than switching on and off. Nothing steps, so nothing has to be crossfaded, so
there is no crossfade transient — and the spectral width no longer follows the
pitch around. That single change is responsible for the two sweep rows in the
table above.

`minfreq` falls out of the same machinery for free: a closed form exists for a
sum starting at any harmonic, so you get a *band* of harmonics whose centre and
width you dial in Hz independently of pitch. That is a formant-ish burst
generator, not just an impulse train.

### Removing the singularity instead of guarding it

With `A` the lowest and `B` the highest full-weight harmonic and `s = πp`:

```
sum_{k=A..B} cos(2ks) = [ sin((2B+1)s) − sin((2A−1)s) ] / (2 sin s)
```

Substituting `sincpi(y) = sin(πy)/(πy)` makes the `πp` cancel analytically:

```
= [ (2B+1)·sincpi((2B+1)p) − (2A−1)·sincpi((2A−1)p) ] / ( 2·sincpi(p) )
```

For `p` reduced to `[−½, ½)`, `sincpi(p)` only ever ranges over `[2/π, 1]`. The
expression is well conditioned *everywhere*; there is no pole, no reciprocal
table, no bad-value sentinel and no special case at the impulse peak. `sinpi` is
a degree-15 polynomial on `[−½, ½]` (max error 6e-12), which is both more
accurate than a table lookup and faster than `libm`'s `sin`.

The phase reduction is not cosmetic. Left un-reduced, a phase landing one ulp
below 1.0 makes `sin(πp)` cancel to *exactly* zero and the whole expression
returns `inf` — this happened during development and is caught by the stress
test.

### Everything at audio rate

Every input is read per sample; control-rate inputs are ramped across the block.
The band description is only rebuilt when `freq`, `minfreq` or `maxfreq` actually
moves, which is where the difference between 0.10% and 0.21% of a core comes
from.

### Phase in and out

- `phase` is an offset in **cycles**, added to the running phase rather than
  accumulated into it — so an audio-rate signal there is phase modulation.
- The second output channel is the phase, 0..1, *including* that offset, so it
  always describes what the waveform is actually doing. It is a raw sawtooth,
  meant for coupling and analysis, and is not band-limited.

This is what makes Kuramoto-style work direct: read the phases, compute
`sin(2π(θⱼ − θᵢ))`, add it to the frequency inputs, done. `iphase` spreads the
starting phases of a bank.

### Hard sync

`sync` accepts either a trigger (mode 0) or a 0..1 phase ramp (mode 1). Mode 1
recovers the crossing time exactly from the ramp itself and is the right way to
chain oscillators or follow a `Phasor` master; mode 0 interpolates the crossing
from the two samples straddling the edge.

The reset lands at a sub-sample position and the resulting step discontinuity
gets a 2-point polyBLEP correction. Measured against an analytically
band-limited hard-sync reference (800 Hz oscillator, 10 harmonics, 93.75 Hz
master):

| | aliasing residual |
|---|---|
| naive reset | −19.1 dB |
| sub-sample reset + polyBLEP | **−28.5 dB** |
| ideal 32-tap linear-phase BLEP (the ceiling) | −45 dB |

Hard sync is the one remaining aliasing source, and it is inherent: a step
discontinuity has infinite bandwidth, so no amount of care in the oscillator
removes it. A table-based minBLEP would recover roughly the remaining 13 dB at
the cost of a 4 kB table and a correction ring buffer; polyBLEP was chosen
because it is table-free and adds no latency. A polyBLAMP slope-correction term
was implemented and measured — it changed nothing and was dropped.

The crossing is detected between two samples and the correction can only be
scheduled forward, so **hard sync responds one sample after the edge**. There is
no output latency.

---

## 3. Things worth knowing

- **`maxfreq` and Nyquist.** The fractional top edge sits one harmonic above
  `maxfreq`. If that would cross Nyquist it is dropped, which reintroduces
  stepping at the very top. Keep `maxfreq` at least one harmonic spacing below
  Nyquist — the 20000 default does this for any fundamental under ~4 kHz — and
  the edge stays smooth.
- **Fundamentals above the band limit fade out** rather than folding over,
  because `maxfreq/freq` drops below 1 and the fundamental's own weight goes
  with it. Sweeping the pitch past Nyquist gives silence, not garbage.
- **FM and PM widen the spectrum.** The band limit applies to the *unmodulated*
  spectrum. Deep modulation spreads each harmonic well beyond `maxfreq` and will
  alias — measured at −31 dBFS for PM with index 2 on a 220 Hz carrier at
  `maxfreq` 8000. Lower `maxfreq` when modulating hard. This is physics, not an
  implementation defect.
- **RMS normalisation peaks well above 1.** `normalize: 1` holds the RMS at
  0.707 for any bandwidth, which means the peak is `sqrt(W)` — 7.4 for a 54
  harmonic band. Scale accordingly. Peak normalisation (the default, and Blip's
  behaviour) keeps the waveform at 1.0 but makes narrow bands quiet.
- **No `mul`/`add` on `ar`**, because it returns two channels and `madd` would
  scale the phase output too. Use `.at(0) * amp`, or the `arSig` convenience
  method.
- **No `-ffast-math`.** The accuracy rests on exact IEEE behaviour in the phase
  reduction and the sinc guards; reassociation there reintroduces exactly the
  cancellation this UGen exists to avoid.

## 4. Cost

At 48 kHz, per voice, one core:

| | ns/sample | % of a core |
|---|---|---|
| control-rate `freq`/`minfreq`/`maxfreq` | 19.8 | 0.10% |
| any of them at audio rate | 43.1 | 0.21% |

Independent of how many harmonics are in the band — 2 Hz with 10 000 harmonics
costs the same as 2 kHz with 5.

## 5. Verifying it yourself

```sh
cd test
g++ -O2 -std=c++17 -o harness harness.cpp -lm && ./harness && python3 analyze.py
```

builds `Blip` (verbatim), `BlipSync` and an exact long-double reference in one
binary and prints the comparison table. `sclang nrt.scd && python3 check.py`
renders the installed UGen through `scsynth` in NRT and checks peak levels,
band emptiness, phase-output continuity, sync lock and behaviour under garbage
input.
