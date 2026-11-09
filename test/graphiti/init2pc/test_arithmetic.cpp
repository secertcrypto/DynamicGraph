#include "src/2pc/evaluator.h"
#include "src/2pc/protocol.h"
#include <iostream>
#include <random>
#include <stdexcept>

using namespace graphiti::twopc;
using namespace threepc;
int main(int argc,char** argv) try {
    if(argc!=3)return 2;
    int pid=std::stoi(argv[1]);emp::NetIO io(pid==0?nullptr:"127.0.0.1",std::stoi(argv[2]),true);
    graphiti::twopc::detail::Protocol protocol(pid,io);
    Words truth={0,1,2,~Word{0},Word{1}<<63,(Word{1}<<63)-1};
    std::mt19937_64 fixture(731);
    for(int i=0;i<32;++i)truth.push_back(fixture());
    Words x,y,expected;
    for(auto a:truth)for(auto b:truth) {
        Word r=fixture(),s=fixture();
        x.push_back(pid==0?r:a-r);y.push_back(pid==0?s:b-s);expected.push_back(a*b);
    }
    if(protocol.OpenWords(protocol.Multiply(x,y))!=expected)throw std::runtime_error("OT multiplication");
    Words zeros;
    for(auto a:truth)for(auto b:truth){(void)b;zeros.push_back(a==0);}
    if(protocol.OpenWords(protocol.EqualZero(x))!=zeros)throw std::runtime_error("secret Eqz");
    // Independent circuit oracle, repeated with fresh circuits on one evaluator.
    Evaluator evaluator(pid,io);
    for(unsigned ell:{0u,1u,4u,60u,63u}) {
        Circuit<Word> c;std::vector<wire_t> inputs,xs;Words values,reference;
        const Word mask=~Word{0}>>ell;
        for(auto a:truth) {
            auto i0=c.newInputWire(0),i1=c.newInputWire(1);Word r=fixture();
            inputs.push_back(pid==0?i0:i1);values.push_back(pid==0?r:a-r);
            auto x=c.addGate(GateType::kAdd,i0,i1);xs.push_back(x);
            auto scaled=c.addCGate(GateType::kCMul,x,Word{1}<<ell);
            c.setAsOutput(c.addEqzGate(scaled));reference.push_back((a&mask)==0);
            c.setAsOutput(c.addLtzGate(scaled));reference.push_back(((a<<ell)>>63)&1);
            c.setAsOutput(c.addGate(GateType::kMul,x,x));reference.push_back(a*a);
        }
        auto only0=c.addRecPGate(xs[1],0),only1=c.addRecPGate(xs[1],1);
        auto group=c.freshPermGroupId();
        auto shuffled=c.addShuffleGate(xs,group);
        auto restored=c.addUnshuffleGate(shuffled,group);
        for(auto w:restored)c.setAsOutput(w);
        reference.insert(reference.end(),truth.begin(),truth.end());
        auto ordered=c.orderGatesByLevel();evaluator.prepare(ordered);evaluator.setInputs(inputs,values);
        for(size_t d=0;d<ordered.depth();++d)evaluator.evalLevel(d,ordered);
        if(evaluator.share(only0)!=(pid==0?truth[1]:0) || evaluator.share(only1)!=(pid==1?truth[1]:0))
            throw std::runtime_error("targeted opening");
        if(evaluator.outputs(ordered)!=reference)throw std::runtime_error("two-party circuit arithmetic/ring projection");
    }
    std::cout<<"PASS two-party arithmetic P"<<pid<<'\n';
    return 0;
} catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
