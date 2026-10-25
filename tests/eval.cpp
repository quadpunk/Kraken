// Independent quality metrics (different from the analyzer's loss): target vs. rendered patch.
// usage: eval target.raw patch.txt [old]
#include "../Source/SynthEngine.h"
#include <complex>
#include <cstdio>
#include <cstdlib>
using namespace kraken;
typedef std::complex<double> cd;
static void fft(std::vector<cd>& a){ size_t N=a.size(); for(size_t i=1,j=0;i<N;++i){size_t bit=N>>1; for(;j&bit;bit>>=1) j^=bit; j^=bit; if(i<j) std::swap(a[i],a[j]);}
  for(size_t len=2;len<=N;len<<=1){ double ang=-2*M_PI/len; cd wl(cos(ang),sin(ang)); for(size_t i=0;i<N;i+=len){ cd w(1); for(size_t k=0;k<len/2;++k){ cd u=a[i+k],v=a[i+k+len/2]*w; a[i+k]=u+v;a[i+k+len/2]=u-v;w*=wl;}}}}
struct M { double lsd=0, centOct=0, hfDb=0, lowDb=0, t30=0, flat=0; };
// power spectra frames (2048 / hop 512), returns frames x 1025 power
static std::vector<std::vector<double>> stft(const std::vector<float>& x){
  const int N=2048,H=512; std::vector<std::vector<double>> out;
  for(int s=0; s+N<=(int)x.size(); s+=H){ std::vector<cd> a(N); for(int i=0;i<N;++i) a[i]=x[s+i]*(0.5-0.5*cos(2*M_PI*i/N)); fft(a); std::vector<double> p(N/2+1); for(int k=0;k<=N/2;++k) p[k]=std::norm(a[k])/ (N*N/4.0)+1e-14; out.push_back(p);} return out; }
static M metrics(const std::vector<float>& t, const std::vector<float>& c){
  auto T=stft(t), C=stft(c); size_t F=std::min(T.size(),C.size()); M m; const double sr=44100.0;
  double peakT=-1e9; for(size_t f=0;f<F;++f) for(double v:T[f]) peakT=std::max(peakT,10*log10(v));
  double floorDb=peakT-70; double wsum=0;
  // mel-ish bands: 40 bands 30Hz..16k
  const int B=40; auto edge=[&](int j){ return 30*pow(16000.0/30,(double)j/B); };
  double eT=0,eC=0,hfT=0,hfC=0,loT=0,loC=0,centAcc=0;
  for(size_t f=0;f<F;++f){
    double ft=0; for(double v:T[f]) ft+=v; double w=std::max(0.0,10*log10(ft)-(peakT-40))/40; if(w<=0.02) w=0.02;
    double bt[B],bc[B]; double sT=0,sC=0,cT=0,cC=0;
    for(int b=0;b<B;++b){ int lo=std::max(1,(int)(edge(b)/(sr/2048))),hi=std::max(lo+1,(int)(edge(b+1)/(sr/2048))); double a=0,d=0; for(int k=lo;k<hi&&k<1025;++k){a+=T[f][k];d+=C[f][k];} a/=(hi-lo); d/=(hi-lo);
      bt[b]=std::max(10*log10(a),floorDb); bc[b]=std::max(10*log10(d),floorDb); double fc=sqrt(edge(b)*edge(b+1)); double pa=pow(10,bt[b]/10),pd=pow(10,bc[b]/10); sT+=pa;sC+=pd;cT+=pa*log2(fc);cC+=pd*log2(fc);
      m.lsd+=w*pow(bt[b]-bc[b],2)/B; }
    wsum+=w; centAcc+=w*fabs(cT/sT-cC/sC);
    for(int k=1;k<1025;++k){ double fr=k*sr/2048; if(fr>4000){hfT+=T[f][k]*w;hfC+=C[f][k]*w;} if(fr<150){loT+=T[f][k]*w;loC+=C[f][k]*w;} eT+=T[f][k]*w; eC+=C[f][k]*w; }
  }
  m.lsd=sqrt(m.lsd/wsum); m.centOct=centAcc/wsum; m.hfDb=10*log10((hfC/eC+1e-9)/(hfT/eT+1e-9)); m.lowDb=10*log10((loC/eC+1e-9)/(loT/eT+1e-9));
  auto t30=[&](const std::vector<float>& x){ double pk=0; for(float v:x) pk=std::max(pk,(double)fabs(v)); int last=0; for(size_t i=0;i<x.size();++i) if(fabs(x[i])>pk*0.0316) last=i; return last/sr; };
  m.t30=t30(c)-t30(t); return m; }
int main(int argc,char**argv){
  FILE*f=fopen(argv[1],"rb"); std::vector<float> t; float v; while(fread(&v,4,1,f)==1) t.push_back(v); fclose(f);
  // trim to the same onset logic as analyzer: 2% of peak, 1.1 s
  float pk=0; for(float x:t) pk=std::max(pk,(float)std::fabs((double)x)); size_t st=0; while(st<t.size()&&fabs(t[st])<0.02f*pk) ++st; st=st>128?st-128:0;
  size_t len=std::min(t.size()-st,(size_t)48510); std::vector<float> tg(t.begin()+st,t.begin()+st+len); if(tg.size()<8192) tg.resize(8192,0);
  float pk2=0; for(float x:tg) pk2=std::max(pk2,(float)std::fabs((double)x)); for(auto&x:tg) x*=0.9f/pk2;
  FILE*g=fopen(argv[2],"r"); int note,ok; float tune,level,loss,match; fscanf(g,"%d %f %f %d %f %f",&note,&tune,&level,&ok,&loss,&match);
  Patch p; for(int i=0;i<kNumParams;++i){ float x; if(fscanf(g,"%f",&x)==1) p.p[i]=x; else break; }
  // old files have 75 params: fscanf stops -> remaining params keep defaults (tone=1, low=.5)
  { Patch d=defaultPatch(); p.p[P::OUT_TONE]= (p.p[P::OUT_TONE]==0.f&&argc>3)?d.p[P::OUT_TONE]:p.p[P::OUT_TONE]; if(argc>3){p.p[P::OUT_TONE]=1.f;p.p[P::OUT_LOW]=.5f;} }
  fclose(g); p.tune=tune; p.level=0.5f;
  double fr=440.0*pow(2.0,(note-69)/12.0);
  M best; double bestScore=1e9;
  for(double hf:{0.3,0.45,0.6,0.75,0.9}){ std::vector<float> c; renderOffline(p,44100.0,(float)fr,(int)tg.size(),(int)(hf*tg.size()),c);
    double e=0; for(float x:c) e+=x*x; double te=0; for(float x:tg) te+=x*x; if(e<=0) continue; double gn=sqrt(te/e); for(auto&x:c) x*=gn;
    M m=metrics(tg,c); double sc=m.lsd; if(sc<bestScore){bestScore=sc;best=m;} }
  printf("%-14s %s  LSD %5.1f dB | centroid err %.2f oct | HF %+5.1f dB | low %+5.1f dB | tail %+5.2f s | root %d tune %+.0fc\n",
    argv[1]+13, argc>3?"OLD":"NEW", best.lsd,best.centOct,best.hfDb,best.lowDb,best.t30,note,tune*100);
}
