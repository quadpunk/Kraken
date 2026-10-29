// SPDX-License-Identifier: MIT
#include "../Source/AnalyzerCore.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
using namespace kraken;
int main(int argc, char** argv)
{
    if (argc < 4) { printf("usage: run_new in.raw out.txt effort [sr]\n"); return 1; }
    FILE* f = fopen(argv[1], "rb"); std::vector<float> x; float v; while (fread(&v,4,1,f)==1) x.push_back(v); fclose(f);
    AnalysisOptions o; o.effort = (float) atof(argv[3]); o.maxThreads = 8; if (getenv("KT_SEED")) o.seed = (unsigned) atoi(getenv("KT_SEED"));
    double sr = argc > 4 ? atof(argv[4]) : 44100.0;
    auto t0 = std::chrono::steady_clock::now();
    auto r = runAnalysis(x, sr, [] { return false; }, [](float){}, o);
    double sec = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    FILE* g = fopen(argv[2], "w");
    fprintf(g, "%d %.6f %.6f %d %.5f %.3f\n", r.midiNote, r.best.tune, r.best.level, r.ok, r.loss, r.match);
    for (float p : r.best.p) fprintf(g, "%.6f ", p); fprintf(g, "\n"); fclose(g);
    printf("%s: ok=%d note=%d freq=%.2f tune=%.3f loss=%.3f match=%.0f%% time=%.1fs\n", argv[1], r.ok, r.midiNote, r.freq, r.best.tune, r.loss, r.match, sec);
}
