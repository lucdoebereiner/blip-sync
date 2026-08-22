import numpy as np, scipy.io.wavfile as wav, warnings
warnings.filterwarnings("ignore")
sr, x = wav.read("bs_tests.wav"); x = x.astype(np.float64)
names = ["t_static","t_phase","t_fm","t_band","t_rms","t_sync","t_sweepbw","t_sweepf","t_stress","t_negzero"]
def seg(i, a=0.5, b=4.0): return x[int((i*5.0+a)*sr):int((i*5.0+b)*sr)]
def nut(n):
    a=[0.3635819,0.4891775,0.1365995,0.0106411]; k=np.arange(n)
    return a[0]-a[1]*np.cos(2*np.pi*k/n)+a[2]*np.cos(4*np.pi*k/n)-a[3]*np.cos(6*np.pi*k/n)
def spec(s):
    n=1<<int(np.floor(np.log2(len(s)))); s=s[:n]; w=nut(n)
    return np.fft.rfftfreq(n,1/sr), 20*np.log10(np.maximum(np.abs(np.fft.rfft(s*w))/(w.sum()/2),1e-30))

print("sanity: any non-finite in whole file?", not np.all(np.isfinite(x)))
for i,n in enumerate(names):
    s = seg(i)
    print(f"  {n:10s} peak {np.max(np.abs(s)):7.4f}  rms {np.sqrt(np.mean(s**2)):7.4f}  finite {np.all(np.isfinite(s))}")

print("\n1. static 440 Hz, maxfreq 17600 (= harmonic 40 exactly)")
f,db = spec(seg(0))
print(f"   peak sample value {np.max(seg(0)):.6f} (expect 1.0)")
print(f"   worst component above 17.7 kHz: {db[f>17700].max():7.1f} dBFS")
h = [db[np.argmin(np.abs(f-440*k))] for k in (1,20,40,41)]
print(f"   harmonics 1/20/40/41: {h[0]:.1f} / {h[1]:.1f} / {h[2]:.1f} / {h[3]:.1f} dB (41 should vanish)")

print("\n2. phase output")
p = seg(1)
print(f"   range [{p.min():.4f}, {p.max():.4f}]  (expect ~[0,1))")
d = np.diff(p); inc = np.median(d[d>0])
print(f"   median increment {inc:.8f}  expect 440/48000 = {440/48000:.8f}")
print(f"   wraps in 3.5 s: {np.sum(d<-0.5)}  expect ~{int(440*3.5)}")

print("\n3. audio-rate FM, fc=300 fm=600: everything must sit on 300 Hz multiples")
f,db = spec(seg(2))
off = np.abs(f - 300*np.round(f/300)); m=(off>45)&(f>100)&(f<sr/2-100)
print(f"   worst off-grid artefact {db[m].max():7.1f} dBFS")

print("\n4. minfreq band: f0=80, band 2400..3200 Hz -> harmonics 30..40 only")
f,db = spec(seg(3))
lo = db[(f>100)&(f<2200)].max(); inb = db[(f>2400)&(f<3200)].max(); hi = db[(f>3400)&(f<24000)].max()
print(f"   below band {lo:7.1f} | in band {inb:7.1f} | above band {hi:7.1f} dBFS")

print("\n5. RMS normalisation, f0=110 maxfreq=6000")
print(f"   rms {np.sqrt(np.mean(seg(4)**2)):.4f} (expect ~0.707), peak {np.max(np.abs(seg(4))):.3f}")

print("\n6. hard sync 800 Hz -> 93.75 Hz master (syncMode 1)")
s = seg(5)
per = len(s)//512*512
blk = s[:per].reshape(-1,512)
print(f"   period-to-period max deviation {np.max(np.abs(blk-blk.mean(0))):.2e} (expect ~0, locked)")
f,db = spec(s)
print(f"   energy is on 93.75 Hz multiples: worst off-grid {db[(np.abs(f-93.75*np.round(f/93.75))>30)&(f>200)].max():7.1f} dBFS")

print("\n7. sweeps: bands that must stay empty")
f,db = spec(seg(6)); print(f"   maxfreq 1k->12k sweep, 14-24 kHz peak: {db[f>14000].max():7.1f} dBFS")
f,db = spec(seg(7)); print(f"   f0 100->800 sweep, maxfreq 8k, 10-24 kHz peak: {db[f>10000].max():7.1f} dBFS")

print("\n8. stress (random/negative/zero freqs, junk in every input)")
s = seg(8, 0.1, 4.4)
print(f"   finite: {np.all(np.isfinite(s))}  peak {np.max(np.abs(s)):.4f}  (must be finite and bounded)")
s = seg(9, 0.1, 4.4)
print(f"   freq -200 -> +200 through zero: finite {np.all(np.isfinite(s))} peak {np.max(np.abs(s)):.4f}")
