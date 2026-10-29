#include "../Source/AnalyzerCore.cpp"
using namespace kraken;
int main(int, char** argv){ FILE* f=fopen(argv[1],"rb"); std::vector<float> x; float v; while(fread(&v,4,1,f)==1) x.push_back(v); fclose(f);
 AnalysisOptions o; auto tgt=prepareTarget(x,44100.0,o); Spec s3; computeSpec(tgt,2,s3); auto pr=findPartials(tgt,s3);
 printf("yin:"); for(float e:detectPitchCandidates(tgt,kSR)) printf(" %.1f",e); printf("\npartials:"); for(auto&q:pr) printf(" %.0f(%.2f)",q.hz,q.amp); printf("\n"); }
