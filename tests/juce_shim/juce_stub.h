// Minimal stand-in for the few JUCE symbols the ORIGINAL Analyzer.cpp needs, so the old analyzer can be
// compiled unchanged in a plain g++ build and compared against the new one.
#pragma once
#include <cmath>
#include <complex>
#include <vector>
#include <cstring>
namespace juce {
template <typename T> struct MathConstants { static constexpr T pi = (T) 3.14159265358979323846; };
struct ScopedNoDenormals {};
class Thread {
public:
    explicit Thread (const char*) {}
    virtual ~Thread() {}
    void startThread() { run(); }
    bool stopThread (int) { return true; }
    bool threadShouldExit() const { return false; }
    virtual void run() = 0;
};
namespace dsp {
class FFT {
public:
    explicit FFT (int order) : n (1 << order) {}
    void performRealOnlyForwardTransform (float* d, bool = false) const {
        std::vector<std::complex<double>> a ((size_t) n);
        for (int i = 0; i < n; ++i) a[(size_t) i] = d[i];
        fft (a);
        for (int k = 0; k <= n / 2; ++k) { d[2*k] = (float) a[(size_t) k].real(); d[2*k+1] = (float) a[(size_t) k].imag(); }
    }
private:
    static void fft (std::vector<std::complex<double>>& a) {
        const size_t N = a.size();
        for (size_t i = 1, j = 0; i < N; ++i) { size_t bit = N >> 1; for (; j & bit; bit >>= 1) j ^= bit; j ^= bit; if (i < j) std::swap (a[i], a[j]); }
        for (size_t len = 2; len <= N; len <<= 1) {
            const double ang = -2 * 3.14159265358979323846 / (double) len;
            std::complex<double> wl (std::cos (ang), std::sin (ang));
            for (size_t i = 0; i < N; i += len) { std::complex<double> w (1); for (size_t k = 0; k < len / 2; ++k) {
                auto u = a[i+k], v = a[i+k+len/2] * w; a[i+k] = u + v; a[i+k+len/2] = u - v; w *= wl; } }
        }
    }
    int n;
};
}
}
