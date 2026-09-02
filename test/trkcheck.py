"""Checks for track: 1 (band width from phase velocity).  Run trk.scd at 48000
and 384000 first; the 384 kHz render is the alias-free reference for the PM
test, decimated 8:1."""
import numpy as np, soundfile as sf
from scipy.signal import resample_poly

x, sr = sf.read('trk48000.wav'); sr = int(sr)
x4, sr4 = sf.read('trk384000.wav'); sr4 = int(sr4)

def cut(sig, s_r, i, t0, dur):
    a = int(i * 5 * s_r + t0 * s_r)
    return sig[a:a + int(dur * s_r)]

def nuttall(n):
    a = [0.3635819, 0.4891775, 0.1365995, 0.0106411]; k = np.arange(n)
    return a[0] - a[1]*np.cos(2*np.pi*k/n) + a[2]*np.cos(4*np.pi*k/n) - a[3]*np.cos(6*np.pi*k/n)

def spec(s):
    n = 1 << int(np.floor(np.log2(len(s)))); s = s[:n]; w = nuttall(n)
    return (np.fft.rfftfreq(n, 1/sr),
            20*np.log10(np.maximum(np.abs(np.fft.rfft(s*w))/(w.sum()/2), 1e-30)))

def peak_above(s, lo):
    f, db = spec(s); return db[f > lo].max()

def band_energy(s, lo, hi):
    n = 1 << int(np.floor(np.log2(len(s)))); s = s[:n]
    X = np.fft.rfft(s*nuttall(n)); f = np.fft.rfftfreq(n, 1/sr); m = (f >= lo) & (f < hi)
    return 20*np.log10(max(np.sqrt(np.sum(abs(X[m])**2)/np.sum(abs(X)**2)), 1e-30))

print("=" * 74)
print("A. PHASE-DRIVEN vs FREQ-DRIVEN   200 Hz, maxfreq 8000")
fa, da = spec(cut(x, sr, 0, 1, 3)); fb, db = spec(cut(x, sr, 1, 1, 3))
def amp(d, k):
    b = int(round(k * 200 * len(d) * 2 / sr)); return d[b-3:b+4].max()
worst = max(abs(amp(db, k) - amp(da, k)) for k in range(1, 41))
print(f"   40 harmonic amplitudes agree to {worst:.4f} dB")
print(f"   worst spur above 10 kHz: freq-driven {peak_above(cut(x,sr,0,1,3),10000):7.1f}"
      f"   phase-driven {peak_above(cut(x,sr,1,1,3),10000):7.1f} dBFS")

print("=" * 74)
print("B. DRIVE-RATE SWEEP 100 -> 800 Hz, band top pinned at 8 kHz")
print("   (10-24 kHz must be empty)")
for i, lbl in ((2, 'freq-driven '), (3, 'phase-driven')):
    s = cut(x, sr, i, 1, 3)
    by = " ".join(f"{r}:{peak_above(cut(x,sr,i,t0,0.5),10000):7.1f}"
                  for t0, r in ((0.2, '105Hz'), (2.0, '283Hz'), (3.6, '640Hz')))
    print(f"   {lbl}: 10-24k energy {band_energy(s,10000,24000):7.1f} dB   "
          f"peak spur {peak_above(s,10000):7.1f} dBFS  | by rate {by}")

print("=" * 74)
print("C. PHASE MODULATION  300 Hz carrier, maxfreq 6000, 50 Hz x 3 cycles")
print("   (+-942 Hz deviation; residual vs the 384 kHz render = aliasing)")
for i, lbl in ((4, 'track 0'), (5, 'track 1')):
    lo = resample_poly(cut(x4, sr4, i, 0.5, 4.0), 1, 8)
    hi = cut(x, sr, i, 0.5, 4.0)
    n = min(len(lo), len(hi)); lo, hi = lo[:n], hi[:n]
    err, sh = min((np.sqrt(np.mean((hi[2000:-2000] - np.roll(lo, s)[2000:-2000])**2)), s)
                  for s in range(-3, 4))
    print(f"   {lbl}: {20*np.log10(err/np.sqrt(np.mean(lo[2000:-2000]**2))):7.1f} dB rel. signal")
