// SPDX-License-Identifier: MIT
// Diagnostic: which seeds does the loss prefer for a target, and how do the loss terms of a result look?
// usage: probe target.raw [result.txt] [topN]
#include "../Source/AnalyzerCore.cpp"
#include <cstdio>
using namespace kraken;
static void line(const char* n, const LossBreakdown& b) {
    printf("%-22s tot %.3f | spec %.2f %.2f %.2f | env %.2f flux %.2f cent %.2f low %.2f harm %.2f prior %.3f\n", n, b.total, b.spec[0], b.spec[1], b.spec[2], b.env, b.flux, b.cent, b.low, b.harm, b.prior); }
int main(int argc, char** argv) {
    FILE* f = fopen(argv[1], "rb"); std::vector<float> x; float v; while (fread(&v,4,1,f)==1) x.push_back(v); fclose(f);
    AnalysisOptions o; auto tgt = prepareTarget(x, 44100.0, o);
    auto est = detectPitchCandidates(tgt, kSR); printf("pitch estimates:"); for (float e : est) printf(" %.1f", e); printf("\n");
    int note = 60; float tune = 0; Patch res; bool haveRes = false;
    if (argc > 2) { FILE* g = fopen(argv[2], "r"); int ok; float level, loss, match; if (fscanf(g, "%d %f %f %d %f %f", &note, &tune, &level, &ok, &loss, &match) == 6) { for (int i = 0; i < kNumParams; ++i) if (fscanf(g, "%f", &res.p[i]) != 1) break; res.tune = tune; haveRes = true; } fclose(g); }
    else { float m = est.empty() ? 110.f : est[est.size()/2]; note = (int)std::lround(69 + 12*std::log2(m/440.0)); tune = (float)(12*std::log2(m/(440*std::pow(2.0,(note-69)/12.0)))); }
    const double rootF = 440.0*std::pow(2.0,(note-69)/12.0), f0 = rootF*std::pow(2.0, tune/12.0);
    Evaluator ev(tgt, rootF, f0);
    printf("root %d tune %.3f (f0 %.2f)\n", note, tune, f0);
    std::vector<Patch> seeds = archetypes(); std::vector<std::string> names; for (size_t i = 0; i < seeds.size(); ++i) names.push_back("archetype " + std::to_string(i));
    for (const auto& d : factoryDefs()) { seeds.push_back(factoryPatch(d)); names.push_back(d.name); }
    struct R { float t; size_t i; LossBreakdown b; }; std::vector<R> rs;
    for (size_t i = 0; i < seeds.size(); ++i) { Patch p = seeds[i]; p.tune = tune; LossBreakdown b; ev.loss(p, nullptr, &b); rs.push_back({b.total, i, b}); }
    std::sort(rs.begin(), rs.end(), [](const R& a, const R& b){ return a.t < b.t; });
    int top = argc > 3 ? atoi(argv[3]) : 8; printf("--- best seeds ---\n");
    for (int k = 0; k < top && k < (int)rs.size(); ++k) line(names[rs[k].i].c_str(), rs[k].b);
    if (haveRes) { printf("--- result ---\n"); LossBreakdown b; ev.loss(res, nullptr, &b); line("RESULT", b);
        { std::vector<float> c; Patch p = res; p.level = 0.5f; renderOffline(p, kSR, rootF, (int)tgt.size(), (int)(0.6*tgt.size()), c);
          double e=0, te=0; for (float q : c) e += q*q; for (float q : tgt) te += q*q; float g = (float)std::sqrt(te/std::max(e,1e-12)); for (auto& q : c) q *= g;
          Feat tf, cf; computeFeat(tgt, tf); computeFeat(c, cf); const auto& T = tf.spec[2]; const auto& C = cf.spec[2];
          printf("band-group mean dB (8192 FFT, loud frames)   centre Hz: target / patch / diff\n");
          for (int gI = 0; gI < kBands; gI += 4) { double a=0,b=0; int n=0; for (int fr=0; fr<T.frames; ++fr) for (int k=gI;k<gI+4;++k){ a+=std::max(T.db[fr*kBands+k], T.peak-70); b+=std::max(C.db[fr*kBands+k], T.peak-70); ++n; }
            printf("  %7.0f Hz  %6.1f %6.1f  %+6.1f\n", bandCentres()[gI+2], a/n, b/n, b/n-a/n); } }
        printf("params:"); for (int i = 0; i < kNumParams; ++i) if (res.p[i] > 0.001f && std::abs(res.p[i]-defaultPatch().p[i]) > 0.02f) printf(" %s=%.2f", paramShort(i), res.p[i]); printf("\n"); }
}
