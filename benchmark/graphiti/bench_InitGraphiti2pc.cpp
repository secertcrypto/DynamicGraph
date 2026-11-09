// Real shared-endpoint initialization and secret order export, exactly two parties.
#include "benchmark/graphiti/common.h"
#include "src/2pc/protocol.h"
#include <iostream>
using namespace graphiti_bench;
namespace twopc=graphiti::twopc;
struct Counters {
    int id;twopc::Initializer& init;twopc::detail::Protocol& validation;
    int pid() const{return id;}
    int numParties() const{return 2;}
    uint64_t bytesSentTo(int p) const{return p==1-id?init.sent()+validation.sent():0;}
    uint64_t bytesRecvFrom(int p) const{return p==1-id?init.received()+validation.received():0;}
};
int main(int argc,char** argv) try {
    if(argc==2 && std::string(argv[1])=="--help") {
        std::cout<<"Usage: bench_InitGraphiti2pc --pid 0|1 --protocol two_party --num-parties 2 "
            "(--graph-size N | --num-verts V --num-edges E) [--port P] [--peer IP] [--seed S] [--output FILE] [--no-check]\n";
        return 0;
    }
    auto args=parseArguments(argc,argv,Workload::Init);
    if(args.protocol.kind!=protocol::ProtocolKind::TwoParty)
        throw std::invalid_argument("bench_InitGraphiti2pc requires --protocol two_party");
    if(args.rows()>(size_t{1}<<22))throw std::invalid_argument("V+E must be <= 2^22");
    const auto pid=args.protocol.pid;
    const bool checked=args.check && args.rows()<=100000;
    graphiti::GraphList fixture;
    if(pid==1 || checked)fixture=graphiti::generateRandomGraphList(args.vertices,args.edges,args.seed);
    twopc::Words src(args.rows(),0),dst(args.rows(),0);
    if(pid==1){src=fixture.src;dst=fixture.dst;}
    emp::NetIO io(pid==0?nullptr:args.protocol.peer.c_str(),args.protocol.port,true);
    twopc::Initializer init(pid,io);twopc::detail::Protocol validation(pid,io);
    Counters counters{pid,init,validation};using Stats=bench::StatsPoint<Counters>;
    Stats begin(counters);
    auto state=init.Init(args.vertices,src,dst,twopc::IndexSharing::Ring64);
    auto labels=init.ExportOrderShares(state);
    const auto online=Stats(counters)-begin;
    Stats idle(counters);auto empty=idle-idle;
    bool correct=true;
    Stats check_start(counters);
    if(checked) {
        auto orders=graphiti::computeGraphOrderPermutations(fixture);
        const auto expected=std::vector<twopc::Words>{
            graphiti::destinationLabelsFromPull<Word>(orders.vertex_to_source),
            graphiti::destinationLabelsFromPull<Word>(orders.source_to_destination),
            graphiti::destinationLabelsFromPull<Word>(orders.destination_to_vertex)};
        std::vector<twopc::Words> actual={labels.vertex_to_source,labels.source_to_destination,labels.destination_to_vertex};
        for(size_t i=0;i<3;++i)if(validation.OpenWords(actual[i])!=expected[i])correct=false;
    }
    auto reconstruction=Stats(counters)-check_start;
    nlohmann::json output={
        {"details",{{"benchmark","bench_InitGraphiti2pc"},{"schema_version",2},
            {"protocol","two_party"},{"pid",pid},{"is_helper",false},{"num_compute_parties",2},
            {"graph_size",args.rows()},{"num_vertices",args.vertices},{"num_edges",args.edges},
            {"seed",args.seed},{"synthetic_sort_model",false},{"online_scope","initialization_and_order_export"},
            {"validation_scope","three secret order labels against plaintext fixture"}}},
        {"correctness_checked",checked},{"correct",checked?nlohmann::json(correct):nlohmann::json(nullptr)},
        {"offline",empty},{"init",empty},{"online",online},{"total",online},{"reconstruction",reconstruction}};
    bench::printPhaseStats(pid,"online",online);
    std::cout<<"P"<<pid<<" init2pc "<<(checked?(correct?"PASS":"FAIL"):"SKIP")<<'\n';
    if(!args.output.empty() && !bench::saveJson(output,args.output))throw std::runtime_error("cannot save JSON");
    return correct?0:1;
} catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
