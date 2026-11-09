// Benchmark-only driver. Control messages go through pipes, not MPC channels.
#include "src/dynamic_graph/engine.h"
#include <nlohmann/json.hpp>
#include <array>
#include <chrono>
#include <fstream>
#include <iostream>
#include <limits>
#include <string>

namespace dg = dynamic_graph;
using json=nlohmann::json;
struct Edge {dg::Word id,src,dst,data;};

int main(int argc,char** argv) try {
    if(argc!=6) {std::cerr<<"usage: bench_dynamic_graph PID BASE_PORT INPUT.json SHARE_OUTPUT|- --worker\n";return 2;}
    if(std::string(argv[5])!="--worker") return 2;
    int pid=std::stoi(argv[1]),port=std::stoi(argv[2]);
    if(pid<0 || pid>2 || port<1024 || port>65535-2049) throw std::invalid_argument("bad PID/port");
    std::ifstream f(argv[3]); if(!f)throw std::runtime_error("cannot read input");
    json in;f>>in;
    const auto v=in.at("vertices").get<size_t>(), e=in.at("base_edges").get<size_t>();
    if(!v || v>(uint64_t{1}<<22) || e>size_t(std::numeric_limits<int>::max())-v)
        throw std::invalid_argument("invalid graph size");
    dg::Config cfg{pid,v,in.at("max_delta").get<dg::Word>(),port,port+50,"127.0.0.1",in.at("shift").get<unsigned>()};
    cfg.two_party_init=in.value("two_party_init",false);
    const auto backend=in.value("backend",std::string("nph"));
    if(backend!="nph" && backend!="two_party")throw std::invalid_argument("unknown runtime backend");
    cfg.two_party=backend=="two_party";
    dg::BaseDagShare base;
    if(pid==2){base.src.resize(v+e);base.dst.resize(v+e);base.data.resize(v+e);}
    else {
        const auto& rows=in.at("base_rows");if(rows.size()!=v+e)throw std::invalid_argument("wrong base size");
        for(const auto& r:rows){base.src.push_back(r.at(0));base.dst.push_back(r.at(1));base.data.push_back(r.at(2));}
    }
    std::vector<std::vector<Edge>> rounds;
    for(const auto& r:in.at("rounds")){
        rounds.emplace_back();
        for(const auto& x:r.at("insert")){
            Edge edge{x.at("id").get<dg::Word>(),0,0,0};
            if(pid!=2){edge.src=x.at("src");edge.dst=x.at("dst");edge.data=x.at("data");}
            rounds.back().push_back(edge);
        }
    }
    const auto app_name=in.at("application").get<std::string>();
    if(app_name!="weighted_sum" && app_name!="reachability")throw std::invalid_argument("unknown application");
    auto program=app_name=="weighted_sum"?dg::Program::weighted_sum():dg::Program::reachability();
    std::ofstream shares;
    if(std::string(argv[4])!="-"){shares.open(argv[4]);if(!shares)throw std::runtime_error("cannot write check file");}
    std::unique_ptr<dg::Engine> engine;
    auto measure=[&](const char* phase,int round,size_t count,auto&& fn){
        std::string go;
        if(!std::getline(std::cin,go)||go!="GO")throw std::runtime_error("lost benchmark coordinator");
        const auto before=engine?engine->traffic():dg::Traffic{};
        const auto start=std::chrono::steady_clock::now();
        fn();
        const auto stop=std::chrono::steady_clock::now();
        auto after=engine->traffic();
        for(size_t i=0;i<3;i++){after.nph_tx[i]-=before.nph_tx[i];after.nph_rx[i]-=before.nph_rx[i];}
        after.risa_tx-=before.risa_tx;
        after.two_party_tx-=before.two_party_tx;
        if(shares.is_open() && std::string(phase)=="iter") {
            shares<<json(engine->vertex_shares()).dump()<<'\n';if(!shares)throw std::runtime_error("write check file failed");
        }
        std::cout<<"@DG "<<json{{"pid",pid},{"phase",phase},{"round",round},{"inserted",count},
            {"active",engine->delta_count()},{"ms",std::chrono::duration<double,std::milli>(stop-start).count()},
            {"nph_tx",after.nph_tx},{"nph_rx",after.nph_rx},{"risa_tx",after.risa_tx},{"two_party_tx",after.two_party_tx}}.dump()<<std::endl;
    };
    std::cout<<"@DG READY"<<std::endl;
    measure("init",-1,0,[&]{engine=dg::Engine::Init(cfg,std::move(base),std::move(program));});
    for(size_t t=0;t<rounds.size();++t){
        measure("insert",int(t),rounds[t].size(),[&]{for(const auto& x:rounds[t])engine->Insert(x.id,x.src,x.dst,x.data);});
        measure("iter",int(t),0,[&]{engine->Iter();});
    }
    return 0;
} catch(const std::exception& ex){std::cerr<<ex.what()<<'\n';return 1;}
