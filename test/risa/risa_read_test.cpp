// Only this public correctness fixture opens output shares.
#include "risa/read.h"
#include "spiral_u64.h"
#include <iostream>
#include <string>
#include <type_traits>

using namespace risa;
static void check(bool ok,const char* reason){if(!ok)throw std::runtime_error(reason);}
template<class F>static void rejects(F fn){try{fn();}catch(const std::logic_error&){return;}throw std::runtime_error("invalid input accepted");}
static Word value(Word x,Word salt) {
    if((x+salt)%7==0)return ~Word{0};
    if((x+salt)%7==1)return Word{1}<<63;
    return x*0x9e3779b97f4a7c15ULL+salt;
}
static std::vector<Word> shares(int party,Word n,Word salt) {
    std::vector<Word> out(n);
    for(Word i=0;i<n;++i){auto mask=(i+salt)*0xd6e8feb86659fd93ULL;out[i]=party?value(i,salt)-mask:mask;}
    return out;
}
static_assert(!std::is_copy_constructible_v<ReadState>);
int main(int argc,char** argv) {
    try {
        if(argc!=3)return 2;
        const int b=std::stoi(argv[1]),port=std::stoi(argv[2]);
        if((b!=0&&b!=1)||port<1024||port>65535)return 2;
        emp::NetIO io(b? "127.0.0.1":nullptr,port);Backend net(b,io);
        const auto stride=spiral_u64::Response::wire_size();
        for(Word n:{Word{1},Word{5},Word{2053}}) {
            auto ctx=SetupRead(net,n);
            const Word a0=n-1,a1=n==2053?1024:n-1, z0=n==2053?1023:0,z1=n>1?1:0;
            auto a=CompileRead(ctx,b?a1:a0), z=CompileRead(ctx,b?z1:z0);
            auto db=shares(b,n,0), db2=shares(b,n,37);
            auto bytes=io.counter,ex=net.read_frame_exchanges();
            rejects([&]{CompileRead(ctx,n);});
            check(ReadBatch(ctx,0,{}).empty(),"empty batch");
            rejects([&]{ReadBatch(ctx,0,{{a,db},{a,db}});});
            std::vector<Word> bad;rejects([&]{Read(ctx,a,0,bad);});
            check(io.counter==bytes && net.read_frame_exchanges()==ex && a.can_read(0),"preflight sent bytes");
            for(Word t:{Word{0},Word{3},~Word{0}}) {
                auto salt=t;db=shares(b,n,salt);db2=shares(b,n,salt+37);
                bytes=io.counter;ex=net.read_frame_exchanges();
                auto y=ReadBatch(ctx,t,{{z,db2},{a,db}});
                check(y.size()==2 && io.counter-bytes==2*stride && net.read_frame_exchanges()-ex==1,"batch exchange");
                check(net.open_sum(y[0])==value((z0+z1)%n,salt+37),"ordered read 0");
                check(net.open_sum(y[1])==value((a0+a1)%n,salt),"ordered read 1");
            }
            bytes=io.counter;rejects([&]{Read(ctx,a,~Word{0},db);});
            rejects([&]{Read(ctx,z,0,db);});check(io.counter==bytes,"counter reuse communicated");
            auto fresh=CompileRead(ctx,b?a1:a0);
            auto moved=std::move(fresh);rejects([&]{Read(ctx,fresh,0,db);});
            bytes=io.counter;auto y=Read(ctx,moved,1,db);
            check(io.counter-bytes==stride && net.open_sum(y)==value((a0+a1)%n,~Word{0}),"single read");
            // Next array is derived from the previous round's LOCAL output.
            for(auto& item:db)item=y+Word(b);
            check(net.open_sum(Read(ctx,moved,2,db))==value((a0+a1)%n,~Word{0})+1,"adaptive input");
        }
        spiral_u64::Client c(5);
        auto pp=c.public_parameters();auto peer=spiral_u64::PeerParameters::decode(5,pp);
        auto q=c.make_query(3).encode();q.pop_back();
        rejects([&]{spiral_u64::Query::decode(peer,q);});
        rejects([&]{spiral_u64::PeerParameters::decode(6,pp);});
        pp[0]^=1;rejects([&]{spiral_u64::PeerParameters::decode(5,pp);});
        std::cout<<"PASS P"<<b<<" Read + ReadBatch: shared-index wrap, multi-block, reuse, wire checks\n";
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
