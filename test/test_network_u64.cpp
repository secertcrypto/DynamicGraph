#include <emp-tool/emp-tool.h>
#include <array>
#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

static void require(bool ok) {if(!ok)throw std::runtime_error("network uint64 check failed");}
class Loopback : public emp::IOChannel<Loopback> {
public:
    std::vector<uint8_t> bytes;
    size_t position=0;
    void send_data_internal(const void* ptr,uint64_t n) {
        if(n) {auto p=static_cast<const uint8_t*>(ptr);bytes.insert(bytes.end(),p,p+emp::checked_io_size(n));}
    }
    void recv_data_internal(void* ptr,uint64_t n) {
        require(n<=bytes.size()-position);
        if(n)std::memcpy(ptr,bytes.data()+position,emp::checked_io_size(n));
        position+=n;
    }
};
class LengthProbe : public emp::IOChannel<LengthProbe> {
public:
    uint64_t sent=0,received=0;
    void send_data_internal(const void*,uint64_t n) {sent=n;}
    void recv_data_internal(void*,uint64_t n) {received=n;}
};
template<class F> void rejects(F fn) {
    try{fn();}catch(const std::length_error&){return;}
    throw std::runtime_error("invalid transfer length accepted");
}
int main() try {
    LengthProbe probe;
    if(std::numeric_limits<size_t>::max()>std::numeric_limits<uint32_t>::max()) {
        const uint64_t large=(uint64_t{1}<<32)+17;
        probe.send_data(nullptr,large);probe.recv_data(nullptr,large);
        require(probe.sent==large && probe.received==large && probe.counter==large);
    }
    const uint64_t before=probe.counter;
    rejects([&]{probe.send_block(nullptr,std::numeric_limits<uint64_t>::max());});
    rejects([&]{probe.recv_block(nullptr,std::numeric_limits<uint64_t>::max());});
    require(probe.counter==before);
    for(size_t offset=0;offset<8;++offset)for(uint64_t length:{0,1,7,8,17,33}) {
        alignas(8) std::array<bool,48> input{},output{};
        for(size_t i=0;i<input.size();++i)input[i]=i%3==0;
        Loopback io;io.send_bool(input.data()+offset,length);io.recv_bool(output.data()+offset,length);
        for(uint64_t i=0;i<length;++i)require(input[offset+i]==output[offset+i]);
        require(io.counter==io.position && io.position==io.bytes.size());
    }
    Loopback blocks;
    emp::block source[2]{emp::makeBlock(1,2),emp::makeBlock(3,4)},destination[2];
    blocks.send_block(source,2);blocks.recv_block(destination,2);
    require(std::memcmp(source,destination,sizeof(source))==0);
    emp::Group group;emp::BigInt scalar;group.get_rand_bn(scalar);
    auto point=group.mul_gen(scalar);emp::Point recovered;
    Loopback points;points.send_pt(&point);
    const uint64_t length=point.size();
    require(points.bytes.size()==8+length);
    for(unsigned i=0;i<8;++i)require(points.bytes[i]==static_cast<uint8_t>(length>>(8*i)));
    points.recv_pt(&group,&recovered);require(point==recovered);
    require(points.counter==points.position);
    Loopback malformed;malformed.bytes.assign(8,0);malformed.bytes[0]=1;malformed.bytes[1]=8;
    rejects([&]{malformed.recv_pt(&group,&recovered);});
    std::cout<<"PASS uint64 transport lengths, point framing and payload round trips\n";
} catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
