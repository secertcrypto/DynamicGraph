// Full-value openings ONLY for public correctness fixtures.
#include "src/dynamic_graph/engine.h"
#include "src/common/protocol_runner.h"
#include <iostream>
using namespace dynamic_graph;
int main(int argc,char** argv){
    try{
        if(argc!=4)return 2;
        int pid=std::stoi(argv[1]);
        threepc::protocol::ProtocolConfig cfg;
        cfg.pid=pid;cfg.kind=threepc::protocol::parseProtocolKind(argv[2]);cfg.num_compute_parties=2;cfg.port=std::stoi(argv[3]);
        Circuit c; std::vector<Wire> owned; std::vector<Word> vals,wants,masks;
        auto shared=[&](Word share){
            auto a=c.newInputWire(0),b=c.newInputWire(1);
            if(pid<2){owned.push_back(pid?b:a);vals.push_back(share);}
            return c.addGate(threepc::GateType::kAdd,a,b);
        };
        // Equality S==2Dmax is insufficient (eta=+Dmax crosses the rounding boundary).
        bool invalid=false;
        try{ Encoding bad(2,2); }catch(const std::invalid_argument&){invalid=true;}
        if(!invalid)return 1;
        size_t naive_fails=0;
        for(unsigned ell:{1u,3u,4u,63u}){
            Word S=Word{1}<<ell,D=S/2-1;
            Encoding enc(D,ell);
            for(Word msg:{Word{0},Word{1}, enc.value_mask(), enc.value_mask()/2})
                for(Word noise:{Word{0}-D, Word{0},D})
                    for(Word p0:{S/2-1,~Word{0},S+Word{2}}){
                        Word y=enc.encode(msg)+noise;
                        Word w0=p0+S/2,w1=y-p0;
                        Word local=pid==0?w0:pid==1?w1:0;
                        auto hi=shared(local>>ell),lo=shared(local&(S-1));
                        c.setAsOutput(decode_parts(c,enc,hi,lo));
                        wants.push_back(msg);masks.push_back(enc.value_mask());
                        naive_fails+=(((w0>>ell)+(w1>>ell))&enc.value_mask())!=msg;
                    }
        }
        if(!naive_fails)return 1;
        auto lc=c.orderGatesByLevel();auto runner=threepc::protocol::makeProtocolRunner<Word>(cfg);
        if(pid<2)runner->setInputs(owned,vals);
        runner->offline(lc);runner->online(lc);auto out=runner->getOutputs(lc);
        if(pid<2){
            if(out.size()!=wants.size())return 1;
            for(size_t i=0;i<out.size();++i)if((out[i]&masks[i])!=wants[i])return 1;
        }
        std::cout<<"Graphiti module PASS P"<<pid<<" secure decode cases="<<wants.size()
                 <<" independent shifts would fail="<<naive_fails<<'\n';
    }catch(const std::exception& ex){std::cerr<<ex.what()<<'\n';return 1;}
}
