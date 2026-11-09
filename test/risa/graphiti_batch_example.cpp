// Public regression fixture: base edge 0->1, two delta edges targeting vertex 0.
// Expected final vertex values: [3+30, 4+3] before LWR errors.
#include "dynamic_graph/nph_delta_input.h"
#include <iostream>
#include <optional>
int main(int argc,char** argv) {
    try {
        if (argc!=4 || std::string(argv[2])!="nph") return 2;
        threepc::protocol::ProtocolConfig cfg;
        cfg.pid=std::stoi(argv[1]); cfg.port=std::stoi(argv[3]);
        cfg.kind=threepc::protocol::ProtocolKind::Nph; cfg.num_compute_parties=2;
        if (cfg.pid<0 || cfg.pid>2 || cfg.port<1024 || cfg.port>65400) return 2;
        using namespace threepc;
        Circuit<risa::Word> c;
        std::vector<std::pair<graphiti::Wires,std::vector<risa::Word>>> p1_inputs;
        auto p1=[&](std::vector<risa::Word> values) {
            graphiti::Wires wires;
            for (size_t i=0;i<values.size();++i) wires.push_back(c.newInputWire(P1));
            p1_inputs.emplace_back(wires,std::move(values));
            return wires;
        };
        graphiti::MessagePassing<risa::Word> graph(c,2,1);
        // Vertex rows [v0,v1,e01]; source and destination order [v0,e01,v1].
        graph.init(p1({3,4}),{p1({0,2,1}),p1({0,1,2}),p1({0,2,1})});
        dynamic_graph::NphDeltaInput input(c,graph.aggregate());
        graph.applyVertices(input.combined(),[](auto& c,size_t,wire_t old,wire_t a){
            return c.addGate(GateType::kAdd,old,a);
        });
        for (auto w:graph.vertices()) c.setAsOutput(w);
        auto circuit=c.orderGatesByLevel();
        auto runner=protocol::makeProtocolRunner<risa::Word>(cfg);
        if (cfg.pid==1) for (const auto& item:p1_inputs) runner->setInputs(item.first,item.second);
        std::optional<risa::ApproximateContribution> delta;
        if (!runner->isHelper()) {
            emp::NetIO io(cfg.pid==0 ? nullptr : "127.0.0.1",cfg.port+50);
            risa::Backend net(cfg.pid,io);
            auto pp=risa::Setup(2);
            auto a=risa::Compile(net,pp,1), b=risa::Compile(net,pp,1);
            delta=risa::UpdateBatchGather(net,pp,0,{{a,cfg.pid?9u:1u},{b,cfg.pid?18u:2u}});
        }
        input.stage(*runner,delta ? &*delta : nullptr);
        runner->offline(circuit); runner->online(circuit);
        auto result=runner->getOutputs(circuit); // public fixture only
        if (!runner->isHelper()) {
            if (result.size()!=2) return 1;
            for (size_t i=0;i<2;++i) {
                auto error=result[i]-(i==0?33u:7u);
                if (error>2 && risa::Word{0}-error>2) return 1;
            }
        }
        std::cout<<"Graphiti module PASS P"<<cfg.pid<<" (RISA batch + NPH ApplyV)\n";
    } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
