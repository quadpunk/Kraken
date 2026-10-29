// SPDX-License-Identifier: MIT
// Quick sanity checks for engine changes: oversampler response, latency, aliasing, tune.
#include "../Source/SynthEngine.h"
#include <cstdio>
using namespace kraken;
int main()
{
    // 1. oversampler: DC gain, latency, passband flatness
    Oversampler2x os;
    std::vector<float> in(4096), out(4096);
    for (int i = 0; i < 4096; ++i) in[i] = std::sin(2*3.14159265*0.05*i);   // 0.05 cycles/sample
    for (int i = 0; i < 4096; ++i) { float e,o; os.up(in[i], e, o); out[i] = os.down(e,o); }
    // find best alignment lag
    int bestLag=0; double bestErr=1e9;
    for (int lag=0; lag<20; ++lag){ double err=0; for(int i=200;i<4000;++i){ double d=out[i]-in[i-lag]; err+=d*d;} if(err<bestErr){bestErr=err;bestLag=lag;} }
    printf("oversampler passthrough: best lag %d (expect %d), rms err %.5f\n", bestLag, kFxLatency, std::sqrt(bestErr/3800));
    // high-freq (0.4 of fs) should be attenuated less than 3dB? passband to ~0.4*fs/ ... test 0.2 and 0.4
    for (double f : {0.1, 0.2, 0.3, 0.4}) {
        os.reset(); double e1=0,e0=0;
        for (int i=0;i<4096;++i){ float x=std::sin(2*3.14159265*f*i); float e,o; os.up(x,e,o); float y=os.down(e,o); if(i>200){e1+=y*y;e0+=x*x;} }
        printf("  f=%.2f fs gain %.2f dB\n", f, 10*std::log10(e1/e0));
    }
    // 2. aliasing: high-freq sine through heavy distortion, with and without oversampling is hard to compare directly;
    // measure energy at a non-harmonic bin for fold on a 5 kHz sine
    {
        Patch p = defaultPatch(); p.p[P::DIST_AMT]=1.f; p.p[P::DIST_FOLD]=0.f;
        std::vector<float> o; p.p[P::A_SHAPE]=0.f; p.p[P::AMP_S]=1.f;p.p[P::F_CUTOFF]=1.f;
        renderOffline(p,44100.0,3000.0f,44100,40000,o);
        double tot=0; for(float v:o) tot+=v*v;
        printf("dist render rms %.4f finite=%d\n", std::sqrt(tot/o.size()), std::isfinite(tot));
    }
    // 3. tune: patch.tune shifts frequency
    {
        Patch p = defaultPatch(); p.p[P::A_SHAPE]=0.f; p.p[P::AMP_S]=1.f; p.p[P::F_CUTOFF]=1.f; p.p[P::AMP_A]=0.f;
        for (float tune : {0.f, 0.5f}) {
            p.tune = tune; std::vector<float> o; renderOffline(p,44100.0,440.0f,44100,40000,o);
            int zc=0; for(int i=2000;i<42000;++i) if(o[i-1]<0&&o[i]>=0) zc++;
            printf("tune %.2f st -> %.2f Hz (expect %.2f)\n", tune, zc/ (40000/44100.0), 440*std::pow(2.0,tune/12.0));
        }
    }
    // 4. new params default = transparent
    { Patch p=defaultPatch(); printf("tone default %.1f low default %.1f\n", p.p[P::OUT_TONE], p.p[P::OUT_LOW]); }
}
