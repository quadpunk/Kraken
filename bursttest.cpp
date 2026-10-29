#include "../Source/AnalyzerCore.cpp"
#include <cstring>
using namespace kraken;
int main(int argc,char**argv){ for(int a=1;a<argc;++a){ FILE* f=fopen(argv[a],"rb"); std::vector<float> x; float v; while(fread(&v,4,1,f)==1) x.push_back(v); fclose(f);
 AnalysisOptions o; auto t=prepareTarget(x,44100.0,o); auto b=estimateBursts(t); printf("%-24s bursts valid %d count %d spacing %.1f ms\n", strrchr(argv[a],'/')+1, b.valid, b.count, b.spacingSec*1000); } }
