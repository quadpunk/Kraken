// SPDX-License-Identifier: MIT
// Prints the modulation estimate (filter LFO / vibrato / tremolo) for target files.
#include "../Source/AnalyzerCore.cpp"
#include <cstdio>
#include <cstring>
using namespace kraken;
int main(int argc, char** argv) {
    for (int a = 1; a < argc; ++a) {
        FILE* f = fopen(argv[a], "rb"); std::vector<float> x; float v; while (fread(&v,4,1,f)==1) x.push_back(v); fclose(f);
        AnalysisOptions o; auto tgt = prepareTarget(x, 44100.0, o); auto est = detectPitchCandidates(tgt, kSR);
        float m = est.empty() ? 110.f : est[est.size()/2]; int note = (int)std::lround(69 + 12*std::log2(m/440.0));
        Evaluator ev(tgt, 440.0*std::pow(2.0,(note-69)/12.0), m); const auto& e = ev.mod;
        printf("%-22s f0 %7.1f | filt %d %.2f oct (%.2f) | vib %d %.2f st (%.2f) | trem %d %.2f (%.2f) | rate %.2f Hz\n", strrchr(argv[a],'/')+1, m, e.filt, e.filtOct, e.filtScore, e.vib, e.vibSt, e.vibScore, e.trem, e.tremDepth, e.tremScore, e.rate);
    }
}
