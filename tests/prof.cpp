#include "../Source/AnalyzerCore.cpp"
#include <chrono>
#include <cstdio>
using namespace kraken;
int main(){
  FILE* f=fopen("/tmp/targets/bell_c5.raw","rb"); std::vector<float> x; float v; while(fread(&v,4,1,f)==1) x.push_back(v); fclose(f);
  AnalysisOptions o; auto tgt=prepareTarget(x,44100.0,o); Evaluator ev(tgt,493.88,491.6);
  auto seeds=allSeeds(); auto T=[&](auto fn,int reps){ auto t0=std::chrono::steady_clock::now(); for(int i=0;i<reps;++i) fn(i); return std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-t0).count()/reps; };
  std::vector<float> out; 
  double tr=T([&](int i){ Patch p=seeds[i%seeds.size()]; renderOffline(p,kSR,493.88,(int)tgt.size(),(int)(tgt.size()*.6),out); },200);
  double tf=T([&](int){ Feat F; computeFeat(out,F); },200);
  double tl=T([&](int i){ Patch p=seeds[i%seeds.size()]; ev.loss(p); },200);
  std::vector<float> o2=out; double t0=T([&](int){ Spec s; computeSpec(o2,0,s);},200), t1=T([&](int){ Spec s; computeSpec(o2,1,s);},200), t2=T([&](int){ Spec s; computeSpec(o2,2,s);},200);
  printf("render %.2f ms | computeFeat %.2f ms (R1 %.2f R2 %.2f R3 %.2f) | full loss %.2f ms\n",tr,tf,t0,t1,t2,tl);
}
