// SPDX-License-Identifier: MIT
// renders a result file (note tune level + params) at its root note; usage: render result.txt out.raw seconds holdFraction
#include "../Source/SynthEngine.h"
#include <cstdio>
#include <cstdlib>
using namespace kraken;
int main(int, char** a){ FILE* g=fopen(a[1],"r"); int note,ok; float tune,level,loss,match; if(fscanf(g,"%d %f %f %d %f %f",&note,&tune,&level,&ok,&loss,&match)!=6) return 1;
 Patch p; for(int i=0;i<kNumParams;++i) if(fscanf(g,"%f",&p.p[i])!=1) break; p.tune=tune; p.level=0.5f; fclose(g);
 int N=(int)(atof(a[3])*44100); std::vector<float> o; renderOffline(p,44100.0,440.0f*std::pow(2.0f,(note-69)/12.0f),N,(int)(N*atof(a[4])),o);
 FILE* f=fopen(a[2],"wb"); fwrite(o.data(),4,o.size(),f); fclose(f); }
