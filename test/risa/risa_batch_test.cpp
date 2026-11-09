#include "risa/update.h"
#include <iostream>
#include <stdexcept>
#include <string>

using namespace risa;
static void check(bool ok) { if (!ok) throw std::runtime_error("batch conformance failed"); }
template<class F> static void rejects(F fn) {
    try { fn(); } catch (const std::logic_error&) { return; }
    throw std::runtime_error("invalid batch accepted");
}
int main(int argc,char** argv) {
    try {
        if (argc != 3) return 2;
        int b=std::stoi(argv[1]), port=std::stoi(argv[2]);
        emp::NetIO io(b ? "127.0.0.1" : nullptr,port);
        Backend net(b,io);
        auto pp=Setup(5);
        auto a=Compile(net,pp,4);             // 4+4 = 3 mod 5
        auto d=Compile(net,pp,b ? 0 : 3);    // same secret destination
        auto e=Compile(net,pp,b ? 2 : 4);    // destination 1
        auto wrong=Compile(net,Setup(1),0);
        std::vector<BatchInput> batch{{e,b ? 21u : 1u},{d,b ? 15u : 2u},{a,b ? 38u : 3u}};
        auto exchanges=net.vector_exchanges(), bytes=io.counter;
        check(UpdateBatch(net,pp,0,{}).empty());
        auto zero=UpdateBatchGather(net,pp,0,{});
        check(zero.shares==std::vector<Word>(5,0) && zero.max_coordinate_error==0);
        rejects([&]{UpdateBatch(net,pp,0,{{a,0},{a,1}});});
        rejects([&]{UpdateBatchGather(net,pp,0,{{a,0},{wrong,1}});});
        check(a.can_update(0) && io.counter==bytes && net.vector_exchanges()==exchanges);
        auto rows=UpdateBatch(net,pp,0,batch);
        check(rows.size()==3 && io.counter-bytes==24 && net.vector_exchanges()==++exchanges);
        const Word dst[]={1,3,3}, msgs[]={22,17,41};
        for (size_t i=0;i<rows.size();++i) {
            check(rows[i].max_coordinate_error==1 && rows[i].shares.size()==5);
            for (size_t x=0;x<5;++x) {
                auto err=net.open_sum(rows[i].shares[x])-(x==dst[i]?msgs[i]:0);
                check(err<=1 || (x==dst[i] && err==~Word{0}));
            }
        }
        bytes=io.counter;
        rejects([&]{UpdateBatchGather(net,pp,0,batch);});
        check(bytes==io.counter && exchanges==net.vector_exchanges());
        auto sum=UpdateBatchGather(net,pp,7,batch);
        check(sum.max_coordinate_error==3 && io.counter-bytes==24 && net.vector_exchanges()==++exchanges);
        for (size_t x=0;x<5;++x) {
            Word err=net.open_sum(sum.shares[x])-(x==1?22:x==3?58:0);
            check(err<=3 || Word{0}-err<=3);
        }
        std::cout<<"PASS both APIs: one vector exchange, 24 bytes per party for 3 edges\n";
    } catch(const std::exception& ex) { std::cerr<<ex.what()<<'\n'; return 1; }
}
