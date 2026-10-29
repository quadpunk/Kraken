// SPDX-License-Identifier: MIT
// Synthetic "real world" targets made with DSP that is independent from the kr@ken engine
// (own 4-pole ladder filter, FM, Karplus-Strong ...), so the analyzer cannot simply find the exact patch.
#include <cmath>
#include <cstdio>
#include <vector>
#include <string>
#include <random>
using namespace std;
static const double PI = 3.14159265358979323846;
static const double SR = 44100.0;

static void save(const string& name, const vector<float>& x)
{
    float pk = 0; for (float v : x) pk = max(pk, fabs(v));
    FILE* f = fopen(("/tmp/targets/" + name + ".raw").c_str(), "wb");
    vector<float> y(x); for (auto& v : y) v *= 0.8f / max(pk, 1e-6f);
    fwrite(y.data(), 4, y.size(), f); fclose(f);
}
struct Ladder { // Huovilainen-style 4 pole, tanh
    double s[4] = {0,0,0,0};
    double process(double in, double fc, double res) {
        double f = 2.0 * sin(PI * min(fc, 0.45 * SR) / SR);  // simple
        f = min(f, 1.2);
        double k = 4.0 * res;
        double x = in - k * s[3];
        x = tanh(x);
        s[0] += f * (x - tanh(s[0]));
        s[1] += f * (tanh(s[0]) - tanh(s[1]));
        s[2] += f * (tanh(s[1]) - tanh(s[2]));
        s[3] += f * (tanh(s[2]) - tanh(s[3]));
        return s[3];
    }
};
static double sawBL(double ph, double dt) { // polyblep saw
    double v = 2*ph - 1;
    if (ph < dt) { double t = ph/dt; v -= (t+t-t*t-1); }
    else if (ph > 1-dt) { double t = (ph-1)/dt; v -= (t*t+t+t+1); }
    return v;
}
static vector<float> acid(double f0, double cut0, double cutEnv, double envDecay, double res, double gate, double dur, double ampDec)
{
    int n = (int)(dur * SR); vector<float> o(n); Ladder L; double ph = 0;
    double env = 1.0, amp = 0;
    for (int i = 0; i < n; ++i) {
        double t = i / SR;
        env *= exp(-1.0 / (envDecay * SR));
        double fc = cut0 + cutEnv * env;
        ph += f0 / SR; ph -= floor(ph);
        double x = sawBL(ph, f0 / SR);
        double y = 0;
        // 2x oversample the filter loop
        y = L.process(x, fc, res); y = L.process(x, fc, res);
        double a = (t < gate) ? min(1.0, t / 0.003) * exp(-t / ampDec) : 0;
        static double ar = 0; (void)ar;
        amp += 0.01 * (a - amp);
        o[i] = (float)(tanh(2.0 * y * 1.5) * amp);
    }
    return o;
}
int main()
{
    system("mkdir -p /tmp/targets");
    mt19937 rng(7); normal_distribution<double> nd(0, 1);
    save("acid_a1", acid(55.0, 180, 2600, 0.22, 0.85, 0.35, 0.9, 0.5));
    save("acid_e2_res", acid(82.41, 250, 3800, 0.14, 0.95, 0.2, 0.7, 0.3));
    save("acid_c2_long", acid(65.41, 120, 1500, 0.45, 0.7, 0.6, 1.2, 1.0));
    { // soft sub bass: sine + a bit of 2nd/3rd harmonic, long
        int n = (int)(1.2*SR); vector<float> o(n);
        for (int i=0;i<n;++i){ double t=i/SR; double ph=2*PI*45.0*t; double a=min(1.0,t/0.01)*exp(-t/0.9);
            o[i]=(float)((sin(ph)+0.25*sin(2*ph+0.3)+0.06*sin(3*ph))*a);} save("sub_f1", o);
    }
    { // supersaw pad @C3
        int n=(int)(1.3*SR); vector<float> o(n); double ph[7]; double det[7]={-0.18,-0.11,-0.05,0,0.05,0.11,0.18};
        for(int k=0;k<7;++k) ph[k]=(k*0.137); Ladder L; double f0=130.81;
        for(int i=0;i<n;++i){ double t=i/SR; double x=0; for(int k=0;k<7;++k){ double f=f0*pow(2.0,det[k]/12.0); ph[k]+=f/SR; ph[k]-=floor(ph[k]); x+=sawBL(ph[k],f/SR);} 
            double a=min(1.0,t/0.25)*(t<1.0?1:exp(-(t-1.0)/0.15)); double y=L.process(x/7,1800+900*min(1.0,t/0.5),0.1); y=L.process(x/7,1800+900*min(1.0,t/0.5),0.1);
            o[i]=(float)(y*a);} save("pad_c3", o);
    }
    { // FM e-piano @A3
        int n=(int)(1.2*SR); vector<float> o(n); double f0=220.0;
        for(int i=0;i<n;++i){ double t=i/SR; double idx=2.2*exp(-t/0.25)+0.3; double a=min(1.0,t/0.002)*exp(-t/0.7);
            double m=sin(2*PI*f0*14*t/14.0*1.0*0+2*PI*f0*t); // ratio 1
            o[i]=(float)(sin(2*PI*f0*t+idx*sin(2*PI*f0*t))*a + 0.15*sin(2*PI*f0*7*t+0.5*idx*sin(2*PI*f0*7*t))*exp(-t/0.05)); (void)m;} save("fm_epiano_a3", o);
    }
    { // Karplus-Strong pluck @ E4 (329.63)
        double f0=329.63; int n=(int)(1.2*SR); vector<float> o(n); int L=(int)(SR/f0); double frac=SR/f0-L; vector<double> b(L+2);
        for(auto& v:b) v=nd(rng); int w=0; double prev=0;
        for(int i=0;i<n;++i){ int r1=(w+1)%(L), r2=(w+2)%L; double y=0.5*(b[w]+b[r1]); y=y*0.9985; b[w]=y; w=(w+1)%L; o[i]=(float)b[r1]; (void)prev;(void)frac;(void)r2;} save("pluck_e4", o);
    }
    { // square lead with vibrato @ A4 through gentle lowpass
        int n=(int)(1.0*SR); vector<float> o(n); double ph=0; Ladder L;
        for(int i=0;i<n;++i){ double t=i/SR; double f=440*pow(2.0,0.2*sin(2*PI*5.5*t)*min(1.0,t/0.4)/12.0); ph+=f/SR; ph-=floor(ph);
            double x=sawBL(ph,f/SR)-sawBL(fmod(ph+0.5,1.0),f/SR); double a=min(1.0,t/0.01)*(t<0.8?1:exp(-(t-0.8)/0.05));
            double y=L.process(x*0.5,5000,0.15); y=L.process(x*0.5,5000,0.15); o[i]=(float)(y*a);} save("lead_sq_a4", o);
    }
    { // ladder-filter bass @E2 two detuned saws, 4 pole LP, env
        int n=(int)(1.0*SR); vector<float> o(n); double p1=0,p2=0.3; Ladder L; double f0=82.41; double env=1;
        for(int i=0;i<n;++i){ double t=i/SR; env*=exp(-1.0/(0.18*SR)); p1+=f0/SR; p1-=floor(p1); p2+=f0*1.006/SR; p2-=floor(p2);
            double x=0.5*(sawBL(p1,f0/SR)+sawBL(p2,f0*1.006/SR)); double a=min(1.0,t/0.004)*(t<0.7?1:exp(-(t-0.7)/0.06))*exp(-t/1.5);
            double fc=200+3200*env; double y=L.process(x,fc,0.45); y=L.process(x,fc,0.45); o[i]=(float)(y*a);} save("ladder_bass_e2", o);
    }

    // ---- more varied material -----------------------------------------------------------------------
    { // kick: sine with fast exponential pitch drop
        int n=(int)(0.9*SR); vector<float> o(n); double ph=0;
        for(int i=0;i<n;++i){ double t=i/SR; double f=45+120*exp(-t/0.03); ph+=f/SR; double a=exp(-t/0.28)*min(1.0,t/0.0015);
            o[i]=(float)(tanh(1.6*sin(2*PI*ph))*a + 0.2*nd(rng)*exp(-t/0.004)); } save("kick", o);
    }
    { // snare: tone + high-passed noise
        int n=(int)(0.7*SR); vector<float> o(n); double ph=0, lp=0;
        for(int i=0;i<n;++i){ double t=i/SR; double f=185+60*exp(-t/0.02); ph+=f/SR; double x=nd(rng); double hp=x-lp; lp+=0.35*(x-lp);
            o[i]=(float)(0.55*sin(2*PI*ph)*exp(-t/0.09) + 0.9*hp*exp(-t/0.16)); } save("snare", o);
    }
    { // closed hat: noise + inharmonic square cluster, high-passed
        int n=(int)(0.4*SR); vector<float> o(n); double fr[6]={205.3,304.4,369.6,522.7,540.0,800.0}; double ph[6]={0}; double lp=0;
        for(int i=0;i<n;++i){ double t=i/SR; double x=0; for(int k=0;k<6;++k){ ph[k]+=fr[k]*2.7/SR; ph[k]-=floor(ph[k]); x+=(ph[k]<0.5?1:-1);} x=x/6*0.6+0.6*nd(rng);
            double hp=x-lp; lp+=0.25*(x-lp); hp=hp-0.0*lp; o[i]=(float)(hp*exp(-t/0.035)); } save("hat", o);
    }
    { // vowel "ah" at A3: saw through three formant band-pass biquads, light vibrato
        int n=(int)(1.2*SR); vector<float> o(n); double ph=0; double F[3]={700,1100,2600}, Q[3]={8,10,14}, G[3]={1.0,0.7,0.35};
        double x1[3]={0},x2[3]={0},y1[3]={0},y2[3]={0};
        for(int i=0;i<n;++i){ double t=i/SR; double f=220*pow(2.0,0.15*sin(2*PI*5.2*t)/12.0); ph+=f/SR; ph-=floor(ph); double x=sawBL(ph,f/SR); double y=0;
            for(int k=0;k<3;++k){ double w=2*PI*F[k]/SR, al=sin(w)/(2*Q[k]); double b0=al,b2=-al,a0=1+al,a1=-2*cos(w),a2=1-al;
                double yk=(b0*x+b2*x2[k]-a1*y1[k]-a2*y2[k])/a0; x2[k]=x1[k]; x1[k]=x; y2[k]=y1[k]; y1[k]=yk; y+=G[k]*yk; }
            double a=min(1.0,t/0.08)*(t<1.0?1:exp(-(t-1.0)/0.05)); o[i]=(float)(y*a);} save("vowel_ah_a3", o);
    }
    { // bell: inharmonic partials
        int n=(int)(1.5*SR); vector<float> o(n); double f0=523.25; double r[5]={1.0,2.76,5.40,8.93,13.3}, d[5]={1.2,0.7,0.4,0.25,0.12}, a[5]={1,0.6,0.45,0.3,0.15};
        for(int i=0;i<n;++i){ double t=i/SR; double x=0; for(int k=0;k<5;++k) x+=a[k]*sin(2*PI*f0*r[k]*t)*exp(-t/d[k]); o[i]=(float)(x*min(1.0,t/0.001)); } save("bell_c5", o);
    }
    { // string ensemble: 5 detuned saws, slow attack, soft lowpass
        int n=(int)(1.3*SR); vector<float> o(n); double ph[5]={0,.2,.4,.6,.8}; double det[5]={-0.12,-0.06,0,0.07,0.13}; Ladder L; double f0=220;
        for(int i=0;i<n;++i){ double t=i/SR; double x=0; for(int k=0;k<5;++k){ double f=f0*pow(2.0,det[k]/12.0)*(1+0.002*sin(2*PI*(4.5+0.3*k)*t)); ph[k]+=f/SR; ph[k]-=floor(ph[k]); x+=sawBL(ph[k],f/SR);} 
            double y=L.process(x/5,3200,0.05); y=L.process(x/5,3200,0.05); o[i]=(float)(y*min(1.0,t/0.3)*(t<1.1?1:exp(-(t-1.1)/0.1))); } save("strings_a3", o);
    }
    { // brass stab at E3: two saws, cutoff opens fast
        int n=(int)(0.9*SR); vector<float> o(n); double p1=0,p2=0.4; Ladder L; double f0=164.81;
        for(int i=0;i<n;++i){ double t=i/SR; p1+=f0/SR;p1-=floor(p1);p2+=f0*1.004/SR;p2-=floor(p2); double x=0.5*(sawBL(p1,f0/SR)+sawBL(p2,f0*1.004/SR));
            double fc=500+3500*(t<0.07?t/0.07:exp(-(t-0.07)/0.25)); double y=L.process(x,fc,0.12); y=L.process(x,fc,0.12); o[i]=(float)(y*min(1.0,t/0.01)*exp(-t/0.5)); } save("brass_stab_e3", o);
    }
    { // wobble bass at E1 (41.2 Hz): saw through ladder with LFO on cutoff
        int n=(int)(1.3*SR); vector<float> o(n); double ph=0; Ladder L; double f0=41.2;
        for(int i=0;i<n;++i){ double t=i/SR; ph+=f0/SR;ph-=floor(ph); double x=sawBL(ph,f0/SR); double fc=250*pow(2.0,2.6*sin(2*PI*3.0*t)+1.2);
            double y=L.process(x,fc,0.5); y=L.process(x,fc,0.5); o[i]=(float)(tanh(1.5*y)*min(1.0,t/0.005)*(t<1.1?1:exp(-(t-1.1)/0.05))); } save("wobble_e1", o);
    }
    { // drawbar organ at C4 with key click
        int n=(int)(1.0*SR); vector<float> o(n); double f0=261.63; double h[6]={1,2,3,4,6,8}, g[6]={1,0.8,0.5,0.35,0.25,0.15};
        for(int i=0;i<n;++i){ double t=i/SR; double x=0; for(int k=0;k<6;++k) x+=g[k]*sin(2*PI*f0*h[k]*t); x*= (1+0.06*sin(2*PI*6.3*t)); x+=0.25*nd(rng)*exp(-t/0.006);
            o[i]=(float)(x*min(1.0,t/0.004)*(t<0.8?1:exp(-(t-0.8)/0.03))); } save("organ_c4", o);
    }
    { // noise riser: band-pass noise sweeping up
        int n=(int)(1.1*SR); vector<float> o(n); double lp1=0,bp=0; 
        for(int i=0;i<n;++i){ double t=i/SR; double fc=300*pow(14.0,t/1.1); double f=2*sin(PI*fc/SR); double x=nd(rng); lp1+=f*bp; double hp=x-lp1-0.35*bp; bp+=f*hp; o[i]=(float)(bp*min(1.0,t/0.2)); } save("riser", o);
    }
    { // PWM lead at A4
        int n=(int)(1.0*SR); vector<float> o(n); double ph=0; Ladder L; double f0=440;
        for(int i=0;i<n;++i){ double t=i/SR; ph+=f0/SR; ph-=floor(ph); double pw=0.5+0.35*sin(2*PI*1.2*t); double x=sawBL(ph,f0/SR)-sawBL(fmod(ph+pw,1.0),f0/SR);
            double y=L.process(x*0.6,6000,0.1); y=L.process(x*0.6,6000,0.1); o[i]=(float)(y*min(1.0,t/0.01)*(t<0.8?1:exp(-(t-0.8)/0.05))); } save("pwm_lead_a4", o);
    }
    printf("targets written\n");
}
