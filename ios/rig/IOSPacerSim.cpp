// The pacer (ios/IOSPacer.h) against a pretend screen: g++ -O2 ios/rig/IOSPacerSim.cpp -o /tmp/sim
// ./sim <true refresh ns> <reported refresh ns> <ask times 0/1> <screen phase ns> <seed> <max frame work ns>
// e.g. ./sim 8333333 8333333 0 2000000 1 9e6 (build 14: uneven) vs ... 1 ... (on time)
#include "../IOSPacer.h"
#include <algorithm>
#include <cstdlib>
#include <cstdio>
#include <random>
#include <vector>
#include <deque>
// pretend screen: refreshes at phase + n*Vtrue; a present is shown at the first refresh >= max(desired, ready)
int main(int argc,char**argv){
  double Vtrue = atof(argv[1]); // ns
  double Vrep = atof(argv[2]);
  int useDesired = atoi(argv[3]);
  double phase = atof(argv[4]);
  std::mt19937 rng(atoi(argv[5]));
  std::uniform_real_distribution<double> work(3e6, atof(argv[6])), jit(0, 1e6);
  IOSPacer p; p.refresh = (uint64_t)Vrep;
  uint64_t now = 1000000000;
  struct Pend{uint32_t id; uint64_t shown;};
  std::deque<Pend> pend; uint64_t lastShown=0; std::vector<double> dts; int frames=6000;
  for(int f=0; f<frames; f++){
    uint64_t sl = IOSPacer_Wake(p, now, 16666667);
    now += sl + (sl? (uint64_t)jit(rng):0);
    // feedback: presents shown before now
    while(!pend.empty() && pend.front().shown <= now){ IOSPacer_Shown(p, pend.front().id, pend.front().shown); pend.pop_front(); }
    now += (uint64_t)work(rng); // cpu+gpu until ready
    uint32_t id; uint64_t des = IOSPacer_Present(p,&id); if(!useDesired) des=0;
    uint64_t t = std::max<uint64_t>(des, now);
    double n = ceil((t - phase)/Vtrue); uint64_t shown = (uint64_t)(phase + n*Vtrue);
    if(shown <= lastShown) shown = (uint64_t)(lastShown + Vtrue); // FIFO: one per refresh
    if(f>600) dts.push_back((shown-(double)lastShown)/1e6);
    lastShown = shown; pend.push_back({id,shown});
  }
  int uneven=0; double mn=1e9,mx=0; for(double d:dts){ if(fabs(d-16.667)>Vtrue/2e6) uneven++; mn=std::min(mn,d); mx=std::max(mx,d);}
  auto s = IOSPacer_TakeStats(p);
  printf("Vtrue %.4f desired %d: uneven %d of %zu, min %.2f max %.2f | pacer log: uneven %d late %d err %.2f sd %.3f\n", Vtrue/1e6, useDesired, uneven, dts.size(), mn, mx, s.uneven, s.late, s.errMs, s.sdMs);
}
