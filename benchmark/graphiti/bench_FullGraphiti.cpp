// Real shared-topology initialization followed by persistent two-party iterations.
// The launcher supplies additive shares of a public benchmark fixture.
#include "src/2pc/init.h"
#include "src/2pc/evaluator.h"
#include "src/graphiti/ring_program.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <unordered_set>

namespace app=graphiti::application;
namespace pc=graphiti::twopc;
using json=nlohmann::json;
using app::Word;
using app::Wire;
using Wires=std::vector<Wire>;

namespace {
struct Edge {Word id,src,dst,data;};

class Phase {
    int party_;
    pc::Evaluator& evaluator_;
    Wires inputs_;
    pc::Words values_;
public:
    app::Circuit circuit;
    Phase(int party,pc::Evaluator& evaluator):party_(party),evaluator_(evaluator){}
    Wire shared(Word value) {
        auto a=circuit.newInputWire(0),b=circuit.newInputWire(1);
        inputs_.push_back(party_==0?a:b);values_.push_back(value);
        return circuit.addGate(threepc::GateType::kAdd,a,b);
    }
    pc::Words run(const Wires& outputs) {
        auto ordered=circuit.orderGatesByLevel();
        evaluator_.prepare(ordered);evaluator_.setInputs(inputs_,values_);
        for(size_t i=0;i<ordered.depth();++i)evaluator_.evalLevel(i,ordered);
        pc::Words result;
        for(auto w:outputs)result.push_back(evaluator_.share(w));
        return result;
    }
};

class StaticGraph {
    int party_;
    size_t vertices_,rows_;
    app::Encoding encoding_;
    app::Program program_;
    pc::Initializer initializer_;
    pc::InitState state_;
    pc::Evaluator evaluator_;
    pc::Words src_,dst_,data_,values_,weights_;
    void initialize() {
        rows_=data_.size();
        state_=initializer_.Init(vertices_,src_,dst_);
        pc::Rows weights(rows_,pc::Words(1));
        for(size_t i=0;i<rows_;++i)weights[i][0]=data_[i];
        auto aligned=initializer_.VertexToSource(state_,std::move(weights));
        weights_.clear();weights_.reserve(rows_);
        for(const auto& row:aligned)weights_.push_back(row[0]);
    }
public:
    StaticGraph(int party,emp::NetIO& io,size_t vertices,const pc::Words& src,
                const pc::Words& dst,const pc::Words& data,
                app::Encoding encoding,app::Program program)
        :party_(party),vertices_(vertices),rows_(data.size()),encoding_(encoding),
         program_(std::move(program)),initializer_(party,io),evaluator_(party,io),
         src_(src),dst_(dst),data_(data) {
        values_.assign(data.begin(),data.begin()+vertices);
        initialize();
    }
    void insert_and_reinitialize(const std::vector<Edge>& additions,bool force) {
        if(additions.empty() && !force)return;
        // Keep the latest secret vertex state and every old edge. Only topology
        // preprocessing is rebuilt; connections and OT contexts are reusable.
        std::copy(values_.begin(),values_.end(),data_.begin());
        for(const auto& edge:additions) {
            src_.push_back(edge.src);dst_.push_back(edge.dst);
            data_.push_back(edge.data&encoding_.value_mask());
        }
        initialize();
    }
    void iterate() {
        // Propagate: shuffle vertex differences, then take a local prefix sum.
        pc::Rows diff(rows_,pc::Words(1));
        diff[0][0]=values_[0];
        for(size_t i=1;i<vertices_;++i)diff[i][0]=values_[i]-values_[i-1];
        auto source=initializer_.VertexToSource(state_,std::move(diff));
        for(size_t i=1;i<rows_;++i)source[i][0]+=source[i-1][0];
        Phase edge(party_,evaluator_);app::RingOps edge_ops(edge.circuit,encoding_);
        Wires messages;
        for(size_t i=0;i<rows_;++i)
            messages.push_back(program_.apply_e(edge_ops,edge.shared(source[i][0]),edge.shared(weights_[i])));
        auto local=edge.run(messages);
        pc::Rows rows(rows_,pc::Words(1));
        for(size_t i=0;i<rows_;++i)rows[i][0]=local[i];
        auto destination=initializer_.SourceToDestination(state_,std::move(rows));
        // Gather: keep each vertex marker's own message so it can be removed.
        pc::Rows prefix(rows_,pc::Words(2));Word sum=0;
        for(size_t i=0;i<rows_;++i){sum+=destination[i][0];prefix[i]={sum,destination[i][0]};}
        auto vertex=initializer_.DestinationToVertex(state_,std::move(prefix));
        Phase update(party_,evaluator_);app::RingOps vertex_ops(update.circuit,encoding_);
        Wires next;
        for(size_t i=0;i<vertices_;++i) {
            Word aggregate=vertex[i][0]-(i?vertex[i-1][0]:0)-vertex[i][1];
            next.push_back(program_.apply_v(vertex_ops,update.shared(values_[i]),update.shared(aggregate)));
        }
        values_=update.run(next);
        for(auto& value:values_)value&=encoding_.value_mask();
    }
    const pc::Words& values() const {return values_;}
    Word received() const {return initializer_.received()+evaluator_.received();}
};
}

int main(int argc,char** argv) try {
    if(argc!=6 || std::string(argv[5])!="--worker") {
        std::cerr<<"Launch with python3 benchmark/graphiti/run_full.py --vertices V --edges E --rounds K\n";
        return 2;
    }
    const int party=std::stoi(argv[1]),port=std::stoi(argv[2]);
    if(party<0 || party>1 || port<1024 || port>65535)throw std::invalid_argument("bad party/port");
    std::ifstream input(argv[3]);if(!input)throw std::runtime_error("cannot open input");
    json in;input>>in;
    const size_t vertices=in.at("vertices"),edges=in.at("base_edges");
    if(!vertices || vertices>(size_t{1}<<22) || edges>(size_t{1}<<22)-vertices)
        throw std::invalid_argument("require 1 <= V and V+E <= 2^22");
    if(in.at("backend")!="two_party")throw std::invalid_argument("full Graphiti requires two_party");
    const Word max_delta=in.at("max_delta");
    app::Encoding encoding(max_delta,in.at("shift").get<unsigned>());
    const std::string name=in.at("application");
    if(name!="weighted_sum" && name!="reachability")throw std::invalid_argument("unknown application");
    auto program=name=="weighted_sum"?app::Program::weighted_sum():app::Program::reachability();
    if(program.count_must_fit && edges+max_delta>encoding.value_mask())
        throw std::invalid_argument("reachability count may wrap modulo q");
    pc::Words src,dst,data;
    if(in.at("base_rows").size()!=vertices+edges)throw std::invalid_argument("wrong row count");
    for(const auto& row:in.at("base_rows")) {
        Word s=row.at(0),d=row.at(1),w=row.at(2);
        if(s>=vertices || d>=vertices)throw std::invalid_argument("noncanonical endpoint share");
        src.push_back(s);dst.push_back(d);data.push_back(w&encoding.value_mask());
    }
    const auto mode=in.value("graphiti_mode",std::string("static"));
    const auto policy=in.value("reinit_policy",std::string("every_round"));
    if(mode!="static" && mode!="rebuild")throw std::invalid_argument("unknown Graphiti mode");
    if(policy!="every_round" && policy!="on_insert")throw std::invalid_argument("unknown reinit policy");
    const size_t rounds=in.at("rounds").size();
    if(!rounds)throw std::invalid_argument("require at least one round");
    std::vector<std::vector<Edge>> additions;
    std::unordered_set<Word> ids;
    for(const auto& round:in.at("rounds")) {
        additions.emplace_back();
        if(mode=="static" && !round.at("insert").empty())
            throw std::invalid_argument("static Graphiti does not accept insertions");
        for(const auto& row:round.at("insert")) {
            Edge edge{row.at("id"),row.at("src"),row.at("dst"),row.at("data")};
            if(edge.src>=vertices || edge.dst>=vertices || !ids.insert(edge.id).second)
                throw std::invalid_argument("invalid inserted edge or duplicate ID");
            if(ids.size()>max_delta || ids.size()>(size_t{1}<<22)-vertices-edges)
                throw std::invalid_argument("insertions exceed max-delta or V+E limit");
            additions.back().push_back(edge);
        }
    }
    std::ofstream shares;
    if(std::string(argv[4])!="-") {
        shares.open(argv[4]);if(!shares)throw std::runtime_error("cannot open share output");
    }
    std::unique_ptr<emp::NetIO> io;
    std::unique_ptr<StaticGraph> graph;
    size_t active=0;
    auto measure=[&](const char* phase,int round,size_t inserted,auto&& fn) {
        std::string go;if(!std::getline(std::cin,go)||go!="GO")throw std::runtime_error("lost coordinator");
        Word before=io?io->counter:0,rx_before=graph?graph->received():0;
        auto start=std::chrono::steady_clock::now();fn();auto stop=std::chrono::steady_clock::now();
        Word sent=io->counter-before,received=graph->received()-rx_before;
        if(shares.is_open() && std::string(phase)=="iter") {
            shares<<json(graph->values()).dump()<<'\n';
            if(!shares)throw std::runtime_error("cannot write shares");
        }
        // Same worker protocol and byte schema as the dynamic benchmark.
        std::cout<<"@DG "<<json{{"pid",party},{"phase",phase},{"round",round},{"inserted",inserted},{"active",active},
            {"ms",std::chrono::duration<double,std::milli>(stop-start).count()},
            {"nph_tx",{0,0}},{"nph_rx",{0,0}},{"risa_tx",0},
            {"two_party_tx",sent},{"two_party_rx",received}}.dump()<<std::endl;
    };
    std::cout<<"@DG READY"<<std::endl;
    measure("init",-1,0,[&]{
        io=std::make_unique<emp::NetIO>(party==0?nullptr:"127.0.0.1",port);
        graph=std::make_unique<StaticGraph>(party,*io,vertices,src,dst,data,encoding,std::move(program));
    });
    for(size_t t=0;t<rounds;++t) {
        if(mode=="rebuild")measure("reinit",int(t),additions[t].size(),[&]{
            graph->insert_and_reinitialize(additions[t],policy=="every_round");
            active+=additions[t].size();
        });
        measure("iter",int(t),0,[&]{graph->iterate();});
    }
    return 0;
} catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
