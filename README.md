# BlipSync

A SuperCollider UGen in the spirit of `Blip` — a band-limited impulse train —
rebuilt so that the band is specified in Hz, the spectrum is a continuous
function of every parameter, all inputs run at audio rate, and phase is both an
input and an output.

```
[ sig, phase ] = BlipSync.ar(freq, maxfreq, minfreq, phase, sync, syncPhase,
                             syncMode, iphase, normalize, rotate, tilt, track,
                             decay, bend, damp)

         sig  = BlipSync.perc(freq, decay, bend, damp, tilt, sync, syncMode,
                              maxfreq, beater, minfreq, mul, add)
```

Plus two companions:

```
phases = PhaseLock.ar(master, freqs, k, mutual, ratios, subs, iphases)

   sig = PulsarBlip.ar(trig, freq, decay, damp, bend, tilt, rotate,
                       maxfreq, minfreq, numVoices, trigMode)
```

`PhaseLock` is a bank of phase-locked oscillators emitting phase ramps for
BlipSync to read — [section 4](#4-phaselock-a-chainable-pll-bank). `PulsarBlip`
is a polyphonic pulsar generator on the same kernel, where grains overlap
instead of replacing one another — [section 5](#5-pulsarblip-polyphonic-pulsar-synthesis).

Complete worked patches, from one oscillator to a three-level hierarchy, are in
[`examples/phase-networks.scd`](examples/phase-networks.scd).

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

### Asymmetric impulses: `rotate`

The pulse is symmetric because every harmonic is a zero-phase cosine. Rotating
them all by the *same* angle φ — not `k·φ`, which is only a time shift and is
what the `phase` input already does — makes it asymmetric:

```
y(p) = (1/W) Σ w_k cos(2πkp + φ)
```

`rotate` is in cycles: 0 is the symmetric impulse, 0.25 its Hilbert transform (a
sharp edge on one side and a slow tail on the other), 0.5 the inverted impulse.

The point of doing it this way is that **the magnitude spectrum is untouched**,
so it cannot alias and adds no bandwidth — measured at 0.0000 dB change on every
one of 40 harmonics. It is safe to modulate at audio rate, unlike PM: the
modulation index does not scale with `k`, so the sideband spread stays bounded.

Measured waveshape, f0 = 200 Hz, band 8 kHz:

| rotate | peak | trough | skewness |
|---|---|---|---|
| 0 | +1.000 | −0.225 | +6.54 |
| 0.125 | +0.926 | −0.468 | +4.62 |
| 0.25 | +0.727 | −0.727 | 0.00 |
| 0.5 | +0.225 | −1.000 | −6.54 |

Because only the phases move, **RMS is exactly invariant** — rotating does not
change loudness, it changes the crest factor. In peak-normalised mode the
waveform peak drops to 0.73 at `rotate: 0.25`, which is extra headroom, not a
level drop. (RMS mode is therefore *not* needed to keep the level steady across
this knob, contrary to what one might expect.)

Rotation is most audible on impulsive material — low fundamentals where each
period reads as a distinct event — and much less so on steady tones up high.

### Softer pulses: `tilt`

The trapezoid is a brick wall, so the impulse has sinc-like ringing and negative
side lobes: the hard, buzzy character. Weighting harmonics by `r^(k−1)` instead
gives a Poisson kernel — strictly positive, smooth, no ringing — which is what
makes Csound's `gbuzz` sound unlike `buzz`.

`tilt` is in dB per kHz, referenced to the fundamental, so like everything else
here it stays put when the pitch moves. It applies *underneath* the `maxfreq`
brick wall, so the alias-free guarantee is unaffected. Measured slope error:
0.004 dB/kHz.

This matters more than it looks if there is saturation downstream. A brick-wall
BLIT has crest factor `sqrt(2W)`, so `tanh` mostly flattens the spike tip and
leaves the rest near-linear; a tilted pulse is wider and less peaky, so the same
drive distorts it far more evenly — and generates less aliasing of its own.

Both features fall out of one complex geometric series, `z = r·e^{i2πp}`:

```
S = z^A (1 − z^n) / (1 − z) + edge terms
y = Re( e^{iφ} · S ) · norm
```

`1 − z^m` is formed as `(1 − r^m) + 2 r^m sin²(mπp)` — same-signed terms that
never cancel — with `1 − r^m = −expm1(m ln r)`, which stays exact as `r → 1`.
Validated against brute-force additive synthesis at −225 dB, and against the
original real-valued kernel at −217 dB.

**The default path is untouched.** With `rotate: 0, tilt: 0` the original
real-valued kernel runs, and all 13 deterministic regression renders are
bit-for-bit identical to the pre-feature build.

### Phase in and out

- `phase` is an offset in **cycles**, added to the running phase rather than
  accumulated into it — so an audio-rate signal there is phase modulation.
- The second output channel is the phase, 0..1, *including* that offset, so it
  always describes what the waveform is actually doing. It is a raw sawtooth,
  meant for coupling and analysis, and is not band-limited.

This is what makes Kuramoto-style work direct: read the phases, compute
`sin(2π(θⱼ − θᵢ))`, add it to the frequency inputs, done. `iphase` spreads the
starting phases of a bank.

### Driving it from phase alone: `track`

The band width is `maxfreq / freq`, so `freq: 0` means zero harmonics and
silence — the phase input reads the waveform but says nothing about how wide the
band may be. `track: 1` derives the width from the **total phase velocity**,
`freq + d(phase)/dt`, which makes both of these work:

- `freq: 0` plus a `Phasor`, another BlipSync's phase output, or any other ramp
  on `phase`. The band limit follows whatever rate that source runs at, so the
  thing stays alias-free while you scrub, warp or stop the drive. Against the
  equivalent freq-driven oscillator (200 Hz, `maxfreq` 8000) the 40 harmonic
  amplitudes agree to **0.0035 dB** and the worst component above 10 kHz is
  **−142 dBFS**.
- Phase modulation that band-limits itself. A fast modulator on `phase` raises
  the instantaneous frequency, and with tracking the band narrows to match
  instead of folding over. 300 Hz carrier, `maxfreq` 6000, ±942 Hz deviation,
  measured against the same synth rendered at 384 kHz and decimated: **−43 dB**
  of aliasing with tracking, **−21 dB** without. It is dynamic band limiting,
  not a transparent fix — the timbre moves as the band does.

Two things are worth knowing. The velocity estimate is smoothed with a 0.25 ms
one-pole: differencing a float32 phase signal turns its quantisation into
high-frequency noise, and feeding that straight into the band edge dithers the
top harmonic and sprays spurs at −96 dBFS. With the filter a swept phase drive
measures −107 dBFS worst spur against −139 dBFS for the same sweep driven by
`freq`; the price is that the band lags a genuinely abrupt change of drive rate
by a quarter millisecond. And a phase input that stops moving is asking for an
infinitely narrow impulse, so it goes quiet rather than freezing on a value.

Tracking is off by default and init-rate, so nothing about the existing
behaviour changes.

### Phase networks: Kuramoto, PLL

Both live outside this UGen, and that is deliberate — a coupling network is one
object shared by *n* oscillators, not something each oscillator owns. Neither
needs new code.

**Kuramoto** is frequency coupling, `dθᵢ/dt = ωᵢ + (K/N)·Σⱼ sin(θⱼ − θᵢ)`, so it
goes straight into the `freq` input with the phase outputs fed back through
`LocalIn`/`LocalOut`. The pairwise sum is worth rewriting: it equals
`S·cos(2πθᵢ) − C·sin(2πθᵢ)` where `C`, `S` are the means of `cos` and `sin` over
all phases — the order parameter. That is O(*n*) instead of O(*n*²), 229 UGens
instead of 905 for *n* = 16, and numerically identical (measured: R agrees to
four decimals at K = 20 and K = 80). Measured locking transition for four
oscillators at 89–111 Hz:

| K (Hz) | 0 | 4 | 10 | 20 | 40 | 80 |
|---|---|---|---|---|---|---|
| order parameter R | 0.44 | 0.38 | 0.39 | 0.88 | 0.98 | 0.995 |

Stable and alias-free throughout; the one block of `LocalIn` latency only starts
to matter when K approaches the block rate.

**Hierarchy** is the same trick once per level, summed. Compute a mean field per
group and a mean field over the groups; each oscillator feels
`K_in·(group field) + K_out·(global field)`. Four groups of four, groups centred
at 96/104/116/132 Hz:

| K_in | K_out | R within a group | R between groups |
|---|---|---|---|
| 0 | 0 | 0.39 | 0.46 |
| 40 | 0 | **0.998** | **0.46** |
| 40 | 10 | 0.998 | 0.44 |
| 40 | 40 | 1.000 | 0.93 |
| 40 | 120 | 1.000 | 0.99 |

The second row is the interesting one: each group is a single coherent pulse
while the four groups drift freely against each other. Raising `K_out` fuses
them. It nests to any depth — one field per level, one term per level in the sum
— and stays O(*n*).

For a level above that runs *slower*, use a ratio lock:
`sin(2π(ratio·masterPhase − θᵢ))` holds `ratio` pulses per conductor cycle.
Groups at 2:3:4:5 against a 25 Hz conductor, deliberately detuned by +3/−4/+5/−2
Hz so the lock has to work for it:

| K_lock (Hz) | ×2 | ×3 | ×4 | ×5 | group frequency error |
|---|---|---|---|---|---|
| 0 | 0.02 | 0.02 | 0.00 | 0.03 | +3.00 −4.00 +5.00 −2.00 Hz |
| 3 | 1.000 | 0.45 | 0.35 | 1.000 | 0.00 −2.65 +3.96 0.00 Hz |
| 8 | 1.000 | 1.000 | 1.000 | 1.000 | 0.00 0.00 0.00 0.00 Hz |

Textbook capture: each group locks once the coupling exceeds its detuning, and
the K=3 row catches two groups in and two out. Alias floor stays below
−127 dBFS everywhere in both tables.

**PLL** corrects the phase itself rather than the frequency, which needs a phase
*source* — a UGen emitting 0..1 ramps. Feed those to `phase` with `freq: 0` and
`track: 1`. Measured with a four-slave master-lock PLL against a 100 Hz master:

| correction factor | 0.0002 | 0.001 | 0.005 | 0.02 |
|---|---|---|---|---|
| order parameter R | 0.36 | 0.53 | 0.98 | 0.999 |

The difference in feel is the difference between the two: Kuramoto integrates
the pull, so it bends pitch and locks softly; a PLL displaces the phase
directly, so it locks harder and can pull discontinuously.

### Percussion: a tilt envelope is an attack

`tilt` weights harmonic *k* by `r^(k-1)`. Sweeping it downward over time is
therefore `w_k(t) = e^{-kt/τ}` — a modal decay in which the *k*-th harmonic dies
*k* times faster than the fundamental, which is what a struck resonator does.
A drum is a pitch envelope on `freq` plus a tilt envelope, nothing more exotic.

The obstacle is that both original normalisations are level-preserving by
design, and that is precisely what a percussive sound must not be. Under
`normalize: 0` the bright impulse at the start and the pure sine it collapses
into both peak at exactly 1.0, so there is no attack to be had. Measured on a
kick patch (pitch 200→47 Hz over 50 ms, tilt 0→−500 over 35 ms, no amplitude
envelope at all), peak per 4 ms block relative to the settled body:

| normalize | 2 ms | 6 ms | 14 ms | 30 ms | 60 ms | 400 ms | attack over body |
|---|---|---|---|---|---|---|---|
| 0 peak | 1.1 | 1.1 | −0.2 | −2.2 | −0.9 | 0.0 | **+1.1 dB** |
| 1 rms | 15.8 | −0.5 | −1.5 | −2.5 | −0.9 | 0.0 | +15.8 dB |
| 2 raw | 32.1 | −0.7 | −1.7 | −2.5 | −0.9 | 0.0 | **+32.1 dB** |

With `normalize: 2` the whole envelope is already in the spectrum — the third
row of that table needs no amplitude envelope to be a drum.

Both envelopes live in the UGen, so a drum is one call and three numbers:

```supercollider
BlipSync.perc(46) * 0.12          // a kick, complete
```

| | |
|---|---|
| `decay` | how long it rings — seconds to fall 60 dB (measured 0.97 s for `1.0`) |
| `bend` | pitch multiplier at the strike, falling to `freq` |
| `damp` | how fast the spectrum dies — **the** shape control |

There is no strike input, because **`sync` already is one**: it takes a trigger,
or a 0..1 phase ramp under `syncMode: 1`, and resets the phase sub-sample
accurately — which is what striking a drum means. Any sync event restarts the
envelopes, so a `PhaseLock` output strikes a drum on every wrap with no trigger
wiring at all. The UGen is also **born struck**, so a synth-per-note needs no
trigger either: the line above plays as it stands.

Every amount is a parameter that already existed — `tilt` is how bright the
strike is, `maxfreq` how far that brightness reaches, `rotate` how hard the
beater is, and the `damp`/`decay` ratio how bright the ring stays. Only the
times are new.

`damp` is the one worth a knob. Spectral centroid after the strike:

| damp | 2 ms | 10 ms | 30 ms | 80 ms | 200 ms |
|---|---|---|---|---|---|
| 0.004 | 43 | 80 | 94 | 50 | 41 Hz |
| 0.02 | 213 | 95 | 54 | 48 | 56 |
| 0.09 | 170 | 90 | 67 | 68 | 46 |
| 0.6 | 525 | 300 | 185 | 98 | 70 |

A `damp` longer than `decay` means the spectrum never finishes collapsing, so
the ring stays bright. The trip is made **linearly in time**, not exponentially,
because `tilt ∝ ln r` and a modal decay has `ln r` falling at a constant rate —
done exponentially, most of the collapse lands in the first tenth of `damp` and
the parameter stops meaning what its name says.

All three are inert at their defaults and the arithmetic then reduces to an
exact multiply by 1 or subtract of 0, so the nine deterministic tests stay
bit-identical — including the sync test, which proves the envelope restart costs
nothing when the percussion parameters are off. `decay` and `damp` are read once
per block; they set a rate of change, not a value.

### Travelling between the two

`perc` fixes a shape; `ar` lets you move. Sweeping the three times morphs
continuously from a pulse train to a drum with nothing switching. Two things
make it work:

- **Never sweep `decay` or `damp` through zero.** Zero means *off*, an infinite
  time, while just above zero means *instant* — so they jump from "no envelope
  at all" to "silent immediately" as they cross. Morph between a long time and a
  short one: 8 s → 0.25 s.
- **Make the pitch an integer multiple of the strike rate.** The strike resets
  the phase, and if the oscillator is already at phase 0 when it arrives, that
  reset is a no-op. Measured discontinuity at the strike instants, against the
  99th-percentile sample-to-sample jump elsewhere in the same waveform:

| | jump at the strike | ratio to the waveform's own |
|---|---|---|
| `freq: 46` — 23 × a 2 Hz strike | 0.0029 | **0.40** |
| `freq: 47` — not a multiple | 0.1028 | **14.2** |
| no sync at all | 0.0033 | 0.44 |

On the multiple it is indistinguishable from never striking at all; off it,
every strike is an audible click. Inside a `PhaseLock` bank this comes free —
the ratios are integers by construction.

`normalize` is init-rate, so a morph commits to `2`. Level then holds within
6 dB across the whole travel with a modest gain term, and the peak stays under
0.25. `examples/percussion.scd` has the fixed shapes,
[`examples/morph.scd`](examples/morph.scd) the travelling ones.

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
  `maxfreq` 8000. Lower `maxfreq` when modulating hard, or turn on `track: 1`
  and let the band follow the instantaneous frequency. This is physics, not an
  implementation defect.
- **`rotate` costs nothing spectrally.** It is a pure phase rotation, so it
  cannot introduce aliasing or change loudness at any modulation rate.
- **`tilt` does not relax the band limit.** `maxfreq` still applies on top of
  it, so the output stays alias-free at any tilt, positive or negative.
- **RMS normalisation peaks well above 1.** `normalize: 1` holds the RMS at
  0.707 for any bandwidth, which means the peak is `sqrt(W)` — 7.4 for a 54
  harmonic band. Scale accordingly. Peak normalisation (the default, and Blip's
  behaviour) keeps the waveform at 1.0 but makes narrow bands quiet.
- **`normalize: 2` is raw** and has no upper bound: the peak is `W`, the number
  of harmonics in the band. In exchange the fundamental is always at 1.0, so
  your output gain is the level of the body — see
  [**Percussion: a tilt envelope is an attack**](#percussion-a-tilt-envelope-is-an-attack).
- **No `mul`/`add` on `ar`**, because it returns two channels and `madd` would
  scale the phase output too. Use `.at(0) * amp`, or the `arSig` convenience
  method.
- **No `-ffast-math`.** The accuracy rests on exact IEEE behaviour in the phase
  reduction and the sinc guards; reassociation there reintroduces exactly the
  cancellation this UGen exists to avoid.

## 4. `PhaseLock`: a chainable PLL bank

*n* phase oscillators, each with its own natural frequency, each pulled toward an
external master phase at a rational ratio; *n* phase ramps out. Per sample, for
slave *j*:

```
e      = wrap(ratios[j]*master - subs[j]*ph[j])        // in [-0.5, 0.5)
ph[j] += freq[j]/sr
       + (k/subs[j]) * e                               // toward the parent
       + mutual * R*sin(2π(ψ - ph[j]))                 // toward the siblings
```

The master is a **signal**, not a frequency, and that is the whole point: banks
chain. One bank's output phase becomes the master of a bank below it, so a
hierarchy is a tree of these — one instance per node, each with its own coupling
strength and ratios. A PLL that generates its own master internally can only ever
be one level deep.

### `k` and `mutual` are frequencies

Both are in **Hz**, and both mean the largest frequency pull the term can exert.
For `k` that is also the capture range: a slave detuned from its locked frequency
by less than `k/subs` Hz locks, one detuned by more slips. Four slaves at 2:3:4:5
against a 25 Hz conductor, detuned by +3/−4/+5/−2 Hz:

| k (Hz) | ×2 | ×3 | ×4 | ×5 | |
|---|---|---|---|---|---|
| 0 | 0.018 | 0.022 | 0.000 | 0.028 | |
| 1 | 0.129 | 0.061 | 0.054 | 0.187 | |
| 2.5 | 0.347 | 0.242 | 0.190 | **1.000** | captures the 2 Hz one |
| 4.5 | **1.000** | **1.000** | 0.385 | **1.000** | and the 3 and 4, not the 5 |
| 10 | 1.000 | 1.000 | 1.000 | 1.000 | |

The capture range is literally the number you typed. Rendering the same test at
96 kHz reproduces this table to three decimals — that is what the Hz units buy;
a per-sample coefficient would have meant a different loop at every sample rate.
The settling time constant is `1/(2k)` seconds.

`mutual` is the bank's own Kuramoto mean field, so a bank coheres with no master
at all, and the units line up with the `K` of section 2. Eight oscillators spread
over 90–120 Hz:

| mutual (Hz) | 0 | 5 | 12 | 25 | 60 | −30 |
|---|---|---|---|---|---|---|
| R | 0.230 | 0.280 | 0.292 | **0.892** | 0.986 | 0.168 |

The transition sits where the coupling passes the width of the bank; negative
coupling drives R below its uncoupled value as the bank spreads into anti-phase.

### Ratios are rational, and modulatable

`ratios : subs` is a lock ratio — `subs` slave cycles per `ratios` master cycles,
so 3:1 is three pulses per conductor cycle, 1:3 is one per three, 3:2 is a three
against two. Both are rounded to integers internally, and that is the definition
rather than a limitation: `θ ↦ nθ` is a well-defined map of the circle only for
integer *n*, and a fractional one puts a jump of `frac(n)` into the lock target
at every wrap of the master — a periodic kick, not a lock.

Because they round, they can be **modulated**: an LFO on `ratios` steps cleanly
from one lock to the next. Sweeping a ratio input from 0.5 to 6.5 gives a
staircase of locked frequencies at 25, 50, 75, 100, 125, 150 Hz against a 25 Hz
conductor, each holding phase (lock 1.000). Feed `freqs` the same modulator so
each slave already sits near its new target; otherwise the jump itself has to
fall inside the capture range, which needs a much stiffer loop.

### A hierarchy

Measured on a two-level tree — four group nodes at 2:3:4:5 against a 25 Hz
conductor, detuned by +3/−4/+5/−2 Hz, four members locked under each node:

| k_in | k_lock | members → node | nodes → conductor (×2 ×3 ×4 ×5) |
|---|---|---|---|
| 96 Hz | 0 | 1.000 ×4 | 0.02 0.02 0.00 0.03 |
| 96 Hz | 1.2 Hz | 1.000 ×4 | 0.15 0.09 0.06 0.23 |
| 96 Hz | 4.8 Hz | 1.000 ×4 | **1.000 1.000 0.52 1.000** |
| 96 Hz | 24 Hz | 1.000 ×4 | 1.000 1.000 1.000 1.000 |
| 0 | 24 Hz | 0.03 0.01 0.02 0.26 | 1.000 1.000 1.000 1.000 |

Row one is the shape worth having: each group internally rigid, the groups
drifting freely against the conductor. Row three is the capture formula being
right to the boundary — 4.8 Hz locks the groups detuned 3, 4 and 2 Hz and lands
exactly on the edge at 5. The last row shows the levels are independent. Alias
floor through BlipSync stays below −142 dBFS in every row of every table here.

A tree of banks is not the same thing as the mean field of section 2. In a tree
each node has a definite phase that its children follow, so a group holds
together even while its node drifts; in a mean field a group's phase is only the
average of its members and has no independent existence.

## 5. `PulsarBlip`: polyphonic pulsar synthesis

One BlipSync is a *monophonic* pulsar generator: a strike replaces the ringing
grain rather than layering on it, because there is only one phase. `PulsarBlip`
gives each grain its own voice.

```
emission rate   the trigger            pitch above ~20 Hz, rhythm below
pulsaret freq   freq                   a formant, NOT the pitch
pulsaret dur    decay                  blips per grain ≈ decay × freq
pulsaret shape  maxfreq, tilt, damp, rotate
duty cycle      decay × emission rate  above 1 the grains overlap
```

The two rates are independent, which is the point of the model — and it is
exactly the thing that makes a single BlipSync confusing, where `freq` is both
at once. Measured: sweeping the emission rate 4 → 200 Hz with `freq` fixed at
600, the pitch climbs thirty-fold through the rhythm/pitch boundary while the
spectral centroid stays at **3110 Hz** (3106 / 3106 / 3113 / 3325 across the
sweep).

Overlap, emission 20 Hz against 0.6 s grains:

| numVoices | 1 | 2 | 4 | 8 | 16 |
|---|---|---|---|---|---|
| rms vs one voice | +0.0 | +3.9 | +6.3 | +7.1 | +7.2 dB |

It saturates at 8 because by then the oldest grain is 40 dB down.

Two things it does that *n* separate oscillators cannot:

**Every parameter is snapshot at the moment a grain is born**, so noise on
`freq` or `rotate` scatters the cloud instead of bending all of it together.
`maxfreq` and `minfreq` stay live, because they are the anti-aliasing limit and
belong to the sample rate rather than to a grain.

**The onset is band-matched.** A grain would otherwise start with its amplitude
stepping 0 → 1 at the exact instant the blip is at its peak — a discontinuity,
therefore broadband. Instead the phase starts a little early and a raised cosine
of the same length brings the amplitude up, so the impulse arrives precisely as
the window reaches 1. The length is two periods of `maxfreq`, the only timescale
the waveform has. Against an 8× oversampled render:

| | aliasing |
|---|---|
| round-robin of BlipSync voices (stepped onset) | −24.8 dB |
| PulsarBlip (band-matched onset) | **−61.4 dB** |

One trap worth recording: that window must be a *duration*, never a sample
count. Rounding it to samples makes each grain's onset time depend on the sample
rate, shifting every grain by a few microseconds — and for impulses this narrow,
a few microseconds is a large error. It measured **46 dB worse** and looked at
first like the whole idea had failed.

Grains are independent, so nothing here needs the pitch to be an integer
multiple of the emission rate. That constraint belongs to BlipSync's `sync`,
where one phase is being reset rather than a new voice started.
`examples/pulsar.scd` has the patches.

## 6. Cost

At 48 kHz, per voice, one core:

| | ns/sample | % of a core |
|---|---|---|
| default kernel, control-rate band | 19.1 | 0.09% |
| with `rotate` and/or `tilt` engaged | 31.8 | 0.15% |
| `freq`/`minfreq`/`maxfreq`/`tilt` at audio rate | 43.1 | 0.21% |

`track: 1` makes the band depend on the phase input, so it is rebuilt every
sample and costs the same as the audio-rate row.

Independent of how many harmonics are in the band — 2 Hz with 10 000 harmonics
costs the same as 2 kHz with 5.

## 7. Verifying it yourself

```sh
cd test
g++ -O2 -std=c++17 -o harness harness.cpp -lm && ./harness && python3 analyze.py
```

builds `Blip` (verbatim), `BlipSync` and an exact long-double reference in one
binary and prints the comparison table. `sclang nrt.scd && python3 check.py`
renders the installed UGen through `scsynth` in NRT and checks peak levels,
band emptiness, phase-output continuity, sync lock and behaviour under garbage
input.

```sh
sclang trk.scd 48000 && sclang trk.scd 384000 && python3 trkcheck.py
```

covers `track: 1`: phase-driven against freq-driven, a swept drive rate, and PM
aliasing measured against an 8x oversampled render of the same synth.
