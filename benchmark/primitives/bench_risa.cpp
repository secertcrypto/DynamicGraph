// Standalone RISA: setup, compile batches, approximate updates and exact reads.
// Public test fixtures only; --check writes shares outside measured phases.
#include "src/risa/read.h"
#include "spiral_u64.h"
#include <nlohmann/json.hpp>
#include <chrono>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>

using json=nlohmann::json;
using Word=risa::Word;
struct Round {
    std::vector<Word> indices,messages;
    size_t reads;
};

int main(int argc,char** argv) try {
    if(argc!=6 || std::string(argv[5])!="--worker") {
        std::cerr<<"Use python3 benchmark/primitives/run_risa.py --database-size N --batch-size B\n";
        return 2;
    }
    int party=std::stoi(argv[1]),port=std::stoi(argv[2]);
    if(party<0 || party>1 || port<1024 || port>65535)throw std::invalid_argument("invalid party/port");
    std::ifstream input(argv[3]);if(!input)throw std::runtime_error("cannot open input");
    json in;input>>in;
    const size_t n=in.at("database_size");
    if(!n || n>(size_t{1}<<22))throw std::invalid_argument("require 1 <= N <= 2^22");
    std::vector<Word> db=in.at("database").get<std::vector<Word>>();
    if(db.size()!=n)throw std::invalid_argument("wrong database length");
    std::vector<Round> rounds;size_t total=0;
    for(const auto& item:in.at("rounds")) {
        Round row{item.at("indices").get<std::vector<Word>>(),
                  item.at("messages").get<std::vector<Word>>(),item.at("read_count")};
        for(auto index:row.indices)if(index>=n)throw std::invalid_argument("noncanonical index share");
        total+=row.indices.size();
        if(row.messages.size()>total || row.reads>total)throw std::invalid_argument("batch exceeds compiled states");
        rounds.push_back(std::move(row));
    }
    if(rounds.empty())throw std::invalid_argument("no rounds");
    std::ofstream checks;
    if(std::string(argv[4])!="-") {
        checks.open(argv[4]);if(!checks)throw std::runtime_error("cannot open check file");
    }
    std::unique_ptr<emp::NetIO> io;
    std::unique_ptr<risa::Backend> backend;
    std::unique_ptr<risa::ReadContext> read_context;
    risa::UpdateParameters parameters{};
    std::vector<risa::ReadState> read_states;
    std::vector<risa::LwrUpdateState> update_states;
    size_t active=0;
    auto measure=[&](const char* phase,int round,size_t batch,auto&& fn) {
        std::string go;
        if(!std::getline(std::cin,go) || go!="GO")throw std::runtime_error("lost coordinator");
        const Word before=io?io->counter:0;
        const auto start=std::chrono::steady_clock::now();
        fn();
        const auto stop=std::chrono::steady_clock::now();
        std::cout<<"@RISA "<<json{{"pid",party},{"phase",phase},{"round",round},
            {"batch_size",batch},{"active_states",active},{"bytes_sent",io?io->counter-before:0},
            {"ms",std::chrono::duration<double,std::milli>(stop-start).count()},
            {"read_response_bytes",spiral_u64::Response::wire_size()}}.dump()<<std::endl;
    };
    std::cout<<"@RISA READY"<<std::endl;
    measure("connect",-1,0,[&]{
        io=std::make_unique<emp::NetIO>(party==0?nullptr:"127.0.0.1",port);
        backend=std::make_unique<risa::Backend>(party,*io);
    });
    measure("setup_update",-1,0,[&]{parameters=risa::Setup(n);});
    measure("setup_read",-1,0,[&]{read_context=std::make_unique<risa::ReadContext>(*backend,n);});
    for(size_t t=0;t<rounds.size();++t) {
        const auto& row=rounds[t];active+=row.indices.size();
        // Read compilation first, as in the dynamic graph's insertion API.
        measure("compile_read",int(t),row.indices.size(),[&]{
            for(auto index:row.indices)read_states.push_back(risa::CompileRead(*read_context,index));
        });
        measure("compile_update",int(t),row.indices.size(),[&]{
            for(auto index:row.indices)update_states.push_back(risa::Compile(*backend,parameters,index));
        });
        risa::ApproximateContribution delta;
        measure("update",int(t),row.messages.size(),[&]{
            std::vector<risa::BatchInput> batch;batch.reserve(row.messages.size());
            for(size_t i=0;i<row.messages.size();++i)batch.push_back({update_states[i],row.messages[i]});
            delta=risa::UpdateBatchGather(*backend,parameters,t,batch);
        });
        measure("apply_update",int(t),row.messages.size(),[&]{
            for(size_t x=0;x<n;++x)db[x]+=delta.shares[x];
        });
        std::vector<Word> values;
        measure("read",int(t),row.reads,[&]{
            std::vector<risa::ReadInput> batch;batch.reserve(row.reads);
            for(size_t i=0;i<row.reads;++i)batch.push_back({read_states[i],db});
            values=risa::ReadBatch(*read_context,t,batch);
        });
        if(checks.is_open()) {
            checks<<json{{"round",t},{"delta",delta.shares},{"database",db},
                {"read",values},{"error_bound",delta.max_coordinate_error}}.dump()<<'\n';
            if(!checks)throw std::runtime_error("cannot write check file");
        }
    }
    return 0;
} catch(const std::exception& ex){std::cerr<<ex.what()<<'\n';return 1;}
