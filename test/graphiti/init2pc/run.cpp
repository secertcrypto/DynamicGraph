#include "src/2pc/init.h"
#include <nlohmann/json.hpp>
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace graphiti::twopc;
using json=nlohmann::json;
int main(int argc,char** argv) try {
    if(argc!=5 && argc!=6) {
        std::cerr<<"usage: graphiti_init2pc_run PID PORT INPUT.json OUTPUT.json [P0_IP]\n";return 2;
    }
    const int pid=std::stoi(argv[1]),port=std::stoi(argv[2]);
    if((pid!=0 && pid!=1)||port<1024||port>65535)throw std::invalid_argument("bad party/port");
    std::ifstream in(argv[3]);if(!in)throw std::runtime_error("input unavailable");
    json input;in>>input;
    auto src=input.at("src").get<Words>(),dst=input.at("dst").get<Words>();
    auto batches=input.at("payloads").get<std::vector<Rows>>();
    const auto vertices=input.at("vertices").get<std::size_t>();
    auto mode=input.value("index_sharing",std::string("mod_vertices"));
    if(mode!="mod_vertices" && mode!="ring64")throw std::invalid_argument("unknown index sharing");
    emp::NetIO io(pid==0?nullptr:(argc==6?argv[5]:"127.0.0.1"),port,true);
    Initializer init(pid,io);
    auto t0=std::chrono::steady_clock::now();
    auto state=init.Init(vertices,src,dst,
        mode=="ring64"?IndexSharing::Ring64:IndexSharing::ModuloVertices);
    auto t1=std::chrono::steady_clock::now();
    json output;
    output["init_ms"]=std::chrono::duration<double,std::milli>(t1-t0).count();
    output["init_sent_bytes"]=io.counter;
    output["source_mapping"]=state.source_mapping();
    output["destination_mapping"]=state.destination_mapping();
    output["return_mapping_share"]=state.return_mapping_share();
    auto labels=init.ExportOrderShares(state);
    output["order_shares"]={labels.vertex_to_source,labels.source_to_destination,labels.destination_to_vertex};
    output["rounds"]=json::array();
    for(auto& batch:batches) {
        auto before=io.counter;
        auto start=std::chrono::steady_clock::now();
        auto source=init.VertexToSource(state,batch);
        auto destination=init.SourceToDestination(state,source);
        auto vertex=init.DestinationToVertex(state,destination);
        auto end=std::chrono::steady_clock::now();
        output["rounds"].push_back({{"source",source},{"destination",destination},{"vertex",vertex},
            {"ms",std::chrono::duration<double,std::milli>(end-start).count()},
            {"sent_bytes",io.counter-before}});
    }
    // Only local shares are written. Reconstruction is confined to check.py's public fixtures.
    std::ofstream out(argv[4]);if(!out)throw std::runtime_error("output unavailable");
    out<<output.dump()<<'\n';
    std::cout<<"PASS graphiti_init2pc P"<<pid<<" init_ms="<<output["init_ms"]<<" sent_bytes="<<io.counter<<'\n';
    return 0;
} catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
