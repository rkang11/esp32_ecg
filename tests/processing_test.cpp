#include "../EcgProcessing.h"
#include <stdio.h>
#include <stdlib.h>
#include <initializer_list>
float bump(float phase,float center,float width) {float x=(phase-center)/width; return expf(-x*x/2);}
int main() {
  bool ok=true;
  for(int target : {60,90,120,180}) {
    for(int polarity : {-1,1}) {
      EcgProcessing p; p.reset(2048,0);
      for(int i=0;i<15000;i++) {
        float t=i/250.0f, rr=60.0f/target, phase=fmodf(t,rr);
        float ecg=polarity*(700*bump(phase,rr*.25f,.012f)-180*bump(phase,rr*.29f,.012f)+180*bump(phase,rr*.60f,.060f)+60*bump(phase,rr*.1f,.03f));
        float noise=100*sinf(6.2831853f*60*t)+20*sinf(6.2831853f*83*t)+100*sinf(6.2831853f*.2f*t);
        p.step(2048+ecg+noise,i*4);
      }
      printf("target=%d polarity=%d estimate=%.1f\n",target,polarity,p.bpm);
      if(fabsf(p.bpm-target)>5) ok=false;
    }
  }
  EcgProcessing quiet; quiet.reset(2048,0);
  for(int i=0;i<15000;i++) quiet.step(2048+100*sinf(6.2831853f*60*i/250),i*4);
  printf("mains-only BPM=%.1f\n",quiet.bpm);
  return ok && quiet.bpm==0 ? 0 : 1;
}
