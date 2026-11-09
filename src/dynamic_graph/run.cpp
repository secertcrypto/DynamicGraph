// Each process reads its own local JSON file. Never opens graph outputs.
#include "engine.h"
#include <nlohmann/json.hpp>
#include <climits>
#include <fstream>
#include <iostream>
using namespace dynamic_graph;
using json=nlohmann::json;
int main(int argc,char** argv){
    try{
        if(argc!=5){std::cerr<<"usage: dynamic_graph_run PARTY BASE_PORT LOCAL_INPUT.json LOCAL_OUTPUT.jsonl\n";return 2;}
        std::ifstream file(argv[3]);if(!file)throw std::runtime_error("cannot read local graph input");
        json in;file>>in;int pid=std::stoi(argv[1]),port=std::stoi(argv[2]);
        if(port<1024 || port>65535-2049)throw std::invalid_argument("invalid base port");
        auto v=in.at("vertices").get<size_t>(),e=in.at("base_edges").get<size_t>();
        if(!v || v>(Word{1}<<22) || e>INT_MAX-v)throw std::invalid_argument("bad graph dimensions");
        Config cfg{pid,v,in.at("max_delta").get<Word>(),port,port+50,
                   in.value("peer",std::string("127.0.0.1")),in.value("shift",0u)};
        cfg.two_party_init=in.value("two_party_init",false);
    const auto backend=in.value("backend",std::string("nph"));
    if(backend!="nph" && backend!="two_party")throw std::invalid_argument("unknown runtime backend");
    cfg.two_party=backend=="two_party";
        BaseDagShare base;
        if(pid!=2){
            const auto& rows=in.at("base_rows");
            if(rows.size()!=v+e)throw std::invalid_argument("wrong number of base rows");
            for(const auto& row:rows){
                base.src.push_back(row.at(0).get<Word>());base.dst.push_back(row.at(1).get<Word>());
                base.data.push_back(row.at(2).get<Word>());
            }
        }else{
            base.src.resize(v+e);base.dst.resize(v+e);base.data.resize(v+e);
        }
        std::ofstream out(argv[4]);if(!out)throw std::runtime_error("cannot write local graph output");
        auto name=in.value("application",std::string("weighted_sum"));
        if(name!="weighted_sum" && name!="reachability")
            throw std::invalid_argument("application must be weighted_sum or reachability");
        auto app=name=="weighted_sum"?Program::weighted_sum():Program::reachability();
        auto graph=Engine::Init(cfg,std::move(base),std::move(app));
        for(const auto& round:in.at("rounds")){
            for(const auto& edge:round.at("insert")){
                graph->Insert(edge.at("id").get<Word>(),pid==2?0:edge.at("src").get<Word>(),
                    pid==2?0:edge.at("dst").get<Word>(),pid==2?0:edge.at("data").get<Word>());
            }
            graph->Iter();
            out<<json{{"iteration",graph->iteration()},{"active_edges",graph->delta_count()},
                {"vertex_shares",graph->vertex_shares()}}.dump()<<'\n';
        }
        std::cout<<"PASS dynamic_graph_run P"<<pid<<'\n';
    }catch(const std::exception& ex){std::cerr<<ex.what()<<'\n';return 1;}
}
