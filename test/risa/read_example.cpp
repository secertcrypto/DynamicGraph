// Public demo; reconstruction is for a test check only.
#include "risa/read.h"
#include <iostream>
#include <string>

int main(int argc,char** argv) {
    try {
        if(argc<3 || argc>4){std::cerr<<"usage: risa_read_example PARTY PORT [SERVER_IP]\n";return 2;}
        int b=std::stoi(argv[1]),port=std::stoi(argv[2]);
        if((b!=0 && b!=1)||port<1024||port>65535)return 2;
        emp::NetIO io(b?(argc==4?argv[3]:"127.0.0.1"):nullptr,port);
        risa::Backend net(b,io);
        auto ctx=risa::SetupRead(net,5);
        auto a=risa::CompileRead(ctx,4);             // 4+4 mod 5 = 3
        auto z=risa::CompileRead(ctx,b?1:0);         // index 1
        std::vector<risa::Word> db(5);
        for(size_t i=0;i<5;++i)db[i]=b?100+i-23:23;
        auto y=risa::Read(ctx,a,0,db);
        if(net.open_sum(y)!=103)throw std::runtime_error("single read failed");
        if(b)for(auto& v:db)v+=10;                  // new snapshot, same compiled state
        auto ys=risa::ReadBatch(ctx,1,{{a,db},{z,db}});
        auto v0=net.open_sum(ys[0]),v1=net.open_sum(ys[1]);
        if(v0!=113||v1!=111)throw std::runtime_error("batch read failed");
        std::cout<<"PASS P"<<b<<" single=103 batch=["<<v0<<","<<v1<<"]\n";
    } catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
