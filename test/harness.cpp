// Offline comparison harness: SuperCollider's Blip (verbatim) vs BlipSync core
// vs an exact long-double reference. Writes raw float64 files for analyze.py.
#include "../plugins/BlipSync/BlipSyncCore.hpp"
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <string>
#include <vector>

static const int kSineSize = 8192;
static const float kBadValue = 1e20f;
static float gSine[kSineSize + 1];
static float gInvSine[kSineSize + 1];

static void fillTables() {
    const double twopi = 2.0 * M_PI;
    for (int i = 0; i <= kSineSize; ++i) {
        double ph = i * twopi / kSineSize;
        double d = sin(ph);
        gSine[i] = (float)d;
        gInvSine[i] = (float)(1.0 / d);
    }
    gInvSine[0] = gInvSine[kSineSize / 2] = gInvSine[kSineSize] = kBadValue;
    int sz = kSineSize, sz2 = sz >> 1;
    for (int i = 1; i <= 8; ++i) {
        gInvSine[i] = gInvSine[sz - i] = kBadValue;
        gInvSine[sz2 - i] = gInvSine[sz2 + i] = kBadValue;
    }
}

static inline float phaseFrac(uint32_t p) {
    union { uint32_t i; float f; } u;
    u.i = 0x3F800000u | (0x007FFF80u & (p << 7));
    return u.f - 1.f;
}
static inline float lininterp(float x, float a, float b) { return a + x * (b - a); }
#define XLO(p) (((p) >> 16) & 0x1FFF)

// --- SuperCollider Blip, transcribed from server/plugins/OscUGens.cpp --------
struct BlipSC {
    int32_t phase = 0, numharm = 0, N = 1;
    float freqin = 0.f, scale = 0.5f;
    double cpstoinc = 0.0;
    double sr = 48000.0;

    void ctor(float f, int nh, double srate) {
        sr = srate;
        freqin = f;
        numharm = nh;
        cpstoinc = kSineSize * (1.0 / sr) * 65536.0 * 0.5;
        int32_t n = numharm;
        int32_t maxN = (int32_t)((sr * 0.5) / freqin);
        if (n > maxN) n = maxN;
        if (n < 1) n = 1;
        N = n;
        scale = 0.5f / n;
        phase = 0;
    }
    void next(float* out, int n, float freqinNew, int numharmNew) {
        int32_t ph = phase;
        int32_t freq, Nn, prevN = N;
        float sc, prevscale = scale;
        bool crossfade;
        if (numharmNew != numharm || freqinNew != freqin) {
            Nn = numharmNew;
            int32_t maxN = (int32_t)((sr * 0.5) / freqinNew);
            if (Nn > maxN) {
                Nn = maxN;
                float mf = freqin > freqinNew ? freqin : freqinNew;
                freq = (int32_t)(cpstoinc * mf);
            } else {
                if (Nn < 1) Nn = 1;
                freq = (int32_t)(cpstoinc * freqinNew);
            }
            crossfade = Nn != N;
            N = Nn;
            scale = sc = 0.5f / Nn;
        } else {
            Nn = N;
            freq = (int32_t)(cpstoinc * freqinNew);
            sc = scale;
            crossfade = false;
        }
        int32_t N2 = 2 * Nn + 1;
        int32_t prevN2 = 2 * prevN + 1;
        float xfslope = 1.f / n, xf = 0.f;
        for (int i = 0; i < n; ++i) {
            float t0 = gInvSine[XLO(ph)], t1 = gInvSine[XLO(ph) + 1];
            float v;
            if (t0 == kBadValue || t1 == kBadValue) {
                float pf = phaseFrac(ph);
                float denom = lininterp(pf, gSine[XLO(ph)], gSine[XLO(ph) + 1]);
                if (std::fabs(denom) < 0.0005f) {
                    v = 1.f;
                } else {
                    int32_t rp = ph * prevN2;
                    float numer = lininterp(phaseFrac(rp), gSine[XLO(rp)], gSine[XLO(rp) + 1]);
                    float n1 = (numer / denom - 1.f) * prevscale;
                    rp = ph * N2;
                    numer = lininterp(phaseFrac(rp), gSine[XLO(rp)], gSine[XLO(rp) + 1]);
                    float n2 = (numer / denom - 1.f) * sc;
                    v = crossfade ? lininterp(xf, n1, n2) : n2;
                }
            } else {
                float pf = phaseFrac(ph);
                float denom = t0 + (t1 - t0) * pf;
                int32_t rp = ph * prevN2;
                float numer = lininterp(phaseFrac(rp), gSine[XLO(rp)], gSine[XLO(rp) + 1]);
                float n1 = (numer * denom - 1.f) * prevscale;
                rp = ph * N2;
                numer = lininterp(phaseFrac(rp), gSine[XLO(rp)], gSine[XLO(rp) + 1]);
                float n2 = (numer * denom - 1.f) * sc;
                v = crossfade ? lininterp(xf, n1, n2) : n2;
            }
            out[i] = v;
            ph += freq;
            xf += xfslope;
        }
        phase = ph;
        freqin = freqinNew;
        numharm = numharmNew;
    }
    // effective frequency actually produced for a requested freq
    double effFreq(double f) const { return (double)(int32_t)(cpstoinc * f) / cpstoinc; }
};

// --- exact reference: literal weighted cosine sum in long double ------------
static long double refSample(long double p, double lowN, double highN) {
    long double acc = 0.0L, W = 0.0L;
    int kmax = (int)std::floor(highN) + 1;
    for (int k = 1; k <= kmax; ++k) {
        double w = std::min(1.0, std::max(0.0, highN - k + 1.0)) * std::min(1.0, std::max(0.0, k - lowN + 1.0));
        if (w <= 0.0) continue;
        acc += (long double)w * cosl(2.0L * (long double)M_PI * (long double)k * p);
        W += (long double)w;
    }
    if (W < 1.0L) W = 1.0L;
    return acc / W;
}

static void dump(const std::string& name, const std::vector<double>& v) {
    std::string path = "out/" + name + ".f64";
    FILE* f = fopen(path.c_str(), "wb");
    fwrite(v.data(), sizeof(double), v.size(), f);
    fclose(f);
    printf("wrote %s (%zu)\n", path.c_str(), v.size());
}

int main() {
    fillTables();
    const double sr = 48000.0;
    const int BS = 64;
    const double nyq = sr * 0.5 * 0.995;

    // ================== 1. static tone, identical spectra ==================
    {
        const int n = 1 << 18;
        const double freqReq = 440.0, nh = 40;
        BlipSC b;
        b.ctor((float)freqReq, (int)nh, sr);
        const double fEff = b.effFreq(freqReq); // match Blip's quantised freq
        std::vector<double> blip(n), mine(n), ref(n), eb(n), em(n);
        std::vector<float> buf(BS);
        for (int i = 0; i < n; i += BS) {
            b.next(buf.data(), BS, (float)freqReq, (int)nh);
            for (int j = 0; j < BS; ++j) blip[i + j] = buf[j];
        }
        blipsync::Band bd = blipsync::makeBand(fEff, 0.0, nh * fEff, 0.0, nyq, false);
        double p = 0.0, inc = fEff / sr;
        for (int i = 0; i < n; ++i) {
            mine[i] = blipsync::evalBand(bd, p);
            ref[i] = (double)refSample((long double)i * (long double)fEff / (long double)sr, 1.0, nh);
            p += inc; p -= std::floor(p);
        }
        for (int i = 0; i < n; ++i) { eb[i] = blip[i] - ref[i]; em[i] = mine[i] - ref[i]; }
        double mb = 0, mm = 0;
        for (int i = 0; i < n; ++i) { mb = std::max(mb, std::fabs(eb[i])); mm = std::max(mm, std::fabs(em[i])); }
        printf("static 440Hz/40harm: max|err| Blip=%.3e (%.1f dB)  BlipSync=%.3e (%.1f dB)\n",
               mb, 20 * log10(mb), mm, 20 * log10(mm));
        dump("s1_blip", blip); dump("s1_mine", mine); dump("s1_eblip", eb); dump("s1_emine", em);
    }

    // ============ 2. bandwidth sweep at fixed f0: band above must be empty ==
    // f0 = 200 Hz. Blip: numharm swept 5..60 (integer steps).
    // BlipSync: maxfreq swept 1000..12000 Hz continuously.
    // Top harmonic never exceeds 12 kHz, so 14k..24k must contain nothing.
    {
        const int n = 1 << 19;
        const double f0 = 200.0;
        BlipSC b; b.ctor((float)f0, 5, sr);
        const double fEff = b.effFreq(f0);
        std::vector<double> blip(n), mine(n);
        std::vector<float> buf(BS);
        double p = 0.0;
        for (int i = 0; i < n; i += BS) {
            double t = (double)i / (double)n;
            int nh = (int)std::floor(5.0 + 55.0 * t);
            b.next(buf.data(), BS, (float)f0, nh);
            for (int j = 0; j < BS; ++j) blip[i + j] = buf[j];
        }
        for (int i = 0; i < n; ++i) {
            double t = (double)i / (double)n;
            double mx = (5.0 + 55.0 * t) * fEff;
            blipsync::Band bd = blipsync::makeBand(fEff, 0.0, mx, 0.0, nyq, false);
            mine[i] = blipsync::evalBand(bd, p);
            p += fEff / sr; p -= std::floor(p);
        }
        dump("s2_blip", blip); dump("s2_mine", mine);
    }

    // ==== 3. pitch sweep, band top pinned at 8 kHz in BOTH implementations ==
    // This is the headline difference: Blip can only approximate a fixed Hz
    // band by recomputing an integer numharm, which steps. Nothing above
    // ~8 kHz should exist in either output.
    {
        const int n = 1 << 19;
        const double top = 8000.0;
        BlipSC b; b.ctor(100.f, (int)(top / 100.0), sr);
        std::vector<double> blip(n), mine(n);
        std::vector<float> buf(BS);
        double p = 0.0;
        for (int i = 0; i < n; i += BS) {
            double t = (double)i / (double)n;
            float f = (float)(100.0 * pow(8.0, t));
            b.next(buf.data(), BS, f, (int)(top / f));
            for (int j = 0; j < BS; ++j) blip[i + j] = buf[j];
        }
        for (int i = 0; i < n; ++i) {
            double t = (double)i / (double)n;
            double f = 100.0 * pow(8.0, t);
            blipsync::Band bd = blipsync::makeBand(f, 0.0, top, 0.0, nyq, false);
            mine[i] = blipsync::evalBand(bd, p);
            p += f / sr; p -= std::floor(p);
        }
        dump("s3_blip", blip); dump("s3_mine", mine);
    }

    // ============ 4. audio-rate FM: Blip samples freq once per block =======
    {
        const int n = 1 << 19;
        const double fc = 300.0, fm = 600.0, dev = 120.0;
        BlipSC b; b.ctor((float)fc, 20, sr);
        std::vector<double> blip(n), mine(n);
        std::vector<float> buf(BS);
        double p = 0.0;
        for (int i = 0; i < n; i += BS) {
            double f = fc + dev * sin(2.0 * M_PI * fm * (double)i / sr);
            b.next(buf.data(), BS, (float)f, 20);
            for (int j = 0; j < BS; ++j) blip[i + j] = buf[j];
        }
        for (int i = 0; i < n; ++i) {
            double f = fc + dev * sin(2.0 * M_PI * fm * (double)i / sr);
            blipsync::Band bd = blipsync::makeBand(f, 0.0, 20.0 * f, 0.0, nyq, false);
            mine[i] = blipsync::evalBand(bd, p);
            p += f / sr; p -= std::floor(p);
        }
        dump("s4_blip", blip); dump("s4_mine", mine);
    }

    // ==== 5. hard sync: naive reset vs sub-sample reset + polyBLEP =========
    // 800 Hz oscillator (10 harmonics up to 8 kHz) synced to 93.75 Hz, i.e.
    // exactly 512 samples -- so an exact band-limited reference can be built
    // in analyze.py and the residual is pure aliasing.
    {
        const int n = 512 * 1024;
        const double fosc = 800.0, fsync = 48000.0 / 512.0, mx = 8000.0;
        std::vector<double> naive(n), blep(n);
        blipsync::Band bd = blipsync::makeBand(fosc, 0.0, mx, 0.0, nyq, false);
        const double inc = fosc / sr, incs = fsync / sr;

        {   // naive: instantaneous reset on the sample after the crossing
            double p = 0.0, ps = 0.0, psPrev = 1.0;
            for (int i = 0; i < n; ++i) {
                bool wrapped = ps < psPrev;
                psPrev = ps; ps += incs; if (ps >= 1.0) ps -= 1.0;
                if (wrapped) p = 0.0;
                naive[i] = blipsync::evalBand(bd, p);
                p += inc; p -= std::floor(p);
            }
        }
        {   // sub-sample reset + 2-point polyBLEP step correction
            double p = 0.0, ps = 0.0, psPrev = 1.0, c0 = 0.0, c1 = 0.0;
            for (int i = 0; i < n; ++i) {
                bool wrapped = ps < psPrev;
                double d = 0.0;
                if (wrapped) { d = (1.0 - psPrev) / incs; if (d < 0.0) d = 0.0; if (d > 1.0) d = 1.0; }
                psPrev = ps; ps += incs; if (ps >= 1.0) ps -= 1.0;
                double y = blipsync::evalBand(bd, p);
                if (wrapped) {
                    double before = blipsync::evalBand(bd, p + d * inc);
                    double after = blipsync::evalBand(bd, 0.0);
                    double D = after - before;
                    c0 += D * (1.0 - d) * (1.0 - d) * 0.5;
                    c1 += -D * d * d * 0.5;
                }
                blep[i] = y + c0;
                c0 = c1; c1 = 0.0;
                if (wrapped) p = (1.0 - d) * inc;
                else { p += inc; p -= std::floor(p); }
            }
        }
        dump("s5_naive", naive); dump("s5_blep", blep);
    }

    return 0;
}
