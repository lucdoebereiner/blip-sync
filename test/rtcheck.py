import numpy as np, scipy.io.wavfile as wav, warnings; warnings.filterwarnings("ignore")
sr,x = wav.read("rt.wav"); x = x.astype(np.float64)
names=["r_rot000","r_rot125","r_rot250","r_rot500","r_rotsw","r_rotar",
       "r_tilt0","r_tilt1","r_tilt4","r_tiltsw","r_both","r_rmsrot","r_stress"]
I={n:i for i,n in enumerate(names)}
def seg(n,a=0.5,b=4.0): i=I[n]; return x[int((i*5+a)*sr):int((i*5+b)*sr)]
def nut(n):
    a=[0.3635819,0.4891775,0.1365995,0.0106411]; k=np.arange(n)
    return a[0]-a[1]*np.cos(2*np.pi*k/n)+a[2]*np.cos(4*np.pi*k/n)-a[3]*np.cos(6*np.pi*k/n)
def spec(s):
    n=1<<int(np.floor(np.log2(len(s)))); s=s[:n]; w=nut(n)
    return np.fft.rfftfreq(n,1/sr), np.abs(np.fft.rfft(s*w))/(w.sum()/2)
def db(v): return 20*np.log10(np.maximum(v,1e-30))

print("all finite:", np.all(np.isfinite(x)))
print()
print("A. ROTATION MUST PRESERVE THE MAGNITUDE SPECTRUM (f0=200, band 8k)")
f,X0 = spec(seg("r_rot000"))
def peaks(X, f, f0, nh):   # max in a +-4 bin window, robust to bin alignment
    out=[]
    for k in range(1,nh+1):
        c=np.argmin(np.abs(f-f0*k)); out.append(X[max(0,c-4):c+5].max())
    return np.array(out)
P0 = peaks(X0,f,200,40)
for n in ("r_rot125","r_rot250","r_rot500"):
    f,X = spec(seg(n))
    d = db(peaks(X,f,200,40)) - db(P0)
    print(f"   {n}: harmonic-by-harmonic level change  max {np.max(np.abs(d)):.4f} dB "
          f"(mean {np.mean(d):+.4f})")
print()
print("B. ROTATION CHANGES THE WAVESHAPE (asymmetry: |max| vs |min|)")
for n in ("r_rot000","r_rot125","r_rot250","r_rot500"):
    s = seg(n)
    sk = np.mean(s**3)/np.mean(s**2)**1.5
    print(f"   {n}: peak {s.max():+.4f}  trough {s.min():+.4f}  skewness {sk:+7.2f}")
print()
print("C. ROTATION MUST NOT ALIAS (nothing above the 8 kHz band limit)")
for n in ("r_rot250","r_rotsw","r_rotar"):
    f,X = spec(seg(n))
    print(f"   {n}: worst component above 9 kHz  {db(X[f>9000]).max():7.1f} dBFS")
print()
print("D. TILT SLOPE (f0=100, band 12k). Expected: tilt dB/kHz, ref at 100 Hz")
for n,t in (("r_tilt0",0),("r_tilt1",-1),("r_tilt4",-4)):
    f,X = spec(seg(n))
    ks = np.arange(1,111)
    idx = np.array([np.argmin(np.abs(f-100*k)) for k in ks])
    lv = db(np.array([X[max(0,i-4):i+5].max() for i in idx])); lv -= lv[0]
    fit = np.polyfit(100*ks/1000.0, lv, 1)[0]
    print(f"   {n}: measured slope {fit:+7.3f} dB/kHz   (asked for {t:+.1f})")
print()
print("E. TILT MUST NOT ALIAS, AND MUST KEEP THE BRICK WALL")
for n in ("r_tilt4","r_tiltsw","r_both"):
    f,X = spec(seg(n))
    print(f"   {n}: worst component above 13 kHz  {db(X[f>13000]).max():7.1f} dBFS")
print()
print("F. LEVEL BEHAVIOUR")
for n in ("r_tilt0","r_tilt1","r_tilt4"):
    s=seg(n); print(f"   {n}: peak {np.max(np.abs(s)):.4f} (peak-normalised, expect 1.0)  rms {np.sqrt(np.mean(s**2)):.4f}")
s=seg("r_rmsrot"); q=len(s)//4
print("   r_rmsrot (RMS mode, rotate swept 0 -> 0.5): rms per quarter "
      + " ".join(f"{np.sqrt(np.mean(s[i*q:(i+1)*q]**2)):.4f}" for i in range(4)))
s=seg("r_rot000"); s2=seg("r_rot250")
print(f"   peak mode: rms rot=0 {np.sqrt(np.mean(s**2)):.4f} vs rot=0.25 {np.sqrt(np.mean(s2**2)):.4f} "
      f"({20*np.log10(np.sqrt(np.mean(s2**2))/np.sqrt(np.mean(s**2))):+.2f} dB)")
print()
s=seg("r_stress",0.1,4.4)
print(f"G. STRESS (junk in every input incl. rotate/tilt): finite {np.all(np.isfinite(s))}  peak {np.max(np.abs(s)):.4f}")
