// Cross-check: with tilt 0 and rotation 0 the complex path must reproduce the
// real one, and rotation by 0.5 must be exact negation.
#include "../plugins/BlipSync/BlipSyncCore.hpp"
#include <cstdio>
using namespace blipsync;
int main(){
  const double nyq=48000*0.5*0.995;
  double worst=0, worstNeg=0, worstRms=0;
  int cases=0;
  double cfg[][3]={{200,0,8000},{100,0,12000},{80,2400,3200},{440,0,17600},
                   {40,0,18000},{1000,0,20000},{55,500,9000},{7,0,15000}};
  for(auto&c:cfg){
    Band b=makeBand(c[0],c[1],c[2],0.0,nyq,false);
    for(int i=0;i<200003;i++){
      double p=(double)i/200003.0*3.0-1.5;      // includes p=0 and wraps
      double a=evalBand(b,p);
      double d=evalBandRot(b,p,1.0,0.0);
      double n=evalBandRot(b,p,-1.0,0.0);       // rot = 0.5
      worst=std::max(worst,std::fabs(a-d));
      worstNeg=std::max(worstNeg,std::fabs(a+n));
      if(!std::isfinite(d)||!std::isfinite(n)){printf("NON-FINITE f=%g p=%g\n",c[0],p);return 1;}
    }
    cases++;
  }
  printf("complex path vs real path, %d configs x 200k phases:\n",cases);
  printf("   max |evalBandRot(rot=0) - evalBand|   = %.3e\n",worst);
  printf("   max |evalBandRot(rot=0.5) + evalBand| = %.3e\n",worstNeg);
  // exact p == 0 (the 0/0 guard)
  Band b=makeBand(200,0,8000,0.0,nyq,false);
  printf("   at p == 0 exactly: real %.15f  complex %.15f\n",evalBand(b,0.0),evalBandRot(b,0.0,1.0,0.0));
  // tilt: check the closed form against a brute-force sum
  for(double tilt : {0.0,-1.0,-4.0,-12.0,3.0}){
    Band bt=makeBand(100.0,0.0,12000.0,tilt,nyq,false);
    double r=bt.r, err=0;
    for(int i=0;i<20001;i++){
      double p=(double)i/20001.0-0.5;
      double ref=0,W=0;
      for(int k=1;k<=120;k++){
        double w=std::min(1.0,std::max(0.0,120.0-k+1.0))*std::pow(r,(double)(k-1));
        ref+=w*std::cos(2*M_PI*k*p); W+=w;
      }
      ref*= (W>1.0? 1.0/W : 1.0);
      err=std::max(err,std::fabs(evalBandRot(bt,p,1.0,0.0)-ref));
    }
    printf("   tilt %+6.1f dB/kHz: max |closed form - brute-force sum| = %.3e\n",tilt,err);
  }
  return 0;
}
