import numpy as np
SR = 48000.0
def rd(n): return np.fromfile(f"out/{n}.f64", dtype=np.float64)

def nuttall(n):
    a=[0.3635819,0.4891775,0.1365995,0.0106411]; k=np.arange(n)
    return a[0]-a[1]*np.cos(2*np.pi*k/n)+a[2]*np.cos(4*np.pi*k/n)-a[3]*np.cos(6*np.pi*k/n)

def spec(x):
    n = 1<<int(np.floor(np.log2(len(x)))); x=x[:n]; w=nuttall(n)
    X = np.abs(np.fft.rfft(x*w))/(w.sum()/2)
    return np.fft.rfftfreq(n,1/SR), 20*np.log10(np.maximum(X,1e-30))

def band_energy(x, lo, hi):
    """RMS in a frequency band, in dB relative to full scale."""
    n = 1<<int(np.floor(np.log2(len(x)))); x=x[:n]
    X = np.fft.rfft(x*nuttall(n)); f = np.fft.rfftfreq(n,1/SR)
    m = (f>=lo)&(f<hi)
    tot = np.sqrt(np.sum(np.abs(X[m])**2)/np.sum(np.abs(X)**2))
    return 20*np.log10(max(tot,1e-30))

print("=" * 74)
print("1. STATIC TONE  440 Hz, harmonics 1..40  (error vs exact reference)")
for nm in ("eblip","emine"):
    e = rd("s1_"+nm); f,db = spec(e)
    print(f"   {nm:6s}: peak err spectrum {db.max():8.1f} dB   rms err {20*np.log10(np.sqrt(np.mean(e**2))):8.1f} dB")
f,db = spec(rd("s1_blip"))
above = f>17700   # nothing above harmonic 40 (=17600 Hz) should exist
print(f"   Blip     : worst component above the 40th harmonic: {db[above].max():7.1f} dBFS")
f,db = spec(rd("s1_mine"))
print(f"   BlipSync : worst component above the 40th harmonic: {db[above].max():7.1f} dBFS")

print("=" * 74)
print("2. BANDWIDTH SWEEP, f0=200 Hz, top harmonic 1 kHz -> 12 kHz")
print("   (14-24 kHz must be empty; anything there is an artefact)")
for nm,lbl in (("s2_blip","Blip numharm 5..60"),("s2_mine","BlipSync maxfreq 1k..12k")):
    x = rd(nm)
    print(f"   {lbl:26s}: 14-24 kHz energy {band_energy(x,14000,24000):7.1f} dB   "
          f"peak spur {spec(x)[1][spec(x)[0]>14000].max():7.1f} dBFS")

print("=" * 74)
print("3. PITCH SWEEP 100 -> 800 Hz, band top pinned at 8 kHz in both")
print("   (10-24 kHz must be empty)")
for nm,lbl in (("s3_blip","Blip numharm=8000/f"),("s3_mine","BlipSync maxfreq=8000")):
    x = rd(nm); f,db = spec(x)
    print(f"   {lbl:24s}: 10-24 kHz energy {band_energy(x,10000,24000):7.1f} dB   "
          f"peak spur {db[f>10000].max():7.1f} dBFS")

print("=" * 74)
print("4. AUDIO-RATE FM  fc=300 Hz, fm=600 Hz, dev=120 Hz, 20 harmonics")
print("   Blip samples freq once per 64-sample block -> sidebands at n*750 Hz")
for nm,lbl in (("s4_blip","Blip"),("s4_mine","BlipSync")):
    x=rd(nm); f,db=spec(x)
    # legitimate content lies on the fc +- k*fm grid; probe a bin that is not on it
    # the true signal is periodic at 300 Hz, so every legitimate partial sits
    # on a multiple of 300 Hz. Everything else is block-rate modulator aliasing.
    off = np.abs(f - 300.0*np.round(f/300.0))
    m = (off > 45) & (f > 100) & (f < SR/2 - 100)
    print(f"   {lbl:10s}: worst off-grid artefact {db[m].max():7.1f} dBFS")

print("=" * 74)
print("5. HARD SYNC  800 Hz osc (10 harmonics), master 93.75 Hz = 512 samples")
print("   Reference = analytically band-limited hard sync; residual = aliasing")
# ideal continuous waveform, sampled 64x oversampled over one 512-sample period
OS = 64; P = 512 * OS
t = np.arange(P) / (SR * OS)                 # seconds within one sync period
ph = (800.0 * t) % 1.0
ideal = sum(np.cos(2*np.pi*k*ph) for k in range(1, 11)) / 10.0
C = np.fft.rfft(ideal) / P
K = 512 // 2                                  # keep only harmonics below Nyquist
per = np.zeros(512)
nn = np.arange(512)
for k in range(1, K):
    per += 2*(C[k].real*np.cos(2*np.pi*k*nn/512) - C[k].imag*np.sin(2*np.pi*k*nn/512))
per += C[0].real + C[K].real*np.cos(np.pi*nn)
for nm,lbl in (("s5_naive","naive reset"),("s5_blep","sub-sample + polyBLEP")):
    x = rd(nm)
    best=None
    for sh in range(0,4):      # sync response is inherently 1 sample delayed
        ref = np.tile(per, len(x)//512)
        ref = np.roll(ref, sh)[2048:len(x)-2048]
        err = x[2048:len(x)-2048] - ref
        r = 20*np.log10(np.sqrt(np.mean(err**2))/np.sqrt(np.mean(ref**2)))
        if best is None or r<best[0]: best=(r,sh)
    print(f"   {lbl:24s}: aliasing residual {best[0]:7.1f} dB rel. signal  (delay {best[1]} smp)")
