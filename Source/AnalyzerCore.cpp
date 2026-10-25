// SPDX-License-Identifier: MIT
#include "AnalyzerCore.h"
#include "Presets.h"

#if __has_include("FactoryPresets.h")
 #include "FactoryPresets.h"
 #define KRAKEN_HAS_FACTORY 1
#else
 #define KRAKEN_HAS_FACTORY 0
#endif

#if defined (__SSE2__)
 #include <emmintrin.h>
#endif
#include <atomic>
#include <numeric>
#include <random>
#include <thread>

// =============================================================================================
// What changed compared with the previous analyzer (and why the old results sounded hard/metallic)
//
//  * Candidates are rendered at 44.1 kHz, the rate the plugin really plays at. The old analysis ran at
//    22.05 kHz and could not see anything above 10 kHz, so a patch could be much brighter / more aliased
//    in playback than the search believed.
//  * Spectrum range is 25 Hz .. 16 kHz (was 50 Hz .. 10 kHz): the fundamental of a low bass (E1 = 41 Hz)
//    used to lie outside the compared range.
//  * Loss is a salience-weighted, floor-clamped log-spectral distance. Bands/frames that are silent in the
//    target no longer dominate, and "too bright" errors count more than "too dark" ones.
//  * New loss terms: brightness (spectral centroid) trajectory, bass weight, harmonicity (how periodic the
//    sound is - this is what separates a smooth saw bass from a ring-modulated / FM metallic one) and a
//    small prior against exotic parameters (ring, fold, FM, inharmonicity, comb ...) unless they pay off.
//  * Pitch: parabolic YIN refinement down to 30 Hz, and the leftover cents are stored in Patch::tune so the
//    patch is in tune when played at its root note (previously up to +-50 cents were thrown away).
//  * Search: seeds = archetypes + all factory patches, several islands instead of one population,
//    then a tune line search and a coordinate polish.
// =============================================================================================

namespace kraken
{
namespace
{
constexpr double kSR = 44100.0;
constexpr int kMinLen = 8192;
constexpr int kBands = 64;
constexpr double kFLo = 25.0, kFHi = 16000.0;
constexpr int kRes = 3;
constexpr int kFftSize[kRes] = { 1024, 4096, 8192 };
constexpr int kFftHop[kRes] = { 256, 1024, 2048 };
constexpr double kMinCentre[kRes] = { 430.0, 110.0, 0.0 };   // lowest band centre each resolution can resolve
constexpr int kEnvHop = 441;                                  // 10 ms

//==============================================================================
// resampler (windowed sinc, low-passed when downsampling)
std::vector<float> resample (const std::vector<float>& in, double srcSr, double dstSr)
{
    if (in.empty() || srcSr <= 0.0 || dstSr <= 0.0) return {};
    if (std::abs (srcSr - dstSr) < 1.0e-6) return in;

    constexpr int radius = 16;
    constexpr double pi = 3.14159265358979323846;
    const double ratio = srcSr / dstSr;
    const int outN = (int) std::floor ((double) in.size() / ratio);
    std::vector<float> out ((size_t) std::max (outN, 0), 0.0f);
    const double cutoff = std::min (1.0, dstSr / srcSr) * 0.95;
    const int inN = (int) in.size();
    const double span = (double) radius / cutoff;   // kernel half-width in input samples

    for (int j = 0; j < outN; ++j)
    {
        const double pos = (double) j * ratio;
        const int k0 = std::max (0, (int) std::ceil (pos - span)), k1 = std::min (inN - 1, (int) std::floor (pos + span));
        double sum = 0.0, wsum = 0.0;
        for (int k = k0; k <= k1; ++k)
        {
            const double x = (pos - (double) k) * cutoff;
            const double sinc = std::abs (x) < 1.0e-9 ? 1.0 : std::sin (pi * x) / (pi * x);
            const double window = 0.5 + 0.5 * std::cos (pi * x / (double) radius);
            const double w = cutoff * sinc * window;
            sum += (double) in[(size_t) k] * w;
            wsum += w;
        }
        out[(size_t) j] = std::abs (wsum) > 1.0e-12 ? (float) (sum / wsum) : 0.0f;
    }
    return out;
}

//==============================================================================
// pitch: YIN with parabolic interpolation, 30 Hz .. 2 kHz
std::vector<float> detectPitchCandidates (const std::vector<float>& x, double sr)
{
    const int W = 2048;
    const int minTau = std::max (2, (int) (sr / 2000.0));
    const int maxTau = (int) (sr / 30.0);
    const int n = (int) x.size();
    if (n < W + maxTau + 4) return {};

    std::vector<float> est;
    std::vector<float> d ((size_t) maxTau + 2), cm ((size_t) maxTau + 2);

    for (float frac : { 0.04f, 0.10f, 0.20f, 0.32f, 0.45f })
    {
        const int start = std::min ((int) (frac * (float) n), n - W - maxTau - 2);
        for (int tau = 1; tau <= maxTau + 1; ++tau)
        {
            double s = 0;
            for (int j = 0; j < W; ++j)
            {
                const float df = x[(size_t) (start + j)] - x[(size_t) (start + j + tau)];
                s += (double) df * df;
            }
            d[(size_t) tau] = (float) s;
        }
        cm[0] = 1.0f;
        double run = 0;
        for (int tau = 1; tau <= maxTau + 1; ++tau)
        {
            run += d[(size_t) tau];
            cm[(size_t) tau] = run > 0 ? (float) (d[(size_t) tau] * tau / run) : 1.0f;
        }
        int best = -1;
        for (int tau = minTau; tau <= maxTau; ++tau)
            if (cm[(size_t) tau] < 0.13f)
            {
                while (tau + 1 <= maxTau && cm[(size_t) tau + 1] < cm[(size_t) tau]) ++tau;
                best = tau;
                break;
            }
        if (best < 0)
        {
            float mn = 1.0e9f;
            for (int tau = minTau; tau <= maxTau; ++tau)
                if (cm[(size_t) tau] < mn) { mn = cm[(size_t) tau]; best = tau; }
            if (mn > 0.4f) best = -1;
        }
        if (best > 1)
        {
            // parabolic interpolation of the minimum
            const float a = cm[(size_t) best - 1], b = cm[(size_t) best], c = cm[(size_t) best + 1];
            const float den = a - 2.0f * b + c;
            const float off = std::abs (den) > 1.0e-9f ? 0.5f * (a - c) / den : 0.0f;
            est.push_back ((float) (sr / ((double) best + std::max (-1.0f, std::min (1.0f, off)))));
        }
    }
    return est;
}

//==============================================================================
// FFT (radix-2, in place) and log-band spectrograms
struct Plan
{
    int n = 0;
    std::vector<float> cs, sn, win;
    std::vector<int> rev;

    explicit Plan (int N) : n (N), cs ((size_t) N / 2), sn ((size_t) N / 2), win ((size_t) N), rev ((size_t) N)
    {
        int lg = 0;
        while ((1 << lg) < n) ++lg;
        for (int i = 0; i < n / 2; ++i)
        {
            cs[(size_t) i] = (float) std::cos (2.0 * 3.14159265358979323846 * i / n);
            sn[(size_t) i] = (float) std::sin (2.0 * 3.14159265358979323846 * i / n);
        }
        for (int i = 0; i < n; ++i)
        {
            int r = 0;
            for (int b = 0; b < lg; ++b) if (i & (1 << b)) r |= 1 << (lg - 1 - b);
            rev[(size_t) i] = r;
            win[(size_t) i] = (float) (0.5 - 0.5 * std::cos (2.0 * 3.14159265358979323846 * i / n));
        }
    }

    void fft (float* re, float* im) const
    {
        for (int i = 0; i < n; ++i)
        {
            const int j = rev[(size_t) i];
            if (j > i) { std::swap (re[i], re[j]); std::swap (im[i], im[j]); }
        }
        for (int size = 2; size <= n; size <<= 1)
        {
            const int half = size >> 1, step = n / size;
            for (int i = 0; i < n; i += size)
                for (int k = 0; k < half; ++k)
                {
                    const float wr = cs[(size_t) (k * step)], wi = -sn[(size_t) (k * step)];
                    const int a = i + k, b = a + half;
                    const float tr = re[b] * wr - im[b] * wi;
                    const float ti = re[b] * wi + im[b] * wr;
                    re[b] = re[a] - tr; im[b] = im[a] - ti;
                    re[a] += tr;        im[a] += ti;
                }
        }
    }
};

const Plan& planFor (int r)
{
    static const Plan p0 (kFftSize[0]), p1 (kFftSize[1]), p2 (kFftSize[2]);
    return r == 0 ? p0 : (r == 1 ? p1 : p2);
}

struct BandMap
{
    std::array<int, kBands> lo {}, hi {};
    std::array<bool, kBands> interp {}, valid {};
    std::array<float, kBands> pos {};
};

const std::array<float, kBands>& bandCentres()
{
    static const auto c = []
    {
        std::array<float, kBands> a {};
        for (int j = 0; j < kBands; ++j)
        {
            const double e0 = kFLo * std::pow (kFHi / kFLo, (double) j / kBands);
            const double e1 = kFLo * std::pow (kFHi / kFLo, (j + 1.0) / kBands);
            a[(size_t) j] = (float) std::sqrt (e0 * e1);
        }
        return a;
    }();
    return c;
}

const BandMap& bandMap (int r)
{
    static const auto maps = []
    {
        std::array<BandMap, kRes> m;
        for (int res = 0; res < kRes; ++res)
        {
            const double binHz = kSR / kFftSize[res];
            const int half = kFftSize[res] / 2;
            for (int j = 0; j < kBands; ++j)
            {
                const double e0 = kFLo * std::pow (kFHi / kFLo, (double) j / kBands);
                const double e1 = kFLo * std::pow (kFHi / kFLo, (j + 1.0) / kBands);
                const double fc = std::sqrt (e0 * e1);
                auto& b = m[(size_t) res];
                b.valid[(size_t) j] = fc >= kMinCentre[res];
                if (e1 - e0 >= binHz)
                {
                    int lo = std::max (1, (int) std::ceil (e0 / binHz));
                    int hi = std::min (half, std::max (lo + 1, (int) std::ceil (e1 / binHz)));
                    lo = std::min (lo, hi - 1);
                    b.lo[(size_t) j] = lo; b.hi[(size_t) j] = hi; b.interp[(size_t) j] = false;
                }
                else
                {
                    b.interp[(size_t) j] = true;
                    b.pos[(size_t) j] = (float) (fc / binHz);
                }
            }
        }
        return m;
    }();
    return maps[(size_t) r];
}

struct Spec
{
    int frames = 0;
    std::vector<float> db;     // frames * kBands, dB
    std::vector<float> loud;   // per frame, dB of the total power
    float peak = -200.0f, peakLoud = -200.0f;
};

void bandsFromPower (const float* pw, const BandMap& bm, int res, float* outDb, float& loudDb)
{
    const float sc = 4.0f / (float) kFftSize[res];
    const float sc2 = sc * sc;
    double total = 0.0;
    for (int b = 0; b < kBands; ++b)
    {
        float e;
        if (bm.interp[(size_t) b])
        {
            const int i0 = (int) bm.pos[(size_t) b];
            const float fr = bm.pos[(size_t) b] - (float) i0;
            e = pw[i0] * (1.0f - fr) + pw[i0 + 1] * fr;
        }
        else
        {
            float s = 0.0f;
            for (int k = bm.lo[(size_t) b]; k < bm.hi[(size_t) b]; ++k) s += pw[k];
            e = s / (float) (bm.hi[(size_t) b] - bm.lo[(size_t) b]);
        }
        e *= sc2;
        outDb[b] = 10.0f * std::log10 (e + 1.0e-12f);
        if (bm.valid[(size_t) b]) total += (double) e;
    }
    loudDb = 10.0f * std::log10 ((float) total + 1.0e-12f);
}

void computeSpec (const std::vector<float>& x, int res, Spec& out)
{
    thread_local std::vector<float> re, im, pa, pb;
    const Plan& plan = planFor (res);
    const int N = kFftSize[res], hop = kFftHop[res];
    const BandMap& bm = bandMap (res);
    const int n = (int) x.size();
    out.frames = std::max (1, n < N ? 1 : (n - N) / hop + 1);
    out.db.assign ((size_t) out.frames * kBands, 0.0f);
    out.loud.assign ((size_t) out.frames, 0.0f);
    re.assign ((size_t) N, 0.0f); im.assign ((size_t) N, 0.0f);
    pa.assign ((size_t) N / 2 + 2, 0.0f); pb.assign ((size_t) N / 2 + 2, 0.0f);

    auto fill = [&] (int frame, float* dst)
    {
        const int s = frame * hop;
        for (int i = 0; i < N; ++i)
        {
            const int idx = s + i;
            dst[i] = idx < n ? x[(size_t) idx] * plan.win[(size_t) i] : 0.0f;
        }
    };

    for (int f = 0; f < out.frames; f += 2)
    {
        const bool two = f + 1 < out.frames;
        fill (f, re.data());
        if (two) fill (f + 1, im.data()); else std::fill (im.begin(), im.end(), 0.0f);
        plan.fft (re.data(), im.data());
        for (int k = 0; k <= N / 2; ++k)
        {
            const int m = (N - k) & (N - 1);
            const float ar = re[(size_t) k] + re[(size_t) m], ai = im[(size_t) k] - im[(size_t) m];
            const float br = im[(size_t) k] + im[(size_t) m], bi = re[(size_t) k] - re[(size_t) m];
            pa[(size_t) k] = 0.25f * (ar * ar + ai * ai);
            pb[(size_t) k] = 0.25f * (br * br + bi * bi);
        }
        bandsFromPower (pa.data(), bm, res, &out.db[(size_t) f * kBands], out.loud[(size_t) f]);
        if (two) bandsFromPower (pb.data(), bm, res, &out.db[(size_t) (f + 1) * kBands], out.loud[(size_t) f + 1]);
    }
    out.peak = *std::max_element (out.db.begin(), out.db.end());
    out.peakLoud = *std::max_element (out.loud.begin(), out.loud.end());
}

void envelopeDb (const std::vector<float>& x, std::vector<float>& out)
{
    const int n = (int) x.size();
    const int frames = std::max (1, n / kEnvHop);
    out.resize ((size_t) frames);
    for (int f = 0; f < frames; ++f)
    {
        double e = 0; int cnt = 0;
        for (int i = f * kEnvHop; i < std::min (n, (f + 1) * kEnvHop); ++i) { e += (double) x[(size_t) i] * x[(size_t) i]; ++cnt; }
        const float rms = (float) std::sqrt (e / std::max (cnt, 1));
        out[(size_t) f] = std::max (-100.0f, 20.0f * std::log10 (rms + 1.0e-7f));
    }
}

//==============================================================================
struct Feat
{
    std::array<Spec, kRes> spec;
    std::vector<float> env;
    float envPeak = -100.0f;
};

void computeFeat (const std::vector<float>& x, Feat& f)
{
    for (int r = 0; r < kRes; ++r) computeSpec (x, r, f.spec[(size_t) r]);
    envelopeDb (x, f.env);
    f.envPeak = *std::max_element (f.env.begin(), f.env.end());
}

// fractional-lag normalised autocorrelation (how periodic is the sound at the given lag)
float periodicity (const std::vector<float>& x, int start, int W, float lag)
{
    const int li = (int) lag;
    const float fr = lag - (float) li;
    const int n = (int) x.size();
    if (start < 0 || start + W + li + 2 >= n) return 0.0f;
    double sxy = 0, sxx = 0, syy = 0;
    for (int i = 0; i < W; ++i)
    {
        const float a = x[(size_t) (start + i)];
        const float b = x[(size_t) (start + i + li)] * (1.0f - fr) + x[(size_t) (start + i + li + 1)] * fr;
        sxy += (double) a * b; sxx += (double) a * a; syy += (double) b * b;
    }
    return (float) (sxy / (std::sqrt (sxx * syy) + 1.0e-12));
}

//==============================================================================
// "exotic" parameters: allowed, but they must earn their keep (keeps patches smooth and musical)
const std::array<float, kNumParams>& priorWeights()
{
    static const auto w = []
    {
        std::array<float, kNumParams> a {};
        a[P::B_RING] = 0.022f;   a[P::DIST_FOLD] = 0.030f;  a[P::C_INHARM] = 0.025f; a[P::A_FM] = 0.016f;
        a[P::B_FB] = 0.016f;     a[P::COMB_MIX] = 0.012f;   a[P::NOISE_TONAL] = 0.006f; a[P::VOWEL_MIX] = 0.010f;
        a[P::DIST_AMT] = 0.008f; a[P::F_DRIVE] = 0.006f;    a[P::B_DRIFT] = 0.004f;  a[P::C_FORMQ] = 0.004f;
        return a;
    }();
    return w;
}

class Evaluator
{
public:
    // rootFreq: frequency of the root note the patch will be played at; f0: measured fundamental
    Evaluator (const std::vector<float>& target, double rootFreqHz, double f0Hz)
        : tgt (target), rootFreq ((float) rootFreqHz), lag ((float) (kSR / f0Hz))
    {
        N = (int) tgt.size();
        double e = 0;
        for (float v : tgt) e += (double) v * v;
        tgtRms = (float) std::sqrt (e / std::max (N, 1));
        computeFeat (tgt, tf);
        hold = estimateHoldSample (tf.env, N);

        // per resolution band weights (target salience)
        for (int r = 0; r < kRes; ++r)
        {
            const Spec& s = tf.spec[(size_t) r];
            const auto& bm = bandMap (r);
            for (int b = 0; b < kBands; ++b)
            {
                double acc = 0, wsum = 0;
                for (int f = 0; f < s.frames; ++f)
                {
                    const float wf = std::max (0.0f, (s.loud[(size_t) f] - (s.peakLoud - 40.0f)) / 40.0f);
                    acc += wf * s.db[(size_t) f * kBands + (size_t) b]; wsum += wf;
                }
                const float mean = wsum > 0 ? (float) (acc / wsum) : -200.0f;
                const float sal = std::min (1.0f, std::max (0.0f, (mean - (s.peak - 60.0f)) / 50.0f));
                bandW[(size_t) r][(size_t) b] = bm.valid[(size_t) b] ? 0.3f + 0.7f * sal : 0.0f;
            }
        }
        const auto& fc = bandCentres();
        for (int b = 0; b < kBands; ++b)
        {
            const float t = std::min (1.0f, std::max (0.0f, std::log2 (fc[(size_t) b] / 1000.0f) / 2.6f));
            asym[(size_t) b] = 1.0f + 0.6f * t;    // candidate brighter than target is penalised harder
            logFc[(size_t) b] = std::log2 (fc[(size_t) b]);
            isLow[(size_t) b] = fc[(size_t) b] < 160.0f;
        }
        // harmonicity of the target at the measured period
        const int W = lag > 1400.0f ? 4096 : 2048;
        harmW = W;
        for (int k = 0; k < 6; ++k)
        {
            const int start = (int) ((float) (N - W - (int) lag - 4) * ((float) k / 6.0f + 0.02f));
            harmStart[(size_t) k] = std::max (0, start);
            harmT[(size_t) k] = periodicity (tgt, harmStart[(size_t) k], W, lag);
            double en = 0;
            for (int i = 0; i < W && harmStart[(size_t) k] + i < N; ++i) en += (double) tgt[(size_t) (harmStart[(size_t) k] + i)] * tgt[(size_t) (harmStart[(size_t) k] + i)];
            harmDb[(size_t) k] = 10.0f * std::log10 ((float) (en / W) + 1.0e-12f);
        }
        harmPeak = *std::max_element (harmDb.begin(), harmDb.end());
    }

    // Lower is better. Level is normalised away so the optimiser concentrates on timbre.
    float loss (const Patch& pIn, float* gainOut = nullptr) const
    {
        thread_local std::vector<float> cand;
        thread_local Feat cf;
        Patch p = pIn;
        p.level = 0.5f; // gain 1.0 (gain = level^2 * 4)
        renderOffline (p, kSR, rootFreq, N, hold, cand);

        double e = 0;
        for (float v : cand) e += (double) v * v;
        const float rms = (float) std::sqrt (e / std::max (N, 1));
        if (gainOut) *gainOut = 1.0f;
        if (! (rms > 1.0e-5f) || ! std::isfinite (rms)) return 1000.0f;

        const float g = tgtRms / rms;
        if (gainOut) *gainOut = g;
        for (auto& v : cand) v *= g;

        computeFeat (cand, cf);

        float lr[kRes], lflux = 0.0f;
        for (int r = 0; r < kRes; ++r) lr[r] = spectralLoss (r, cf.spec[(size_t) r], r == 0 ? &lflux : nullptr);

        // envelope (broadband loudness over time)
        float lenv = 0.0f;
        {
            const size_t n = std::min (tf.env.size(), cf.env.size());
            const float fl = tf.envPeak - 60.0f;
            double acc = 0, ws = 0;
            for (size_t i = 0; i < n; ++i)
            {
                const float a = std::max (tf.env[i], fl), b = std::max (cf.env[i], fl);
                const float w = 0.25f + std::max (0.0f, std::min (1.0f, (std::max (a, b) - fl) / 60.0f));
                acc += w * std::abs (a - b); ws += w;
            }
            lenv = ws > 0 ? (float) (acc / ws) / 18.0f : 1.0f;
        }

        float lcent = 0.0f, llow = 0.0f;
        centroidLowLoss (cf.spec[1], lcent, llow);
        lcent = std::min (2.0f, lcent);
        llow = std::min (3.0f, llow);

        // harmonicity
        float lharm = 0.0f;
        {
            double acc = 0, ws = 0;
            for (int k = 0; k < 6; ++k)
            {
                double en = 0;
                const int s0 = harmStart[(size_t) k];
                for (int i = 0; i < harmW && s0 + i < N; ++i) en += (double) cand[(size_t) (s0 + i)] * cand[(size_t) (s0 + i)];
                const float dbC = 10.0f * std::log10 ((float) (en / harmW) + 1.0e-12f);
                const float w = std::max (0.0f, std::min (1.0f, (std::max (dbC, harmDb[(size_t) k]) - (harmPeak - 40.0f)) / 40.0f));
                if (w <= 0.0f) continue;
                const float hc = periodicity (cand, s0, harmW, lag);
                const float d = harmT[(size_t) k] - hc;       // positive: candidate less periodic than target (metallic / noisy)
                acc += w * (d > 0 ? d : -0.5f * d); ws += w;
            }
            lharm = ws > 0 ? (float) (acc / ws) / 0.5f : 0.0f;
        }

        float prior = 0.0f;
        const auto& pw = priorWeights();
        for (int i = 0; i < kNumParams; ++i) prior += pw[(size_t) i] * pIn.p[(size_t) i];

        return 0.55f * lr[0] + 0.90f * lr[1] + 0.90f * lr[2]
             + 0.50f * lenv + 0.20f * lflux + 0.35f * lcent + 0.30f * llow + 0.45f * lharm + prior;
    }

private:
    // log-spectral distance of one resolution (+ optionally spectral flux)
    float spectralLoss (int r, const Spec& c, float* fluxOut) const
    {
        const Spec& t = tf.spec[(size_t) r];
        const size_t frames = std::min ((size_t) t.frames, (size_t) c.frames);
        const auto& bm = bandMap (r);
        const float floorDb = t.peak - 70.0f;
        double acc = 0, ws = 0;
        double fluxT = 0, fluxC = 0, fluxW = 0;
        for (size_t f = 0; f < frames; ++f)
        {
            const float wf = 0.12f + 0.88f * std::max (0.0f, std::min (1.0f,
                (std::max (t.loud[f], c.loud[f]) - (t.peakLoud - 45.0f)) / 45.0f));
            const float* tr = &t.db[f * kBands];
            const float* cr = &c.db[f * kBands];
            for (int b = 0; b < kBands; ++b)
            {
                const float wb = bandW[(size_t) r][(size_t) b];
                if (wb <= 0.0f) continue;
                const float tv = std::max (tr[b], floorDb), cv = std::max (cr[b], floorDb);
                const float d = cv - tv;
                const float err = d > 0.0f ? asym[(size_t) b] * d : -d;
                acc += (double) (wf * wb) * err; ws += (double) (wf * wb);
            }
            if (fluxOut != nullptr && f > 0)
            {
                const float* tp = &t.db[(f - 1) * kBands];
                const float* cp = &c.db[(f - 1) * kBands];
                float ft = 0, fc2 = 0; int cnt = 0;
                for (int b = 0; b < kBands; ++b)
                {
                    if (! bm.valid[(size_t) b]) continue;
                    ft += std::max (0.0f, std::max (tr[b], floorDb) - std::max (tp[b], floorDb));
                    fc2 += std::max (0.0f, std::max (cr[b], floorDb) - std::max (cp[b], floorDb));
                    ++cnt;
                }
                if (cnt > 0) { fluxT += wf * ft / cnt; fluxC += wf * fc2 / cnt; fluxW += wf; }
            }
        }
        if (fluxOut != nullptr) *fluxOut = fluxW > 0 ? (float) (std::abs (fluxT - fluxC) / fluxW) / 12.0f : 0.0f;
        return ws > 0 ? (float) (acc / ws) / 25.0f : 1.0f;
    }

    // spectral centroid trajectory (octaves) and bass weight (dB) differences, from the 4096-point spectrogram
    void centroidLowLoss (const Spec& c, float& centLoss, float& lowLoss) const
    {
        const Spec& t = tf.spec[1];
        const size_t frames = std::min ((size_t) t.frames, (size_t) c.frames);
        const float floorDb = t.peak - 70.0f;
        const auto& bm = bandMap (1);
        double cl = 0, ll = 0, w = 0;
        for (size_t f = 0; f < frames; ++f)
        {
            double pc = 0, pcl = 0, lowC = 0, pt = 0, ptl = 0, lowT = 0;
            for (int b = 0; b < kBands; ++b)
            {
                if (! bm.valid[(size_t) b]) continue;
                const float pwC = std::pow (10.0f, std::max (c.db[f * kBands + (size_t) b], floorDb) * 0.1f);
                const float pwT = std::pow (10.0f, std::max (t.db[f * kBands + (size_t) b], floorDb) * 0.1f);
                pc += pwC; pcl += pwC * logFc[(size_t) b]; if (isLow[(size_t) b]) lowC += pwC;
                pt += pwT; ptl += pwT * logFc[(size_t) b]; if (isLow[(size_t) b]) lowT += pwT;
            }
            const float wf = 0.12f + 0.88f * std::max (0.0f, std::min (1.0f,
                (std::max (t.loud[f], c.loud[f]) - (t.peakLoud - 45.0f)) / 45.0f));
            const double centC = pc > 0 ? pcl / pc : 0, centT = pt > 0 ? ptl / pt : 0;
            const double lowRC = 10.0 * std::log10 (lowC / (pc + 1e-30) + 1e-9);
            const double lowRT = 10.0 * std::log10 (lowT / (pt + 1e-30) + 1e-9);
            cl += wf * std::abs (centC - centT); ll += wf * std::abs (lowRC - lowRT);
            w += wf;
        }
        centLoss = w > 0 ? (float) (cl / w) / 1.2f : 1.0f;
        lowLoss = w > 0 ? (float) (ll / w) / 10.0f : 1.0f;
    }

    static int estimateHoldSample (const std::vector<float>& env, int numSamples)
    {
        // Detect a likely release only when a reasonably stable, loud region is followed by a clear drop.
        // Plucks / one-shots often have none: fall back to a conservative 60 %.
        if (env.size() < 10 || numSamples < 4096)
            return (int) ((float) numSamples * 0.6f);

        const auto peakIt = std::max_element (env.begin(), env.end());
        const int peakFrame = (int) std::distance (env.begin(), peakIt);
        const float peakDb = *peakIt;
        const int first = std::max (peakFrame + 4, 4);
        const int last = (int) env.size() - 4;

        for (int i = first; i < last; ++i)
        {
            const float preSlope = std::abs (env[(size_t) (i - 1)] - env[(size_t) (i - 3)]);
            const float pre = (env[(size_t) (i - 3)] + env[(size_t) (i - 2)] + env[(size_t) (i - 1)]) / 3.0f;
            const float post = (env[(size_t) i] + env[(size_t) (i + 1)] + env[(size_t) (i + 2)]) / 3.0f;
            if (pre > peakDb - 8.0f && preSlope < 3.0f && post < pre - 8.0f)
            {
                const int candidate = i * kEnvHop;
                return std::min (std::max (candidate, (int) (numSamples * 0.15f)), (int) (numSamples * 0.9f));
            }
        }
        return (int) ((float) numSamples * 0.6f);
    }

    std::vector<float> tgt;
    float rootFreq = 220.0f, lag = 200.0f, tgtRms = 0.1f;
    int N = 0, hold = 0;
    Feat tf;
    std::array<std::array<float, kBands>, kRes> bandW {};
    std::array<float, kBands> asym {}, logFc {};
    std::array<bool, kBands> isLow {};
    int harmW = 2048;
    std::array<int, 6> harmStart {};
    std::array<float, 6> harmT {}, harmDb {};
    float harmPeak = -100.0f;
};

//==============================================================================
struct Cand { Patch p; float loss = 1000.0f; };

float distance (const Patch& a, const Patch& b)
{
    float s = 0;
    for (int i = 0; i < kNumParams; ++i) { const float d = a.p[(size_t) i] - b.p[(size_t) i]; s += d * d; }
    return std::sqrt (s / (float) kNumParams);
}


//==============================================================================
void parallelFor (int n, int maxThreads, const std::function<void (int)>& fn)
{
    const int nt = std::max (1, std::min (std::min (maxThreads, std::max ((int) std::thread::hardware_concurrency(), 1)), n));
    std::atomic<int> next { 0 };
    auto worker = [&]
    {
       #if defined (__SSE2__)
        _mm_setcsr (_mm_getcsr() | 0x8040);   // flush denormals to zero (filter tails)
       #endif
        for (;;)
        {
            const int i = next.fetch_add (1);
            if (i >= n) break;
            fn (i);
        }
    };
    if (nt == 1) { worker(); return; }
    std::vector<std::thread> th;
    for (int t = 1; t < nt; ++t) th.emplace_back (worker);
    worker();
    for (auto& t : th) t.join();
}

std::vector<Patch> allSeeds()
{
    auto v = archetypes();
   #if KRAKEN_HAS_FACTORY
    for (const auto& d : factoryDefs()) v.push_back (factoryPatch (d));
   #endif
    return v;
}

// heuristic confidence (0..100): calibrated on self-matches (~0.1) and unmatched sounds (>1.5)
float matchFromLoss (float loss)
{
    const float t = std::min (1.0f, std::max (0.0f, 1.0f - (loss - 0.05f) / 1.55f));
    return 100.0f * std::pow (t, 1.4f);
}

} // namespace

//==============================================================================
AnalysisResult runAnalysis (std::vector<float> input, double inputSr,
                            const std::function<bool()>& shouldExit,
                            const std::function<void (float)>& progress,
                            const AnalysisOptions& opt)
{
    AnalysisResult result;
    auto exitNow = [&] { return shouldExit && shouldExit(); };
    auto report = [&] (float v) { if (progress) progress (std::min (0.99f, std::max (0.0f, v))); };
    const int maxThreads = std::max (1, opt.maxThreads);

    // ---- 1. prepare the target ------------------------------------------------
    const size_t maxIn = (size_t) (inputSr * 12.0);
    if (input.size() > maxIn) input.resize (maxIn);
    auto sig = resample (input, inputSr, kSR);
    input.clear();
    float peak = 0;
    for (float v : sig) peak = std::max (peak, std::abs (v));
    if (sig.size() < 2048 || peak < 1.0e-5f) return result;

    size_t start = 0;
    if (opt.trimSilence)
    {
        while (start < sig.size() && std::abs (sig[start]) < 0.02f * peak) ++start;
        start = start > 128 ? start - 128 : 0;
    }
    const size_t maxLen = (size_t) (kSR * std::min (4.0f, std::max (0.2f, opt.maxSeconds)));
    const size_t len = std::min (sig.size() - start, maxLen);
    std::vector<float> tgt (sig.begin() + (long) start, sig.begin() + (long) (start + len));
    if ((int) tgt.size() < kMinLen) tgt.resize ((size_t) kMinLen, 0.0f);
    sig.clear();

    {
        float pk = 0;
        for (float v : tgt) pk = std::max (pk, std::abs (v));
        for (auto& v : tgt) v *= 0.9f / std::max (pk, 1.0e-6f);
        const int fade = std::min (441, (int) tgt.size());
        for (int i = 0; i < fade; ++i) tgt[tgt.size() - 1 - (size_t) i] *= (float) i / (float) fade;
    }

    // ---- 2. pitch ---------------------------------------------------------------
    std::vector<float> cands;
    {
        auto addCand = [&] (float f)
        {
            if (f < 28.0f || f > 4200.0f) return;
            for (float c : cands) if (std::abs (std::log2 (f / c)) < 0.03f) return;
            cands.push_back (f);
        };
        const auto est = detectPitchCandidates (tgt, kSR);
        // most frequent estimate first (median-ish), then octave / fifth variants
        std::vector<float> sorted = est;
        std::sort (sorted.begin(), sorted.end());
        if (! sorted.empty())
        {
            const float med = sorted[sorted.size() / 2];
            addCand (med); addCand (med * 2.0f); addCand (med * 0.5f);
            for (float e : sorted) addCand (e);
            addCand (med * 1.5f); addCand (med / 1.5f); addCand (med * 4.0f); addCand (med * 0.25f);
        }
        if (cands.empty()) { addCand (110.0f); addCand (220.0f); addCand (440.0f); addCand (55.0f); }
        if (cands.size() > 12) cands.resize (12);
    }

    auto rootOf = [] (float f) { return (int) std::lround (69.0 + 12.0 * std::log2 ((double) f / 440.0)); };
    auto noteFreq = [] (int n) { return 440.0 * std::pow (2.0, (double) (n - 69) / 12.0); };
    auto tuneOf = [&] (float f, int note) { return (float) (12.0 * std::log2 ((double) f / noteFreq (note))); };

    const auto seeds = allSeeds();
    std::vector<Patch> voteSet = archetypes();
   #if KRAKEN_HAS_FACTORY
    {
        const auto& fd = factoryDefs();
        for (size_t i = 0; i < fd.size(); i += 4) voteSet.push_back (factoryPatch (fd[i]));
    }
   #endif

    std::unique_ptr<Evaluator> evPtr;
    float freq = cands[0];
    {
        float bestVote = 1.0e9f;
        for (float f : cands)
        {
            if (exitNow()) return result;
            const int note = rootOf (f);
            auto e = std::make_unique<Evaluator> (tgt, noteFreq (note), (double) f);
            const float t0 = tuneOf (f, note);
            std::vector<float> l (voteSet.size());
            parallelFor ((int) voteSet.size(), maxThreads, [&] (int i)
            {
                Patch p = voteSet[(size_t) i]; p.tune = t0;
                l[(size_t) i] = e->loss (p);
            });
            std::sort (l.begin(), l.end());
            // small bias towards the primary estimate so that equal scores do not flip octaves
            const float vote = 0.5f * (l[0] + l[1]) + (f == cands[0] ? 0.0f : 0.02f);
            if (vote < bestVote) { bestVote = vote; freq = f; evPtr = std::move (e); }
            report (0.04f * (float) (&f - &cands[0] + 1) / (float) cands.size());
        }
    }
    Evaluator& ev = *evPtr;
    const int rootNote = rootOf (freq);
    const float tune0 = tuneOf (freq, rootNote);

    // ---- 3. search --------------------------------------------------------------
    std::mt19937 rng (opt.seed);
    std::normal_distribution<float> nd (0.0f, 1.0f);
    std::uniform_real_distribution<float> ud (0.0f, 1.0f);
    // longer regions cost proportionally more per evaluation: trim the search a little (never below half)
    const float lenSec = (float) tgt.size() / (float) kSR;
    const float eff = std::max (0.05f, opt.effort) * std::min (1.0f, std::max (0.5f, std::sqrt (1.1f / std::max (lenSec, 0.1f))));

    auto evalMany = [&] (std::vector<Cand>& c)
    {
        parallelFor ((int) c.size(), maxThreads, [&] (int i) { c[(size_t) i].loss = ev.loss (c[(size_t) i].p); });
    };
    auto byLoss = [] (const Cand& a, const Cand& b) { return a.loss < b.loss; };
    auto clampv = [] (float v) { return std::min (1.0f, std::max (0.0f, v)); };
    auto mutated = [&] (const Patch& base, float sigma, float prob)
    {
        Patch m = base;
        bool any = false;
        for (auto& v : m.p)
            if (ud (rng) < prob) { v = clampv (v + sigma * nd (rng)); any = true; }
        if (! any)
        {
            auto& v = m.p[(size_t) (rng() % (unsigned) kNumParams)];
            v = clampv (v + sigma * nd (rng));
        }
        return m;
    };

    constexpr int mu = 10;
    const int nIslands = 3;
    const int islandGens = std::max (4, (int) (30 * eff)), islandLambda = 36;
    const int finalGens = std::max (6, (int) (70 * eff)), finalLambda = 48;
    constexpr int polishRounds = 4;
    const float totalSteps = (float) (3 + nIslands * islandGens + finalGens + polishRounds + 2);
    float stepsDone = 0.0f;
    auto step = [&] { stepsDone += 1.0f; report (0.05f + 0.93f * stepsDone / totalSteps); };

    // 3a. screen every seed, plus one wild variant of each
    std::vector<Cand> pop;
    for (const auto& a : seeds) { Cand c; c.p = a; c.p.tune = tune0; pop.push_back (c); }
    const size_t nSeeds = pop.size();
    for (size_t i = 0; i < nSeeds; ++i) { Cand c; c.p = mutated (pop[i].p, 0.28f, 0.35f); c.p.tune = tune0; pop.push_back (c); }
    evalMany (pop);
    if (exitNow()) return result;
    std::sort (pop.begin(), pop.end(), byLoss);
    step();

    // 3b. spawn around the best distinct seeds
    {
        std::vector<size_t> elites;
        for (size_t i = 0; i < pop.size() && elites.size() < 8; ++i)
        {
            bool far = true;
            for (size_t e : elites) if (distance (pop[i].p, pop[e].p) < 0.08f) { far = false; break; }
            if (far) elites.push_back (i);
        }
        std::vector<Cand> spawn;
        for (size_t e : elites)
            for (int k = 0; k < 16; ++k) { Cand c; c.p = mutated (pop[e].p, 0.12f, 0.25f); c.p.tune = tune0; spawn.push_back (c); }
        evalMany (spawn);
        if (exitNow()) return result;
        pop.insert (pop.end(), spawn.begin(), spawn.end());
        std::sort (pop.begin(), pop.end(), byLoss);
        step();
    }
    const float poolMedian = pop[pop.size() / 2].loss;
    (void) poolMedian;

    // log-rank recombination weights
    std::vector<float> w ((size_t) mu);
    {
        float wsum = 0;
        for (int i = 0; i < mu; ++i) { w[(size_t) i] = std::log ((float) mu + 0.5f) - std::log ((float) i + 1.0f); wsum += w[(size_t) i]; }
        for (auto& v : w) v /= wsum;
    }

    struct Island
    {
        std::array<float, kNumParams> mean {}, sigma {};
        Cand best;
        std::vector<Cand> archive;
    };

    auto runES = [&] (Island& isl, int gens, int lambda, float phase0, float phase1) -> bool
    {
        for (int g = 0; g < gens; ++g)
        {
            if (exitNow()) return false;
            const float phase = phase0 + (phase1 - phase0) * (float) g / (float) std::max (gens - 1, 1);
            std::vector<Cand> off ((size_t) lambda);
            for (int k = 0; k < lambda; ++k)
            {
                const bool sparse = (k & 1) != 0;
                auto& c = off[(size_t) k];
                c.p.tune = tune0;
                bool any = false;
                for (int d = 0; d < kNumParams; ++d)
                {
                    const int arm = armOf (d);
                    // early: sound sources, filter, envelopes. later: harmonics, noise, FX.
                    float armChance;
                    if (phase < 0.35f)      armChance = (arm == 0 || arm == 1 || arm == 4 || arm == 5 || arm == 6) ? 0.90f : 0.28f;
                    else if (phase < 0.75f) armChance = (arm == 7) ? 0.42f : 0.72f;
                    else                    armChance = (arm == 2 || arm == 3 || arm == 7) ? 0.72f : 0.58f;
                    const float chance = sparse ? armChance * 0.22f : armChance;
                    const bool mut = ud (rng) < chance;
                    any = any || mut;
                    c.p.p[(size_t) d] = clampv (isl.mean[(size_t) d] + (mut ? isl.sigma[(size_t) d] * nd (rng) : 0.0f));
                }
                if (! any)
                {
                    const int d = (int) (rng() % (unsigned) kNumParams);
                    c.p.p[(size_t) d] = clampv (isl.mean[(size_t) d] + isl.sigma[(size_t) d] * nd (rng));
                }
            }
            evalMany (off);
            std::sort (off.begin(), off.end(), byLoss);
            if (off[0].loss < isl.best.loss) isl.best = off[0];
            for (int k = 0; k < 3; ++k) isl.archive.push_back (off[(size_t) k]);

            for (int d = 0; d < kNumParams; ++d)
            {
                float m = 0;
                for (int i = 0; i < mu; ++i) m += w[(size_t) i] * off[(size_t) i].p.p[(size_t) d];
                float var = 0;
                for (int i = 0; i < mu; ++i) { const float df = off[(size_t) i].p.p[(size_t) d] - m; var += w[(size_t) i] * df * df; }
                isl.mean[(size_t) d] = m;
                isl.sigma[(size_t) d] = std::max (0.01f, 0.7f * isl.sigma[(size_t) d] + 0.3f * 1.3f * std::sqrt (var));
            }
            step();
        }
        return true;
    };

    // 3c. islands: independent searches started from the best *different* seeds
    std::vector<Island> islands;
    {
        std::vector<size_t> elites;
        for (size_t i = 0; i < pop.size() && (int) elites.size() < nIslands; ++i)
        {
            bool far = true;
            for (size_t e : elites) if (distance (pop[i].p, pop[e].p) < 0.14f) { far = false; break; }
            if (far) elites.push_back (i);
        }
        for (size_t i = 0; i < pop.size() && (int) elites.size() < nIslands; ++i)
            if (std::find (elites.begin(), elites.end(), i) == elites.end()) elites.push_back (i);

        for (size_t e : elites)
        {
            Island isl;
            for (int d = 0; d < kNumParams; ++d) { isl.mean[(size_t) d] = pop[e].p.p[(size_t) d]; isl.sigma[(size_t) d] = 0.14f; }
            isl.best = pop[e];
            isl.archive.push_back (pop[e]);
            islands.push_back (std::move (isl));
        }
    }
    for (auto& isl : islands)
        if (! runES (isl, islandGens, islandLambda, 0.0f, 0.45f)) return result;

    // 3d. continue from the best island (its archive and mean carry over)
    size_t bi = 0;
    for (size_t i = 1; i < islands.size(); ++i) if (islands[i].best.loss < islands[bi].best.loss) bi = i;
    Island fin = std::move (islands[bi]);
    for (size_t i = 0; i < islands.size(); ++i)
        if (i != bi) for (auto& c : islands[i].archive) fin.archive.push_back (c);
    for (int d = 0; d < kNumParams; ++d)
    {
        fin.mean[(size_t) d] = fin.best.p.p[(size_t) d];
        fin.sigma[(size_t) d] = std::max (0.05f, fin.sigma[(size_t) d]);
    }
    if (! runES (fin, finalGens, finalLambda, 0.45f, 1.0f)) return result;

    // 3e. tune line search + coordinate polish
    Patch cur = fin.best.p;
    float curLoss = fin.best.loss;
    auto tuneSearch = [&]
    {
        for (float st : { 0.10f, 0.05f, 0.025f, 0.012f, 0.006f })
        {
            std::vector<Cand> pr (2);
            pr[0].p = cur; pr[0].p.tune = cur.tune - st;
            pr[1].p = cur; pr[1].p.tune = cur.tune + st;
            evalMany (pr);
            const Cand& b = pr[0].loss < pr[1].loss ? pr[0] : pr[1];
            if (b.loss < curLoss) { cur = b.p; curLoss = b.loss; }
        }
    };
    tuneSearch();
    static const float polishStep[polishRounds] = { 0.06f, 0.035f, 0.02f, 0.010f };
    for (int round = 0; round < polishRounds; ++round)
    {
        if (exitNow()) return result;
        const float st = polishStep[round];
        std::vector<Cand> probes;
        for (int d = 0; d < kNumParams; ++d)
            for (int sgn = -1; sgn <= 1; sgn += 2)
            {
                Cand c; c.p = cur;
                c.p.p[(size_t) d] = clampv (cur.p[(size_t) d] + (float) sgn * st);
                probes.push_back (c);
            }
        evalMany (probes);
        Patch comb = cur;
        bool any = false;
        Cand bestSingle; bestSingle.loss = curLoss;
        for (int d = 0; d < kNumParams; ++d)
        {
            const Cand& a = probes[(size_t) (2 * d)];
            const Cand& b = probes[(size_t) (2 * d + 1)];
            const Cand& wn = a.loss < b.loss ? a : b;
            if (wn.loss < curLoss)
            {
                comb.p[(size_t) d] = wn.p.p[(size_t) d];
                any = true;
                if (wn.loss < bestSingle.loss) bestSingle = wn;
            }
        }
        if (any)
        {
            const float cl = ev.loss (comb);
            if (cl < curLoss) { cur = comb; curLoss = cl; }
            else if (bestSingle.loss < curLoss) { cur = bestSingle.p; curLoss = bestSingle.loss; }
        }
        step();
    }
    tuneSearch();
    step();

    // ---- 4. build result -------------------------------------------------------------
    auto setLevel = [&] (Patch& p)
    {
        float g = 1.0f;
        ev.loss (p, &g);
        p.level = std::sqrt (std::min (std::max (g / 4.0f, 0.02f), 1.0f));
    };

    Patch bestPatch = cur;
    setLevel (bestPatch);
    result.best = bestPatch;
    result.kin.push_back (bestPatch);

    std::sort (fin.archive.begin(), fin.archive.end(), byLoss);
    std::vector<Patch> chosen { cur };
    for (const auto& c : fin.archive)
    {
        if (chosen.size() >= 4) break;
        bool far = true;
        for (const auto& o : chosen) if (distance (c.p, o) < 0.10f) { far = false; break; }
        if (far) { chosen.push_back (c.p); Patch q = c.p; setLevel (q); result.kin.push_back (q); }
    }
    while (result.kin.size() < 6)
    {
        Patch m = mutated (cur, 0.15f, 0.3f);
        setLevel (m);
        result.kin.push_back (m);
    }

    result.loss = curLoss;
    result.match = matchFromLoss (curLoss);
    result.freq = freq;
    result.midiNote = rootNote;
    result.ok = true;
    report (1.0f);
    return result;
}

} // namespace kraken
