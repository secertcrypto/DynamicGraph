#include "src/2pc/protocol.h"
#include "src/2pc/libsss/benes.h"
#include <algorithm>
#include <iostream>
#include <numeric>
#include <random>
#include <stdexcept>
using namespace graphiti::twopc;
using namespace graphiti::twopc::detail;
static Words open_words(Protocol& p,const Words& local) {
    Words out(local.size());
    auto b=p.ToBits(local,64);
    for(unsigned i=0;i<64;++i){auto o=p.Open(b[i]);for(std::size_t j=0;j<o.size();++j)out[j]|=Word(o[j])<<i;}
    return out;
}
int main(int argc,char** argv) try {
    if(argc<3)return 2;
    int id=std::stoi(argv[1]),port=std::stoi(argv[2]);
    emp::NetIO io(id==0?nullptr:"127.0.0.1",port,true);Protocol p(id,io);
    // Fixed known fixture randomness is ONLY for tests, never protocol masks.
    std::mt19937_64 fixture(54721);
    Words x={0,1,2,3,Word{1}<<63,~Word{0},~Word{0}-1},y=x;
    for(int i=0;i<19;++i){x.push_back(fixture());y.push_back(fixture());}
    Words sx(x.size()),sy(y.size());
    for(std::size_t i=0;i<x.size();++i) {
        auto a=fixture(),b=fixture();sx[i]=id==0?a:x[i]-a;sy[i]=id==0?b:y[i]-b;
    }
    auto bx=p.ToBits(sx,64),by=p.ToBits(sy,64);
    auto gt=p.Open(p.Greater(bx,by));
    for(std::size_t i=0;i<x.size();++i)if(gt[i]!=(x[i]>y[i]))throw std::runtime_error("unsigned comparison");
    auto av=p.BitToArithmetic(p.Greater(bx,by));auto aopen=open_words(p,av);
    for(std::size_t i=0;i<x.size();++i)if(aopen[i]!=gt[i])throw std::runtime_error("B2A");
    for(Word m:{1,2,3,5,17}) {
        Words shares,expected;
        for(Word a=0;a<m;++a)for(Word b=0;b<m;++b){shares.push_back(id==0?a:b);expected.push_back((a+b)%m);}
        if(open_words(p,p.NormalizeIndices(shares,m))!=expected)throw std::runtime_error("mod-N normalization");
    }
    for(std::size_t n:{1,2,3,4,5,8,17})for(int owner=0;owner<2;++owner) {
        auto target=std::vector<int>(n);std::iota(target.begin(),target.end(),0);
        std::shuffle(target.begin(),target.end(),fixture);
        Rows input(n,Words(3));
        for(std::size_t i=0;i<n;++i)for(std::size_t j=0;j<3;++j) {
            Word r=fixture(),v=100*i+j;input[i][j]=id==0?r:v-r;
        }
        auto out=p.PrivatePermute(input,id==owner?target:std::vector<int>{},owner);
        Words flat;for(auto& row:out)flat.insert(flat.end(),row.begin(),row.end());
        auto opened=open_words(p,flat);
        for(std::size_t i=0;i<n;++i)for(std::size_t j=0;j<3;++j)
            if(opened[3*i+j]!=100*Word(target[i])+j)throw std::runtime_error("Beneš private permutation");
        auto back=p.PrivatePermute(out,id==owner?inverse(target):std::vector<int>{},owner);
        flat.clear();for(auto& row:back)flat.insert(flat.end(),row.begin(),row.end());
        opened=open_words(p,flat);
        for(std::size_t i=0;i<n;++i)for(std::size_t j=0;j<3;++j)
            if(opened[3*i+j]!=100*i+j)throw std::runtime_error("inverse permutation");
    }
    std::cout<<"PASS two-party primitives P"<<id<<"\n";
    return 0;
} catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
