#include "java_random.hpp"
#include <cstdio>
#include <cstdint>
#include <cstring>
using namespace pzformat;
int main(int argc,char**argv){
  if(argc<2){std::fprintf(stderr,"usage: rng_oracle <out.txt>\n");return 2;}
  FILE*f=std::fopen(argv[1],"wb");
  // Seeds chosen to mirror the real call sites: GisCells' per-cell
  // SEED*31 + cx*7919 + cy, its per-building SEED*131 + i, MaskRule's fixed
  // 1 and 12345, plus 0, -1 and a large negative to exercise sign handling.
  const long long seeds[]={0,1,12345,-1,42,1337LL*31+200LL*7919+200LL,1337LL*131+7LL,-9007199254740993LL};
  for(long long s: seeds){
    JavaRandom r(s);
    for(int i=0;i<200;i++){
      double d=r.nextDouble(); std::uint64_t bits; std::memcpy(&bits,&d,sizeof bits);
      std::fprintf(f,"%lld\td\t%d\t%016llx\n",s,i,static_cast<unsigned long long>(bits));
    }
    JavaRandom r2(s);
    for(int i=0;i<200;i++) std::fprintf(f,"%lld\ti\t%d\t%d\n",s,i,r2.nextInt(97));
    JavaRandom r3(s);
    for(int i=0;i<200;i++) std::fprintf(f,"%lld\tp\t%d\t%d\n",s,i,r3.nextInt(64));
    JavaRandom r4(s);
    for(int i=0;i<200;i++) std::fprintf(f,"%lld\tl\t%d\t%lld\n",s,i,static_cast<long long>(r4.nextLong()));
    JavaRandom r5(s);
    for(int i=0;i<200;i++) std::fprintf(f,"%lld\tb\t%d\t%d\n",s,i,r5.nextBoolean()?1:0);
  }
  std::fclose(f); std::printf("cpp  rng: 8 seeds x 5 generators x 200 draws -> %s\n",argv[1]);
}
