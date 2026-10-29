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
#include <cstdio>
#include <cstdlib>
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

const std::array<float, kBands>& bandCentresLog2()
{
    static const auto c = []
    {
        std::array<float, kBands> a {};
        for (int j = 0; j < kBands; ++j) a[(size_t) j] = std::log2 (bandCentres()[(size_t) j]);
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

// Loss weights. Compile with -DKRAKEN_TUNING_ENV to override them from environment variables (used by tests/).
struct Tuning
{
    float asym = 0.6f;                                    // extra penalty (0..) when the candidate is brighter than the target
    float wSpec[3] = { 0.55f, 0.90f, 0.90f };
    float wEnv = 0.50f, wFlux = 0.20f, wCent = 0.35f, wLow = 0.30f, wHarm = 0.45f, priorScale = 1.0f;
    float excessDb = 15.0f, excessW = 0.9f;               // candidate louder than target by more than this: weight at least excessW
    bool extraSeeds = true, halving = false;           // search features (switchable for experiments)
};

const Tuning& tuning()
{
    static const Tuning t = []
    {
        Tuning x;
       #ifdef KRAKEN_TUNING_ENV
        auto env = [] (const char* n, float d) { const char* v = std::getenv (n); return v != nullptr ? (float) std::atof (v) : d; };
        x.asym = env ("KT_ASYM", x.asym);
        x.wSpec[0] = env ("KT_S0", x.wSpec[0]); x.wSpec[1] = env ("KT_S1", x.wSpec[1]); x.wSpec[2] = env ("KT_S2", x.wSpec[2]);
        x.wEnv = env ("KT_ENV", x.wEnv); x.wFlux = env ("KT_FLUX", x.wFlux); x.wCent = env ("KT_CENT", x.wCent);
        x.extraSeeds = env ("KT_NOEXTRA", 0.0f) < 0.5f; x.halving = env ("KT_SH", 0.0f) > 0.5f;
        x.excessDb = env ("KT_EXDB", x.excessDb); x.excessW = env ("KT_EXW", x.excessW);
        x.wLow = env ("KT_LOW", x.wLow); x.wHarm = env ("KT_HARM", x.wHarm); x.priorScale = env ("KT_PRIOR", x.priorScale);
       #endif
        return x;
    }();
    return t;
}

// Modulation estimate: is the sound wobbling (filter LFO), vibrato-ing or tremolo-ing, how fast and how deep?
struct ModEstimate
{
    bool filt = false, vib = false, trem = false;
    float rate = 5.0f;                       // Hz (of the strongest detected modulation)
    float filtOct = 0.0f, vibSt = 0.0f, tremDepth = 0.0f;   // peak depths
    float filtScore = 0.0f, vibScore = 0.0f, tremScore = 0.0f;
};

// remove slow trends (moving average of 2*half+1 samples) and the mean
void detrend (std::vector<float>& x, int half)
{
    const int n = (int) x.size();
    std::vector<double> cs ((size_t) n + 1, 0.0);
    for (int i = 0; i < n; ++i) cs[(size_t) i + 1] = cs[(size_t) i] + x[(size_t) i];
    std::vector<float> out ((size_t) n);
    for (int i = 0; i < n; ++i)
    {
        const int a = std::max (0, i - half), b = std::min (n, i + half + 1);
        out[(size_t) i] = x[(size_t) i] - (float) ((cs[(size_t) b] - cs[(size_t) a]) / (double) (b - a));
    }
    x = out;
}

// dominant period (in samples of x) by autocorrelation; false if the trajectory is not clearly periodic
bool findPeriod (const std::vector<float>& x, int minLag, int maxLag, float& lagOut, float& scoreOut)
{
    const int n = (int) x.size();
    maxLag = std::min (maxLag, n / 2);
    if (n < 12 || maxLag <= minLag + 1) return false;
    double e = 0;
    for (float v : x) e += (double) v * v;
    if (e < 1.0e-12) return false;
    std::vector<float> r ((size_t) maxLag + 2, 0.0f);
    for (int l = 1; l <= maxLag + 1 && l < n; ++l)
    {
        double s = 0;
        for (int i = 0; i + l < n; ++i) s += (double) x[(size_t) i] * x[(size_t) (i + l)];
        r[(size_t) l] = (float) (s / e * (double) n / (double) (n - l));
    }
    float best = 0.0f;
    for (int l = minLag; l <= maxLag; ++l)
        if (r[(size_t) l] > r[(size_t) l - 1] && r[(size_t) l] >= r[(size_t) l + 1]) best = std::max (best, r[(size_t) l]);
    if (best < 0.35f) return false;
    for (int l = minLag; l <= maxLag; ++l)   // smallest lag that is nearly as good (avoids picking multiples)
        if (r[(size_t) l] > r[(size_t) l - 1] && r[(size_t) l] >= r[(size_t) l + 1] && r[(size_t) l] >= 0.8f * best)
        {
            const float a = r[(size_t) l - 1], b = r[(size_t) l], c = r[(size_t) l + 1];
            const float den = a - 2.0f * b + c;
            lagOut = (float) l + (std::abs (den) > 1.0e-9f ? 0.5f * (a - c) / den : 0.0f);
            scoreOut = b;
            return true;
        }
    return false;
}

float stdDev (const std::vector<float>& x)
{
    double e = 0;
    for (float v : x) e += (double) v * v;
    return x.empty() ? 0.0f : (float) std::sqrt (e / (double) x.size());
}

ModEstimate estimateModulation (const Feat& tf, const std::vector<float>& tgt, float lag)
{
    ModEstimate m;
    float bestScore = 0.0f;
    auto consider = [&] (float rate, float score) { if (score > bestScore) { bestScore = score; m.rate = rate; } };

    // 1. brightness (spectral centroid) trajectory -> filter LFO
    {
        const Spec& sp = tf.spec[1];
        const float hopSec = (float) kFftHop[1] / (float) kSR;
        std::vector<float> c ((size_t) sp.frames, 0.0f);
        float prev = 0.0f; int valid = 0;
        for (int f = 0; f < sp.frames; ++f)
        {
            double pw = 0, pl = 0;
            for (int b = 0; b < kBands; ++b)
            {
                if (! bandMap (1).valid[(size_t) b]) continue;
                const float p = std::pow (10.0f, std::max (sp.db[(size_t) f * kBands + (size_t) b], sp.peak - 60.0f) * 0.1f);
                pw += p; pl += p * bandCentresLog2()[(size_t) b];
            }
            if (sp.loud[(size_t) f] > sp.peakLoud - 35.0f && pw > 0) { prev = (float) (pl / pw); ++valid; }
            c[(size_t) f] = prev;
        }
        if (valid >= 14)
        {
            detrend (c, std::max (3, (int) (0.5f / hopSec)));
            float lg, sc;
            if (findPeriod (c, 3, (int) (2.0f / hopSec), lg, sc))
            {
                const float depth = 1.414f * stdDev (c);
                if (depth > 0.10f) { m.filt = true; m.filtOct = depth; m.filtScore = sc; consider (1.0f / (lg * hopSec), sc); }
            }
        }
    }

    // 2. pitch track -> vibrato
    {
        const int W = 1536, hop = 1024;
        const int lo = std::max (4, (int) (lag * 0.92f)), hi = (int) (lag * 1.08f) + 1;
        const int n = (int) tgt.size();
        if (lag > 12.0f && hi - lo >= 3 && n > W + hi + 8)
        {
            std::vector<float> cents;
            float prevC = 0.0f;
            std::vector<float> d ((size_t) hi + 2);
            for (int s0 = 0; s0 + W + hi + 2 < n; s0 += hop)
            {
                double en = 0;
                for (int j = 0; j < W; ++j) en += (double) tgt[(size_t) (s0 + j)] * tgt[(size_t) (s0 + j)];
                float bestD = 1.0e30f; int bt = -1;
                for (int tau = lo - 1; tau <= hi + 1; ++tau)
                {
                    double sd = 0;
                    for (int j = 0; j < W; ++j) { const float df = tgt[(size_t) (s0 + j)] - tgt[(size_t) (s0 + j + tau)]; sd += (double) df * df; }
                    d[(size_t) tau] = (float) sd;
                    if (tau >= lo && tau <= hi && (float) sd < bestD) { bestD = (float) sd; bt = tau; }
                }
                if (bt > lo && bt < hi && en > 1.0e-4 * W && bestD < 0.35 * 2.0 * en)
                {
                    const float a = d[(size_t) bt - 1], b = d[(size_t) bt], c2 = d[(size_t) bt + 1];
                    const float den = a - 2.0f * b + c2;
                    const float tauF = (float) bt + (std::abs (den) > 1.0e-9f ? 0.5f * (a - c2) / den : 0.0f);
                    prevC = 1200.0f * std::log2 (lag / tauF);
                }
                cents.push_back (prevC);
            }
            if (cents.size() >= 10)
            {
                const float hopSec = (float) hop / (float) kSR;
                detrend (cents, std::max (3, (int) (0.5f / hopSec)));
                float lg, sc;
                if (findPeriod (cents, 3, (int) (1.5f / hopSec), lg, sc))
                {
                    const float st = 1.414f * stdDev (cents) / 100.0f;
                    if (st > 0.04f) { m.vib = true; m.vibSt = st; m.vibScore = sc; consider (1.0f / (lg * hopSec), sc); }
                }
            }
        }
    }

    // 3. loudness trajectory -> tremolo (RMS window of >= 3 periods so low notes do not alias)
    {
        const int hop = kEnvHop;
        const int win = std::min (4410, std::max (882, (int) (3.0f * lag)));
        const int n = (int) tgt.size();
        std::vector<float> e;
        for (int s0 = 0; s0 + win <= n; s0 += hop)
        {
            double en = 0;
            for (int j = 0; j < win; ++j) en += (double) tgt[(size_t) (s0 + j)] * tgt[(size_t) (s0 + j)];
            e.push_back (10.0f * std::log10 ((float) (en / win) + 1.0e-9f));
        }
        if (e.size() >= 30)
        {
            const float pk = *std::max_element (e.begin(), e.end());
            for (auto& v : e) v = std::max (v, pk - 30.0f);
            const float hopSec = (float) hop / (float) kSR;
            detrend (e, std::max (3, (int) (0.5f / hopSec)));
            float lg, sc;
            if (findPeriod (e, 3, (int) (1.5f / hopSec), lg, sc))
            {
                const float rate = 1.0f / (lg * hopSec);
                const float ppDb = 2.0f * 1.414f * stdDev (e) ;
                const float depth = 1.0f - std::pow (10.0f, -ppDb / 10.0f);
                if (sc >= 0.45f && ppDb > 1.0f && depth < 0.95f && rate * (float) n / (float) kSR >= 2.2f)
                { m.trem = true; m.tremDepth = depth; m.tremScore = sc; consider (rate, sc); }
            }
        }
    }
    return m;
}

// ---------------------------------------------------------------------------------------------
// Amplitude envelope estimate -> AMP_A / AMP_D / AMP_S / AMP_R (normalised engine parameters)
struct AmpEnvEstimate { float a = 0.02f, d = 0.5f, s = 0.8f, r = 0.3f, fall = 0.0f; bool valid = false; };

AmpEnvEstimate estimateAmpEnv (const std::vector<float>& env, int holdSample)
{
    AmpEnvEstimate e;
    if (env.size() < 8) return e;
    const float hop = (float) kEnvHop / (float) kSR;
    const int n = (int) env.size();
    const int pk = (int) (std::max_element (env.begin(), env.end()) - env.begin());
    const float peak = env[(size_t) pk];
    auto lg = [] (float v, float lo, float hi) { return std::min (1.0f, std::max (0.0f, std::log (v / lo) / std::log (hi / lo))); };

    int first = 0;
    while (first < pk && env[(size_t) first] < peak - 25.0f) ++first;
    const float att = std::max (0.0008f, (float) (pk - first) * hop + 0.002f);

    const int hf = std::min (n - 1, std::max (pk + 1, holdSample / kEnvHop));
    const float atHold = env[(size_t) std::max (pk, hf - 2)];
    const float rel = std::pow (10.0f, (atHold - peak) / 20.0f);   // linear level at note-off relative to the peak
    float sus = 0.0f, dec = 0.3f;
    if (rel > 0.3f)
    {
        sus = rel;                                   // sustained sound
        int i = pk;
        while (i < hf && env[(size_t) i] > peak + 20.0f * std::log10 (std::max (sus * 1.15f, 1.0e-3f))) ++i;
        dec = std::max (0.02f, (float) (i - pk) * hop * 2.0f);

        // the level keeps sinking while the note is held (almost every real sound does): measure the slope over the later part
        const int ia = pk + (int) (0.4f * (float) (hf - pk));
        if (hf - 2 - ia >= 5)
        {
            const float slope = (env[(size_t) ia] - atHold) / ((float) (hf - 2 - ia) * hop);   // dB per second
            if (slope > 4.0f)
            {
                e.fall = lg (20.0f / slope, 20.0f, 0.05f);                                     // time per -20 dB: 20 s .. 0.05 s -> 0 .. 1
                const float fallAtA = std::pow (10.0f, -slope * (float) (ia - pk) * hop / 20.0f);
                const float lvlA = std::pow (10.0f, (env[(size_t) ia] - peak) / 20.0f);
                sus = std::min (1.0f, lvlA / std::max (fallAtA, 1.0e-3f));
            }
        }
    }
    else
    {
        // decaying sound: time to fall 20 dB, doubled = time constant to -40 dB (what AMP_D means)
        int i = pk;
        while (i < n - 1 && env[(size_t) i] > peak - 20.0f) ++i;
        if (env[(size_t) i] > peak - 20.0f)          // never fell that far: extrapolate the slope
        {
            const float slope = (peak - env[(size_t) (n - 1)]) / std::max (1.0f, (float) (n - 1 - pk) * hop);
            dec = slope > 0.5f ? 40.0f / slope : 8.0f;
        }
        else dec = std::max (0.02f, (float) (i - pk) * hop * 2.0f);
    }

    float relT = 0.2f;
    {
        int i = hf;
        const float lvl = env[(size_t) hf];
        while (i < n - 1 && env[(size_t) i] > lvl - 20.0f) ++i;
        if (env[(size_t) i] <= lvl - 20.0f) relT = std::max (0.02f, (float) (i - hf) * hop * 2.0f);
    }
    e.a = lg (att, 0.0008f, 5.0f);
    e.d = lg (std::min (dec, 8.0f), 0.005f, 8.0f);
    e.s = std::min (1.0f, sus);
    e.r = lg (std::min (relT, 8.0f), 0.005f, 8.0f);
    e.valid = true;
    return e;
}

// ---------------------------------------------------------------------------------------------
// Strongest steady partials of the target (Hz, relative amplitude)
struct Partial { float hz = 0, amp = 0; };

std::vector<Partial> findPartials (const std::vector<float>& x, const Spec& s8192)
{
    const Plan& plan = planFor (2);
    const int N = kFftSize[2], n = (int) x.size();
    std::vector<float> re ((size_t) N), im ((size_t) N);
    std::vector<double> pw ((size_t) N / 2 + 1, 0.0);
    int used = 0;
    for (int f = 0; f < s8192.frames && used < 4; ++f)
    {
        if (s8192.loud[(size_t) f] < s8192.peakLoud - 12.0f) continue;
        const int st = f * kFftHop[2];
        for (int i = 0; i < N; ++i) { re[(size_t) i] = st + i < n ? x[(size_t) (st + i)] * plan.win[(size_t) i] : 0.0f; im[(size_t) i] = 0.0f; }
        plan.fft (re.data(), im.data());
        for (int k = 0; k <= N / 2; ++k) pw[(size_t) k] += (double) re[(size_t) k] * re[(size_t) k] + (double) im[(size_t) k] * im[(size_t) k];
        ++used;
    }
    std::vector<Partial> out;
    if (used == 0) return out;
    const double binHz = kSR / N;
    double mx = 0;
    const int kLo = (int) (30.0 / binHz), kHi = (int) (9000.0 / binHz);
    for (int k = kLo; k <= kHi; ++k) mx = std::max (mx, pw[(size_t) k]);
    std::vector<std::pair<double, int>> peaks;
    for (int k = kLo + 2; k < kHi - 1; ++k)
        if (pw[(size_t) k] > pw[(size_t) k - 1] && pw[(size_t) k] >= pw[(size_t) k + 1] && pw[(size_t) k] > mx * 1.0e-3)
            peaks.push_back ({ pw[(size_t) k], k });
    std::sort (peaks.begin(), peaks.end(), [] (const auto& a, const auto& b) { return a.first > b.first; });
    for (const auto& p : peaks)
    {
        const int k = p.second;
        bool close = false;
        for (const auto& o : out) if (std::abs (o.hz - (float) (k * binHz)) < 25.0f) { close = true; break; }
        if (close) continue;
        const double a = std::log (pw[(size_t) k - 1] + 1e-30), b = std::log (pw[(size_t) k] + 1e-30), c = std::log (pw[(size_t) k + 1] + 1e-30);
        const double den = a - 2 * b + c, off = std::abs (den) > 1e-12 ? 0.5 * (a - c) / den : 0.0;
        out.push_back ({ (float) ((k + std::max (-0.5, std::min (0.5, off))) * binHz), (float) std::sqrt (p.first / mx) });
        if (out.size() >= 8) break;
    }
    return out;
}

// ---------------------------------------------------------------------------------------------
// Burst pattern at the start of the sound (hand claps: 3-5 noise slaps 8-20 ms apart, then the tail)
struct BurstEstimate { bool valid = false; int count = 1; float spacingSec = 0.01f; };

BurstEstimate estimateBursts (const std::vector<float>& x)
{
    BurstEstimate b;
    const int hop = 44, win = 88;                    // 1 ms hop, 2 ms window
    const int n = std::min ((int) x.size(), (int) (0.12 * kSR));
    std::vector<float> e;
    for (int s0 = 0; s0 + win <= n; s0 += hop)
    {
        double en = 0;
        for (int j = 0; j < win; ++j) en += (double) x[(size_t) (s0 + j)] * x[(size_t) (s0 + j)];
        e.push_back (10.0f * std::log10 ((float) (en / win) + 1.0e-10f));
    }
    if (e.size() < 20) return b;
    const float peakDb = *std::max_element (e.begin(), e.end());
    // peaks: local maxima that stand 4 dB above the deepest dip between them and the previous peak
    std::vector<int> peaks;
    int lastPeak = -1;
    float dip = 1.0e9f;
    for (size_t i = 1; i + 1 < e.size(); ++i)
    {
        if (lastPeak >= 0) dip = std::min (dip, e[i]);
        if (e[i] > e[i - 1] && e[i] >= e[i + 1] && e[i] > peakDb - 25.0f)
        {
            if (lastPeak < 0) { peaks.push_back ((int) i); lastPeak = (int) i; dip = 1.0e9f; }
            else if (e[i] - dip >= 4.0f && (int) i - lastPeak >= 3) { peaks.push_back ((int) i); lastPeak = (int) i; dip = 1.0e9f; }
        }
    }
    if (peaks.size() < 2) return b;
    std::vector<float> gaps;
    for (size_t i = 1; i < peaks.size(); ++i) gaps.push_back ((float) (peaks[i] - peaks[i - 1]) * 0.001f);
    std::sort (gaps.begin(), gaps.end());
    const float med = gaps[gaps.size() / 2];
    if (med < 0.004f || med > 0.04f) return b;
    int regular = 1;
    for (size_t i = 1; i < peaks.size(); ++i)
        if (std::abs ((float) (peaks[i] - peaks[i - 1]) * 0.001f - med) < 0.4f * med) ++regular;
    if (regular < 2) return b;
    b.valid = true;
    b.count = std::min (6, std::max (2, regular));
    b.spacingSec = med;
    return b;
}

struct LossBreakdown { float spec[3] = {}, env = 0, flux = 0, cent = 0, low = 0, harm = 0, prior = 0, total = 0; };

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
            asym[(size_t) b] = 1.0f + tuning().asym * t;    // candidate brighter than target is penalised harder
            logFc[(size_t) b] = std::log2 (fc[(size_t) b]);
            isLow[(size_t) b] = fc[(size_t) b] < 160.0f;
        }
        mod = estimateModulation (tf, tgt, lag);
        ampEnv = estimateAmpEnv (tf.env, hold);
        partials = findPartials (tgt, tf.spec[2]);

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

        // clap bursts only make sense for noise-like material (a low periodic tone also has "peaks" every few ms)
        {
            double ws = 0, hs = 0;
            for (int k = 0; k < 6; ++k)
            {
                const float w = std::max (0.0f, std::min (1.0f, (harmDb[(size_t) k] - (harmPeak - 40.0f)) / 40.0f));
                ws += w; hs += w * harmT[(size_t) k];
            }
            if (ws <= 0 || hs / ws < 0.45) bursts = estimateBursts (tgt);
        }
    }

    // Lower is better. Level is normalised away so the optimiser concentrates on timbre.
    float loss (const Patch& pIn, float* gainOut = nullptr, LossBreakdown* bd = nullptr) const
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

        const auto& T = tuning();
        prior *= T.priorScale;
        const float total = T.wSpec[0] * lr[0] + T.wSpec[1] * lr[1] + T.wSpec[2] * lr[2]
                          + T.wEnv * lenv + T.wFlux * lflux + T.wCent * lcent + T.wLow * llow + T.wHarm * lharm + prior;
        if (bd != nullptr)
        {
            for (int r = 0; r < kRes; ++r) bd->spec[r] = lr[r];
            bd->env = lenv; bd->flux = lflux; bd->cent = lcent; bd->low = llow; bd->harm = lharm; bd->prior = prior; bd->total = total;
        }
        return total;
    }

    ModEstimate mod;   // modulation found in the target (used to build seeds)
    AmpEnvEstimate ampEnv;
    BurstEstimate bursts;
    std::vector<Partial> partials;

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
                // energy that the target does not have is audible whatever the band's salience: full weight for clear excess
                const float wEff = d > tuning().excessDb ? std::max (wb, tuning().excessW) : wb;
                acc += (double) (wf * wEff) * err; ws += (double) (wf * wb);
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

        // abrupt end (gate closes): the last audible frame is followed by a sudden fall of 20 dB or more
        {
            int lastAudible = -1;
            for (int i = (int) env.size() - 1; i > peakFrame; --i)
                if (env[(size_t) i] > peakDb - 55.0f) { lastAudible = i; break; }
            if (lastAudible > peakFrame + 3 && lastAudible + 3 < (int) env.size()
                && env[(size_t) lastAudible + 2] < env[(size_t) lastAudible] - 20.0f)
                return std::min (std::max (lastAudible * kEnvHop, (int) (numSamples * 0.15f)), (int) (numSamples * 0.95f));
        }
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
// resample, trim, cap the length and normalise; empty when the input is unusable
std::vector<float> prepareTarget (std::vector<float> input, double inputSr, const AnalysisOptions& opt)
{
    const size_t maxIn = (size_t) (inputSr * 12.0);
    if (input.size() > maxIn) input.resize (maxIn);
    auto sig = resample (input, inputSr, kSR);
    input.clear();
    float peak = 0;
    for (float v : sig) peak = std::max (peak, std::abs (v));
    if (sig.size() < 256 || peak < 1.0e-5f) return {};

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

    float pk = 0;
    for (float v : tgt) pk = std::max (pk, std::abs (v));
    for (auto& v : tgt) v *= 0.9f / std::max (pk, 1.0e-6f);

    // drop a dead tail (digital silence / noise floor below -60 dB): it only costs time and dilutes the loss
    {
        size_t last = tgt.size();
        while (last > 1 && std::abs (tgt[last - 1]) < 0.9f * 1.0e-3f) --last;
        const size_t keep = std::min (tgt.size(), std::max ((size_t) kMinLen, last + 2205));
        tgt.resize (keep);
    }
    const int fade = std::min (441, (int) tgt.size());
    for (int i = 0; i < fade; ++i) tgt[tgt.size() - 1 - (size_t) i] *= (float) i / (float) fade;
    return tgt;
}

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
namespace
{
AnalysisResult runAnalysisImpl (std::vector<float> input, double inputSr,
                                const std::function<bool()>& shouldExit,
                                const std::function<void (float)>& progress,
                                const AnalysisOptions& opt, std::vector<float>* finalistsOut)
{
    AnalysisResult result;
    auto exitNow = [&] { return shouldExit && shouldExit(); };
    auto report = [&] (float v) { if (progress) progress (std::min (0.99f, std::max (0.0f, v))); };
    const int maxThreads = std::max (1, opt.maxThreads);

    // ---- 1. prepare the target ------------------------------------------------
    std::vector<float> tgt = prepareTarget (std::move (input), inputSr, opt);
    if (tgt.empty()) return result;

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
        // strongest steady partials (and their sub-octaves) are good root candidates for noisy / inharmonic sounds
        {
            Spec s3;
            computeSpec (tgt, 2, s3);
            const auto pr = findPartials (tgt, s3);
            int taken = 0;
            for (const auto& q : pr)
            {
                if (q.hz < 40.0f || q.hz > 2500.0f || taken >= 3) continue;
                addCand (q.hz);
                if (taken < 2) addCand (q.hz * 0.5f);
                ++taken;
            }
        }
        if (cands.empty()) { addCand (110.0f); addCand (220.0f); addCand (440.0f); addCand (55.0f); }
        if (cands.size() > 16) cands.resize (16);
        // YIN picks the shortest clearly repeating period, so a consistent estimate is rarely too high - but sub-octaves of it
        // (which the candidate list also contains) are a common mistake: they make the patch grow a fundamental the sample lacks
        {
            std::vector<float> e = detectPitchCandidates (tgt, kSR);
            std::sort (e.begin(), e.end());
            if (e.size() >= 3 && (e.back() - e.front()) < 0.06f * e[e.size() / 2])
            {
                const float med = e[e.size() / 2];
                std::vector<float> keep;
                for (float c : cands) if (c >= 0.7f * med) keep.push_back (c);
                if (! keep.empty()) cands = keep;
            }
        }
        if (opt.forcePitchHz > 0.0f) cands.assign (1, opt.forcePitchHz);
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
    int rootNote = rootOf (freq);
    float tune0 = tuneOf (freq, rootNote);
    std::vector<std::pair<float, float>> votes;      // (vote score, frequency)
    {
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
            votes.push_back ({ 0.5f * (l[0] + l[1]) + (f == cands[0] ? 0.0f : 0.02f), f });
            report (0.04f * (float) (&f - &cands[0] + 1) / (float) cands.size());
        }
        std::sort (votes.begin(), votes.end());
    }
    if (finalistsOut != nullptr)
    {
        finalistsOut->push_back (votes[0].second);
        if (votes.size() > 1 && votes[1].first - votes[0].first < 0.30f) finalistsOut->push_back (votes[1].second);
        return result;
    }
    auto setPitch = [&] (float f)
    {
        freq = f;
        rootNote = rootOf (f);
        tune0 = tuneOf (f, rootNote);
        evPtr = std::make_unique<Evaluator> (tgt, noteFreq (rootNote), (double) f);
    };
    setPitch (votes[0].second);

    // ---- 3. search --------------------------------------------------------------
    std::mt19937 rng (opt.seed);
    std::normal_distribution<float> nd (0.0f, 1.0f);
    std::uniform_real_distribution<float> ud (0.0f, 1.0f);
    // longer regions cost proportionally more per evaluation: trim the search a little (never below half)
    const float lenSec = (float) tgt.size() / (float) kSR;
    const float eff = std::max (0.05f, opt.effort) * std::min (1.0f, std::max (0.5f, std::sqrt (1.1f / std::max (lenSec, 0.1f))));

   #ifdef KRAKEN_TUNING_ENV
    auto trace = [] (const char* what, float loss) { std::fprintf (stderr, "  [%s] best loss %.4f\n", what, loss); };
   #else
    auto trace = [] (const char*, float) {};
   #endif
    auto evalMany = [&] (std::vector<Cand>& c)
    {
        parallelFor ((int) c.size(), maxThreads, [&] (int i) { c[(size_t) i].loss = evPtr->loss (c[(size_t) i].p); });
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
    // successive halving: many short searches from different starting points, then only the best continue
    const bool sh = tuning().halving;
    const int nStage1 = sh ? 8 : 3, nStage2 = sh ? 3 : 0;
    const int gens1 = sh ? std::max (3, (int) (8 * eff)) : std::max (4, (int) (30 * eff)), lambda1 = sh ? 28 : 36;
    const int gens2 = std::max (4, (int) (14 * eff)), lambda2 = 36;
    const int finalGens = std::max (6, (int) (70 * eff)), finalLambda = 48;
    constexpr int polishRounds = 4;
    const float totalSteps = (float) (15 + nStage1 * gens1 + nStage2 * gens2 + finalGens + polishRounds + 2);
    float stepsDone = 0.0f;
    auto step = [&] { stepsDone += 1.0f; report (0.05f + 0.93f * stepsDone / totalSteps); };

    std::vector<Cand> pop;
    auto screenSeeds = [&]() -> bool
    {
    // 3a. screen every seed, plus one wild variant of each
    pop.clear();
    for (const auto& a : seeds) { Cand c; c.p = a; c.p.tune = tune0; pop.push_back (c); }
    const size_t nSeeds = pop.size();
    for (size_t i = 0; i < nSeeds; ++i) { Cand c; c.p = mutated (pop[i].p, 0.28f, 0.35f); c.p.tune = tune0; pop.push_back (c); }
    evalMany (pop);
    if (exitNow()) return false;
    std::sort (pop.begin(), pop.end(), byLoss);
    trace ("seeds", pop[0].loss);

    // 3a''. envelope-matched copies of the best seeds, and oscillator-per-partial seeds (cowbells, bells, clean tones ...)
    if (tuning().extraSeeds)
    {
        std::vector<size_t> top;
        for (size_t i = 0; i < pop.size() && top.size() < 12; ++i)
        {
            bool far = true;
            for (size_t t : top) if (distance (pop[i].p, pop[t].p) < 0.06f) { far = false; break; }
            if (far) top.push_back (i);
        }
        std::vector<Cand> vars;
        const auto& ae = evPtr->ampEnv;
        auto applyEnv = [&] (Patch& p)
        {
            p.p[P::AMP_A] = ae.a; p.p[P::AMP_D] = ae.d; p.p[P::AMP_S] = ae.s; p.p[P::AMP_R] = ae.r; p.p[P::AMP_HOLD] = 0.0f; p.p[P::AMP_FALL] = ae.fall;
        };
        if (ae.valid)
            for (size_t t : top) { Cand c; c.p = pop[t].p; applyEnv (c.p); vars.push_back (c); }

        const auto& pr = evPtr->partials;
        const float f0Eff = (float) (440.0 * std::pow (2.0, (double) (rootNote - 69) / 12.0)) * std::exp2 (tune0 / 12.0f);
        auto offsetOf = [&] (float hz) { return 12.0f * std::log2 (hz / f0Eff); };
        std::vector<Partial> usable;
        for (const auto& q : pr)
            if (std::abs (offsetOf (q.hz)) <= 23.4f) usable.push_back (q);
        if (usable.size() >= 1)
        {
            auto setOsc = [] (Patch& p, bool isB, float st, float level, float shape)
            {
                const float r = std::round (st);
                const float coarse = 0.5f + r / 48.0f, fine = 0.5f + (st - r) / 2.0f;
                if (! isB) { p.p[P::A_COARSE] = coarse; p.p[P::A_FINE] = fine; p.p[P::A_LEVEL] = level; p.p[P::A_SHAPE] = shape; p.p[P::A_UNI] = 0.0f; p.p[P::A_PW] = 0.5f; }
                else       { p.p[P::B_COARSE] = coarse; p.p[P::B_FINE] = fine; p.p[P::B_LEVEL] = level; p.p[P::B_SHAPE] = shape; p.p[P::B_RING] = 0.0f; p.p[P::B_FB] = 0.0f; }
            };
            const size_t nu = std::min<size_t> (usable.size(), 4);
            for (float shape : { 0.0f, 0.67f, 1.0f })
                for (size_t i = 0; i < nu; ++i)
                    for (size_t j = i; j < nu; ++j)
                    {
                        Patch p = defaultPatch();
                        p.tune = tune0;
                        p.p[P::F_CUTOFF] = 1.0f; p.p[P::F_RES] = 0.0f; p.p[P::F_ENVAMT] = 0.5f; p.p[P::F_KEYTRK] = 0.0f;
                        p.p[P::SUB_LEVEL] = 0.0f; p.p[P::NOISE_LEVEL] = 0.0f; p.p[P::C_LEVEL] = 0.0f; p.p[P::A_FM] = 0.0f;
                        p.p[P::COMB_MIX] = 0.0f; p.p[P::VOWEL_MIX] = 0.0f; p.p[P::DIST_AMT] = 0.0f; p.p[P::DIST_FOLD] = 0.0f;
                        p.p[P::CHO_MIX] = 0.0f; p.p[P::DLY_MIX] = 0.0f; p.p[P::REV_MIX] = 0.0f; p.p[P::PITCH_AMT] = 0.5f;
                        setOsc (p, false, offsetOf (usable[i].hz), 0.9f, shape);
                        if (j == i) setOsc (p, true, 0.0f, 0.0f, shape);
                        else        setOsc (p, true, offsetOf (usable[j].hz), 0.9f * usable[j].amp / std::max (usable[i].amp, 1.0e-3f), shape);
                        if (ae.valid) applyEnv (p); else { p.p[P::AMP_S] = 0.6f; }
                        Cand c; c.p = p; vars.push_back (c);
                    }
        }
        // noise-based seeds: snares, hats, claps, cymbals (filtered noise, optionally with a tone at the strongest partial)
        {
            auto cutP = [] (float hz) { return std::min (1.0f, std::max (0.0f, std::log (hz / 25.0f) / std::log (720.0f))); };
            struct F { float mode, hz; };
            const F filters[] = { { 1.0f, 1200.0f }, { 1.0f, 2500.0f }, { 1.0f, 4500.0f }, { 1.0f, 7000.0f },
                                  { 0.5f, 1500.0f }, { 0.5f, 3000.0f }, { 0.0f, 6000.0f } };
            for (const auto& fl : filters)
                for (float color : { 0.55f, 1.0f })
                    for (int tone = 0; tone < 2; ++tone)
                    {
                        if (tone == 1 && usable.empty()) continue;
                        Patch p = defaultPatch();
                        p.tune = tune0;
                        p.p[P::A_LEVEL] = 0.0f; p.p[P::B_LEVEL] = 0.0f; p.p[P::C_LEVEL] = 0.0f; p.p[P::SUB_LEVEL] = 0.0f; p.p[P::A_FM] = 0.0f;
                        p.p[P::NOISE_LEVEL] = 0.85f; p.p[P::NOISE_COLOR] = color; p.p[P::NOISE_TONAL] = 0.0f; p.p[P::NOISE_TRANS] = 0.0f;
                        p.p[P::F_MODE] = fl.mode; p.p[P::F_CUTOFF] = cutP (fl.hz); p.p[P::F_RES] = 0.2f; p.p[P::F_SLOPE] = 0.0f;
                        p.p[P::F_ENVAMT] = 0.5f; p.p[P::F_KEYTRK] = 0.0f; p.p[P::COMB_MIX] = 0.0f; p.p[P::VOWEL_MIX] = 0.0f;
                        p.p[P::DIST_AMT] = 0.0f; p.p[P::DIST_FOLD] = 0.0f; p.p[P::CHO_MIX] = 0.0f; p.p[P::DLY_MIX] = 0.0f; p.p[P::REV_MIX] = 0.0f;
                        p.p[P::PITCH_AMT] = 0.5f;
                        if (tone == 1)
                        {
                            const float st = offsetOf (usable[0].hz), r = std::round (st);
                            p.p[P::A_SHAPE] = 0.33f; p.p[P::A_LEVEL] = 0.55f; p.p[P::A_COARSE] = 0.5f + r / 48.0f; p.p[P::A_FINE] = 0.5f + (st - r) / 2.0f;
                        }
                        if (ae.valid) applyEnv (p); else p.p[P::AMP_S] = 0.0f;
                        Cand c; c.p = p; vars.push_back (c);
                        if (evPtr->bursts.valid)      // the same noise seed with the detected clap burst pattern
                        {
                            Cand cb = c;
                            cb.p.p[P::AMP_BURSTS] = ((float) evPtr->bursts.count - 1.0f + 0.5f) / 5.99f;
                            cb.p.p[P::BURST_T] = clampv (std::log (evPtr->bursts.spacingSec / 0.004f) / std::log (10.0f));
                            vars.push_back (cb);
                        }
                    }
        }
        if (! vars.empty())
        {
            evalMany (vars);
            if (exitNow()) return false;
            pop.insert (pop.end(), vars.begin(), vars.end());
            std::sort (pop.begin(), pop.end(), byLoss);
            trace ("envelope / partial seeds", pop[0].loss);
        }
    }

    // 3a'. modulation-aware variants: if the sample wobbles / has vibrato / tremolo, give the best seeds that LFO
    {
        const auto& m = evPtr->mod;
        if (tuning().extraSeeds && (m.filt || m.vib || m.trem))
        {
            const float rateP = clampv (std::log (m.rate / 0.1f) / std::log (300.0f));
            std::vector<size_t> top;
            for (size_t i = 0; i < pop.size() && top.size() < 12; ++i)
            {
                bool far = true;
                for (size_t t : top) if (distance (pop[i].p, pop[t].p) < 0.06f) { far = false; break; }
                if (far) top.push_back (i);
            }
            std::vector<Cand> vars;
            auto make = [&] (const Patch& base, float filt, float vib, float trem, float shape)
            {
                Cand c; c.p = base;
                c.p.p[P::LFO_RATE] = rateP; c.p.p[P::LFO_SHAPE] = shape; c.p.p[P::LFO_FADE] = 0.12f;
                c.p.p[P::LFO_FILT] = clampv (filt); c.p.p[P::LFO_VIB] = clampv (vib); c.p.p[P::LFO_TREM] = clampv (trem);
                vars.push_back (c);
            };
            for (size_t t : top)
            {
                const Patch& b = pop[t].p;
                if (m.filt) for (float mult : { 1.0f, 3.0f, 6.0f }) for (float shape : { 0.0f, 0.33f }) make (b, mult * m.filtOct / 3.0f, 0.0f, 0.0f, shape);
                if (m.vib)  make (b, 0.0f, std::sqrt (m.vibSt / 4.0f), 0.0f, 0.0f);
                if (m.trem) make (b, 0.0f, 0.0f, m.tremDepth, 0.0f);
            }
            evalMany (vars);
            if (exitNow()) return false;
            pop.insert (pop.end(), vars.begin(), vars.end());
            std::sort (pop.begin(), pop.end(), byLoss);
            trace ("modulation variants", pop[0].loss);
        }
    }
    step();

        return true;
    };

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

    if (! screenSeeds()) return result;

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
        trace ("spawn", pop[0].loss);
        step();
    }
    const float poolMedian = pop[pop.size() / 2].loss;
    (void) poolMedian;

    // 3c. islands: independent searches started from the best *different* seeds
    std::vector<Island> islands;
    {
        std::vector<size_t> elites;
        for (size_t i = 0; i < pop.size() && (int) elites.size() < nStage1; ++i)
        {
            bool far = true;
            for (size_t e : elites) if (distance (pop[i].p, pop[e].p) < 0.12f) { far = false; break; }
            if (far) elites.push_back (i);
        }
        for (size_t i = 0; i < pop.size() && (int) elites.size() < nStage1; ++i)
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
    auto byBest = [] (const Island& a, const Island& b) { return a.best.loss < b.best.loss; };
    for (auto& isl : islands)
        if (! runES (isl, gens1, lambda1, 0.0f, sh ? 0.2f : 0.45f)) return result;
    std::sort (islands.begin(), islands.end(), byBest);
    for (auto& isl : islands) trace ("stage 1 island", isl.best.loss);

    // stage 2: the best few get more time
    const size_t keep = std::min<size_t> ((size_t) nStage2, islands.size());
    for (size_t i = 0; i < keep; ++i)
        if (! runES (islands[i], gens2, lambda2, 0.2f, 0.45f)) return result;
    std::sort (islands.begin(), islands.begin() + (long) keep, byBest);
    for (size_t i = 0; i < keep; ++i) trace ("stage 2 island", islands[i].best.loss);

    // 3d. continue from the winner (its archive and mean carry over, the others contribute their archives)
    Island fin = std::move (islands[0]);
    for (size_t i = 1; i < islands.size(); ++i)
        for (auto& c : islands[i].archive) fin.archive.push_back (c);
    for (int d = 0; d < kNumParams; ++d)
    {
        fin.mean[(size_t) d] = fin.best.p.p[(size_t) d];
        fin.sigma[(size_t) d] = std::max (0.05f, fin.sigma[(size_t) d]);
    }
    if (! runES (fin, finalGens, finalLambda, 0.45f, 1.0f)) return result;
    trace ("final ES", fin.best.loss);

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
    trace ("tune", curLoss);
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
            const float cl = evPtr->loss (comb);
            if (cl < curLoss) { cur = comb; curLoss = cl; }
            else if (bestSingle.loss < curLoss) { cur = bestSingle.p; curLoss = bestSingle.loss; }
        }
        trace ("polish", curLoss);
        step();
    }
    tuneSearch();
    trace ("tune2", curLoss);
    step();

    // ---- 4. build result -------------------------------------------------------------
    auto setLevel = [&] (Patch& p)
    {
        float g = 1.0f;
        evPtr->loss (p, &g);
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

} // namespace

// Two pitch hypotheses are common (a cowbell is 187 Hz or 560 Hz, a snare 74 or 148 Hz ...) and the cheap scores cannot
// tell them apart reliably, so when the vote is close the best two each get a full search and the lower final loss wins.
AnalysisResult runAnalysis (std::vector<float> input, double inputSr,
                            const std::function<bool()>& shouldExit,
                            const std::function<void (float)>& progress,
                            const AnalysisOptions& opt)
{
    auto exitNow = [&] { return shouldExit && shouldExit(); };
    auto report = [&] (float v) { if (progress) progress (std::min (0.99f, std::max (0.0f, v))); };
    if (opt.forcePitchHz > 0.0f) return runAnalysisImpl (std::move (input), inputSr, shouldExit, progress, opt, nullptr);

    std::vector<float> finalists;
    auto pre = runAnalysisImpl (input, inputSr, shouldExit, [&] (float p) { report (0.08f * p); }, opt, &finalists);
    if (finalists.empty()) return pre;

    AnalysisResult best;
    bool have = false;
    for (size_t i = 0; i < finalists.size(); ++i)
    {
        if (exitNow()) return {};
        AnalysisOptions o = opt;
        o.forcePitchHz = finalists[i];
        if (finalists.size() > 1) o.effort = opt.effort * 0.7f;
        const float n = (float) finalists.size();
        auto r = runAnalysisImpl (input, inputSr, shouldExit, [&, i, n] (float p) { report (0.08f + 0.92f * ((float) i + p) / n); }, o, nullptr);
        if (r.ok && (! have || r.loss < best.loss)) { best = std::move (r); have = true; }
    }
    report (1.0f);
    return best;
}

} // namespace kraken
