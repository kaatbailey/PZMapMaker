// footprint_oracle.cpp — C++ side of the FootprintSnap cross-language oracle.
//   footprint_oracle <out.txt>
// Corpus is generated from JavaRandom with a fixed seed, so both trees build
// byte-identical inputs without shipping a data file.
#include "footprintsnap.hpp"
#include "java_random.hpp"
#include "geojson.hpp"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
using namespace pzformat;

static std::string bits(double d){ std::uint64_t b; std::memcpy(&b,&d,8);
  char t[32]; std::snprintf(t,sizeof t,"%016llx",(unsigned long long)b); return t; }

int main(int argc,char**argv){
  if(argc<2){ std::fprintf(stderr,"usage: footprint_oracle <out.txt>\n"); return 2; }
  std::ofstream o(argv[1], std::ios::binary);
  std::size_t lines=0;
  auto emit=[&](const std::string&s){ o<<s<<'\n'; lines++; };

  // ---- fixed cases mirroring the Java self-test -------------------------
  std::vector<std::vector<std::pair<int,int>>> fixed = {
    {{10,10},{20,10},{20,16},{10,16}},                 // axis-aligned 10x6
    {{100,90},{110,100},{100,110},{90,100}},           // 45-degree diamond
    {{0,0},{30,15},{26,23},{-4,8}},                    // thin barn, negative x
    {{0,0},{1,1}},                                     // degenerate
    {{-20,-20},{-10,-20},{-10,-14},{-20,-14}},         // fully negative
    {{0,0},{5,0},{5,5},{0,5},{0,0}},                   // repeated closing vertex
    {{3,3},{3,3},{3,3}},                               // all identical
  };
  for(std::size_t i=0;i<fixed.size();i++){
    auto r=FootprintSnap::snapRing(fixed[i]);
    emit("X\t"+std::to_string(i)+"\t"+(r? r->toString():"null")+"\t"+
         (FootprintSnap::isAxisAligned(fixed[i])?"1":"0"));
  }

  // ---- randomised corpus ------------------------------------------------
  JavaRandom rng(20260831LL);
  for(int k=0;k<20000;k++){
    int n = 3 + rng.nextInt(10);
    double cx = rng.nextDouble()*2000.0 - 1000.0;
    double cy = rng.nextDouble()*2000.0 - 1000.0;
    double sx = 0.5 + rng.nextDouble()*60.0;
    double sy = 0.5 + rng.nextDouble()*60.0;
    std::vector<FootprintSnap::Point> ring;
    for(int i=0;i<n;i++){
      // No transcendentals anywhere: raw draws only, so both trees build
      // BIT-IDENTICAL inputs. The first corpus used cos/sin and the rings
      // themselves differed by ulp, which contaminated the comparison.
      double ux = rng.nextDouble()*2.0 - 1.0;
      double uy = rng.nextDouble()*2.0 - 1.0;
      ring.push_back({cx + ux*sx, cy + uy*sy});
    }
    auto r = FootprintSnap::snap(ring);
    auto h = FootprintSnap::hull(FootprintSnap::dedupeExact(ring));
    auto m = FootprintSnap::minAreaRect(h);
    emit("R\t"+std::to_string(k)+"\t"+(r? r->toString():"null")+
         "\t"+bits(FootprintSnap::area(ring))+
         "\t"+std::to_string(h.size())+
         "\t"+bits(m[2])+"\t"+bits(m[3]));
  }

  // ---- real footprints from GeoJSON, projected to metres ----------------
  for(int fi=2; fi<argc; fi++){
    GeoJson g=GeoJson::read(argv[fi]);
    double minLon=1e18,maxLat=-1e18;
    for(auto&f:g.features) for(auto&r:f.rings) for(auto&p:r){
      minLon=std::min(minLon,p.lon); maxLat=std::max(maxLat,p.lat); }
    if(g.features.empty()) continue;
    double mPerLat=111320.0, mPerLon=111320.0*std::cos(maxLat*3.141592653589793/180.0);
    for(std::size_t i=0;i<g.features.size();i++){
      for(std::size_t ri=0;ri<g.features[i].rings.size();ri++){
        std::vector<FootprintSnap::Point> ring;
        for(auto&p:g.features[i].rings[ri])
          ring.push_back({(p.lon-minLon)*mPerLon,(maxLat-p.lat)*mPerLat});
        auto r=FootprintSnap::snap(ring);
        auto h=FootprintSnap::hull(FootprintSnap::dedupeExact(ring));
        emit("G\t"+std::to_string(fi)+"\t"+std::to_string(i)+"\t"+std::to_string(ri)+"\t"
             +(r? r->toString():"null")+"\t"+bits(FootprintSnap::area(ring))
             +"\t"+std::to_string(h.size()));
      }
    }
  }
  std::printf("cpp  footprint: %zu lines -> %s\n",lines,argv[1]);
}
