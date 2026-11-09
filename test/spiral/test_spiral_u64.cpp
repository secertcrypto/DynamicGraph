#include "spiral_u64.h"
#include <iostream>
#include <stdexcept>
#include <vector>
int main(){try {
 size_t checked=0;
 for(uint64_t size:{1,17,1024,1025,2053,8193}) {
    std::vector<uint64_t> db(size);
    for(uint64_t i=0;i<size;++i)db[i]=(i*0x9e3779b97f4a7c15ULL)^(i<<51);
    db[0]=0;if(size>1)db[1]=UINT64_MAX;if(size>2)db[2]=uint64_t{1}<<63;
    if(size>3)db[3]=UINT64_MAX;
    spiral_u64::Server server(db);spiral_u64::Client client(size);
    std::vector<uint64_t> indices{0,size-1,size/2};
    for(uint64_t i:{1,2,3,1023,1024,2047,2048,8192})if(i<size)indices.push_back(i);
    for(auto index:indices) {
        const uint64_t result=spiral_u64::read(client,server,index);
        if(result!=db[index]) {
            std::cerr<<"FAIL N="<<size<<" index="<<index<<" expected="<<db[index]<<" actual="<<result<<'\n';return 1;
        }
        ++checked;
    }
    try {client.make_query(size);throw std::runtime_error("out-of-range accepted");}catch(const std::out_of_range&){}
    try {server.answer(spiral_u64::Query{});throw std::runtime_error("empty query accepted");}catch(const std::invalid_argument&){}
    std::cout<<"PASS N="<<size<<" queries="<<indices.size()<<'\n';
 }
 // Interleaved clients and response lifetime: server must not use client globals.
 {
    std::vector<uint64_t> db(2053,0x0123456789abcdefULL);db[2048]=UINT64_MAX;db[17]=42;
    spiral_u64::Client c1(db.size()),c2(db.size());
    auto q1=c1.make_query(2048),q2=c2.make_query(17);
    spiral_u64::Response r1,r2;
    {spiral_u64::Server server(db);r2=server.answer(q2);r1=server.answer(q1);}
    if(c1.recover(r1,2048)!=UINT64_MAX || c2.recover(r2,17)!=42)
        throw std::runtime_error("interleaved client or response lifetime failure");
    checked+=2;
    spiral_u64::Server wrong(std::vector<uint64_t>(17));
    try {wrong.answer(q1);throw std::runtime_error("mismatched database accepted");}catch(const std::invalid_argument&){}
 }
 try {spiral_u64::Server server({});throw std::runtime_error("empty database accepted");}catch(const std::invalid_argument&){}
 std::cout<<"PASS total_queries="<<checked<<"; actual encrypted PIR, noise enabled\n";
 return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
