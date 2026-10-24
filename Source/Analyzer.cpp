#include "Analyzer.h"

#include <random>
#include <thread>

namespace kraken
{
namespace
{
constexpr double kSR = 22050.0;
constexpr int kMaxLen = 26460; // 1.2 s
constexpr int kMinLen = 4096;

//==============================================================================
std::vector<float> resample (const std::vector<float>& in, double srcSr, double dstSr)
{
    if (srcSr == dstSr || in.empty()) return in;
    const double ratio = srcSr / dstSr;
    const int outN = (int) std::floor ((double) in.size() / ratio);
    std::vector<float> out ((size_t) std::max (outN, 0));
    const int inN = (int) in.size();
    for (int j = 0; j < outN; ++j)
    {
        const double a = j * ratio, b = (j + 1) * ratio;
        if (ratio > 1.0)
        {
            const int i0 = (int) a, i1 = std::min ((int) std::ceil (b), inN);
            double sum = 0; int cnt = 0;
            for (int i = i0; i < i1; ++i) { sum += in[(size_t) i]; ++cnt; }
            out[(size_t) j] = cnt ? (float) (sum / cnt) : 0.0f;
        }
        else
        {
            const int i0 = (int) a, i1 = std::min (i0 + 1, inN - 1);
            const float fr = (float) (a - i0);
            out[(size_t) j] = in[(size_t) i0] * (1.0f - fr) + in[(size_t) i1] * fr;
        }
    }
    return out;
}

//==============================================================================
float detectPitch (const std::vector<float>& x, double sr)
{
    const int W = 1536;
    const int minTau = std::max (1, (int) (sr / 2000.0));
    const int maxTau = (int) (sr / 50.0);
    const int n = (int) x.size();
    if (n < W + maxTau + 2) return 0.0f;

    std::vector<float> est;
    std::vector<float> d ((size_t) maxTau + 1), cm ((size_t) maxTau + 1);

    for (float frac : { 0.12f, 0.25f, 0.40f })
    {
        const int start = std::min ((int) (frac * (float) n), n - W - maxTau - 1);
        for (int tau = 1; tau <= maxTau; ++tau)
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
        for (int tau = 1; tau <= maxTau; ++tau)
        {
            run += d[(size_t) tau];
            cm[(size_t) tau] = run > 0 ? (float) (d[(size_t) tau] * tau / run) : 1.0f;
        }
        int best = -1;
        for (int tau = minTau; tau <= maxTau; ++tau)
            if (cm[(size_t) tau] < 0.15f)
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
        if (best > 0) est.push_back ((float) (sr / best));
    }
    if (est.size() < 2) return 0.0f;
    std::sort (est.begin(), est.end());
    return est[est.size() / 2];
}

//==============================================================================
std::vector<float> makeHann (int n)
{
    std::vector<float> w ((size_t) n);
    for (int i = 0; i < n; ++i) w[(size_t) i] = 0.5f - 0.5f * std::cos (kTwoPi * (float) i / (float) n);
    return w;
}

struct FftSet
{
    juce::dsp::FFT f512 { 9 };
    juce::dsp::FFT f2048 { 11 };
    std::vector<float> buf = std::vector<float> (4096, 0.0f);
};

void logSpectrogram (const std::vector<float>& x, int fftSize, std::vector<float>& out)
{
    thread_local FftSet fft;
    static const std::vector<float> w512 = makeHann (512);
    static const std::vector<float> w2048 = makeHann (2048);

    const bool is512 = (fftSize == 512);
    const auto& win = is512 ? w512 : w2048;
    auto& f = is512 ? fft.f512 : fft.f2048;

    const int n = (int) x.size();
    const int hop = fftSize / 2;
    const int bins = (int) ((float) fftSize * 0.44f);
    const int frames = std::max (1, (n - fftSize) / hop + 1);
    out.assign ((size_t) frames * (size_t) bins, 0.0f);

    for (int fr = 0; fr < frames; ++fr)
    {
        std::fill (fft.buf.begin(), fft.buf.end(), 0.0f);
        const int s = fr * hop;
        for (int i = 0; i < fftSize; ++i)
        {
            const int idx = s + i;
            fft.buf[(size_t) i] = idx < n ? x[(size_t) idx] * win[(size_t) i] : 0.0f;
        }
        f.performRealOnlyForwardTransform (fft.buf.data(), true);
        for (int k = 1; k <= bins; ++k)
        {
            const float re = fft.buf[(size_t) (2 * k)], im = fft.buf[(size_t) (2 * k + 1)];
            const float m = std::sqrt (re * re + im * im) * 4.0f / (float) fftSize;
            out[(size_t) fr * (size_t) bins + (size_t) (k - 1)] = 20.0f * std::log10 (m + 3.0e-5f);
        }
    }
}

void envelopeDb (const std::vector<float>& x, std::vector<float>& out)
{
    const int hop = 512, n = (int) x.size();
    const int frames = std::max (1, n / hop);
    out.resize ((size_t) frames);
    for (int f = 0; f < frames; ++f)
    {
        double e = 0; int cnt = 0;
        for (int i = f * hop; i < std::min (n, (f + 1) * hop); ++i) { e += (double) x[(size_t) i] * x[(size_t) i]; ++cnt; }
        const float rms = (float) std::sqrt (e / std::max (cnt, 1));
        out[(size_t) f] = std::max (-80.0f, 20.0f * std::log10 (rms + 1.0e-6f));
    }
}

float meanAbsDiff (const std::vector<float>& a, const std::vector<float>& b)
{
    const size_t n = std::min (a.size(), b.size());
    if (n == 0) return 1000.0f;
    double s = 0;
    for (size_t i = 0; i < n; ++i) s += std::abs (a[i] - b[i]);
    return (float) (s / (double) n);
}

//==============================================================================
class Evaluator
{
public:
    Evaluator (std::vector<float> target, float f) : tgt (std::move (target)), freq (f)
    {
        N = (int) tgt.size();
        hold = (int) ((float) N * 0.6f);
        double e = 0;
        for (float v : tgt) e += (double) v * v;
        tgtRms = (float) std::sqrt (e / std::max (N, 1));
        logSpectrogram (tgt, 512, t512);
        logSpectrogram (tgt, 2048, t2048);
        envelopeDb (tgt, tEnv);
    }

    // lower is better. gainOut = linear gain that matches candidate RMS to target RMS.
    float loss (const Patch& pIn, float* gainOut = nullptr) const
    {
        thread_local std::vector<float> cand, s5, s20, env;
        Patch p = pIn;
        p.level = 0.70710678f; // gain 1.0
        renderOffline (p, kSR, freq, N, hold, cand);

        double e = 0;
        for (float v : cand) e += (double) v * v;
        const float rms = (float) std::sqrt (e / std::max (N, 1));
        if (gainOut) *gainOut = 1.0f;
        if (! (rms > 1.0e-5f) || ! std::isfinite (rms)) return 1000.0f;

        const float g = tgtRms / rms;
        if (gainOut) *gainOut = g;
        for (auto& v : cand) v *= g;

        logSpectrogram (cand, 512, s5);
        logSpectrogram (cand, 2048, s20);
        envelopeDb (cand, env);

        const float l1 = meanAbsDiff (t512, s5) / 30.0f;
        const float l2 = meanAbsDiff (t2048, s20) / 30.0f;
        const float le = meanAbsDiff (tEnv, env) / 20.0f;
        return l1 + l2 + 0.5f * le;
    }

    static constexpr float kMaxLoss = 2.5f; // sum of weights

private:
    std::vector<float> tgt;
    float freq = 220.0f, tgtRms = 0.1f;
    int N = 0, hold = 0;
    std::vector<float> t512, t2048, tEnv;
};

//==============================================================================
void parallelFor (int n, const std::function<void (int)>& fn)
{
    const int nt = std::min (std::max ((int) std::thread::hardware_concurrency(), 1), 8);
    std::atomic<int> next { 0 };
    auto worker = [&]
    {
        for (;;)
        {
            const int i = next.fetch_add (1);
            if (i >= n) break;
            fn (i);
        }
    };
    std::vector<std::thread> th;
    for (int t = 1; t < nt; ++t) th.emplace_back (worker);
    worker();
    for (auto& t : th) t.join();
}

struct Cand { Patch p; float loss = 1000.0f; };

float distance (const Patch& a, const Patch& b)
{
    float s = 0;
    for (int i = 0; i < kNumParams; ++i) { const float d = a.p[(size_t) i] - b.p[(size_t) i]; s += d * d; }
    return std::sqrt (s);
}

} // namespace

//==============================================================================
void Analyzer::analyse (std::vector<float> mono, double sampleRate)
{
    stopThread (10000);
    input = std::move (mono);
    inputSr = sampleRate;
    startThread();
}

void Analyzer::run()
{
    AnalysisResult result;

    auto finish = [&]
    {
        if (! threadShouldExit() && onFinished) onFinished (result);
    };

    // ---- 1. prepare the target ------------------------------------------------
    auto sig = resample (input, inputSr, kSR);
    float peak = 0;
    for (float v : sig) peak = std::max (peak, std::abs (v));
    if (sig.size() < 1024 || peak < 1.0e-5f) { finish(); return; }

    size_t start = 0;
    while (start < sig.size() && std::abs (sig[start]) < 0.02f * peak) ++start;
    start = start > 64 ? start - 64 : 0;
    const size_t len = std::min (sig.size() - start, (size_t) kMaxLen);
    std::vector<float> tgt (sig.begin() + (long) start, sig.begin() + (long) (start + len));
    if ((int) tgt.size() < kMinLen) tgt.resize ((size_t) kMinLen, 0.0f);

    for (auto& v : tgt) v *= 0.9f / peak;
    const int fade = std::min (220, (int) tgt.size());
    for (int i = 0; i < fade; ++i) tgt[tgt.size() - 1 - (size_t) i] *= (float) i / (float) fade;

    float freq = detectPitch (tgt, kSR);
    if (freq <= 0.0f) freq = 220.0f;
    freq = std::min (std::max (freq, 30.0f), 4000.0f);

    Evaluator ev (tgt, freq);

    // ---- 2. evolutionary search ------------------------------------------------
    std::mt19937 rng (0xBEEF);
    std::normal_distribution<float> nd (0.0f, 1.0f);
    std::uniform_real_distribution<float> ud (0.0f, 1.0f);

    auto evalMany = [&] (std::vector<Cand>& c)
    {
        parallelFor ((int) c.size(), [&] (int i) { c[(size_t) i].loss = ev.loss (c[(size_t) i].p); });
    };
    auto byLoss = [] (const Cand& a, const Cand& b) { return a.loss < b.loss; };

    constexpr int mu = 8, lambda = 32, generations = 90, initN = 400;

    std::vector<Cand> pop ((size_t) initN);
    for (size_t i = 0; i < pop.size(); ++i)
    {
        pop[i].p = defaultPatch();
        if (i > 0) for (auto& v : pop[i].p.p) v = ud (rng);
    }
    evalMany (pop);
    if (threadShouldExit()) return;
    std::sort (pop.begin(), pop.end(), byLoss);

    std::vector<float> w ((size_t) mu);
    float wsum = 0;
    for (int i = 0; i < mu; ++i) { w[(size_t) i] = std::log ((float) mu + 0.5f) - std::log ((float) i + 1.0f); wsum += w[(size_t) i]; }
    for (auto& v : w) v /= wsum;

    std::array<float, kNumParams> mean {}, sigma {};
    for (int d = 0; d < kNumParams; ++d)
    {
        float m = 0;
        for (int i = 0; i < mu; ++i) m += w[(size_t) i] * pop[(size_t) i].p.p[(size_t) d];
        mean[(size_t) d] = m;
        float var = 0;
        for (int i = 0; i < 24; ++i) { const float df = pop[(size_t) i].p.p[(size_t) d] - m; var += df * df / 24.0f; }
        sigma[(size_t) d] = std::max (0.10f, std::sqrt (var));
    }

    Cand best = pop[0];
    std::vector<Cand> archive (pop.begin(), pop.begin() + 12);

    for (int g = 0; g < generations; ++g)
    {
        if (threadShouldExit()) return;
        std::vector<Cand> off ((size_t) lambda);
        for (auto& c : off)
            for (int d = 0; d < kNumParams; ++d)
                c.p.p[(size_t) d] = std::min (1.0f, std::max (0.0f, mean[(size_t) d] + sigma[(size_t) d] * nd (rng)));
        evalMany (off);
        std::sort (off.begin(), off.end(), byLoss);
        if (off[0].loss < best.loss) best = off[0];
        for (int k = 0; k < 3; ++k) archive.push_back (off[(size_t) k]);

        for (int d = 0; d < kNumParams; ++d)
        {
            float m = 0;
            for (int i = 0; i < mu; ++i) m += w[(size_t) i] * off[(size_t) i].p.p[(size_t) d];
            float var = 0;
            for (int i = 0; i < mu; ++i) { const float df = off[(size_t) i].p.p[(size_t) d] - m; var += w[(size_t) i] * df * df; }
            mean[(size_t) d] = m;
            sigma[(size_t) d] = std::max (0.012f, 0.6f * sigma[(size_t) d] + 0.4f * 1.3f * std::sqrt (var));
        }
        if (onProgress) onProgress ((float) (g + 1) / (float) generations);
    }

    // ---- 3. build result -----------------------------------------------------
    auto setLevel = [&] (Patch& p)
    {
        float g = 1.0f;
        ev.loss (p, &g);
        p.level = std::sqrt (std::min (std::max (g / 2.0f, 0.02f), 1.0f));
    };

    Patch bestPatch = best.p;
    setLevel (bestPatch);
    result.best = bestPatch;
    result.kin.push_back (bestPatch);

    std::sort (archive.begin(), archive.end(), byLoss);
    std::vector<Patch> chosen { best.p };
    for (const auto& c : archive)
    {
        if (chosen.size() >= 4) break;
        bool far = true;
        for (const auto& o : chosen) if (distance (c.p, o) < 0.45f) { far = false; break; }
        if (far) { chosen.push_back (c.p); Patch q = c.p; setLevel (q); result.kin.push_back (q); }
    }
    while (result.kin.size() < 6)
    {
        Patch m = best.p;
        for (auto& v : m.p) v = std::min (1.0f, std::max (0.0f, v + 0.15f * nd (rng)));
        setLevel (m);
        result.kin.push_back (m);
    }

    const float lossNorm = best.loss / Evaluator::kMaxLoss;
    result.match = 100.0f * std::min (1.0f, std::max (0.0f, 1.0f - 3.0f * lossNorm));
    result.freq = freq;
    result.midiNote = (int) std::lround (69.0 + 12.0 * std::log2 ((double) freq / 440.0));
    result.ok = true;
    if (onProgress) onProgress (1.0f);
    finish();
}

} // namespace kraken
