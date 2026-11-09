#pragma once
#include "dynamic_graph/delta_updates.h"
#include "risa/read.h"

namespace dynamic_graph {
// Each public handle owns independently compiled source and destination states.
// Execute all calls in the same public order on the two compute parties.
class DeltaAccess {
    risa::ReadContext reads_;
    LwrDeltaUpdates updates_;
    std::map<uint64_t,risa::ReadState> sources_;
public:
    DeltaAccess(risa::Backend& net,uint64_t vertices,unsigned value_bits=64):reads_(net,vertices,value_bits),updates_(net,vertices){}
    void insert(uint64_t edge,risa::Word src_share,risa::Word dst_share) {
        if(sources_.count(edge))throw std::invalid_argument("duplicate delta edge");
        if(src_share>=reads_.size() || dst_share>=reads_.size())throw std::invalid_argument("invalid endpoint share");
        auto st=risa::CompileRead(reads_,src_share);
        updates_.insert(edge,dst_share);
        sources_.emplace(edge,std::move(st));
    }
    void erase(uint64_t edge) {
        updates_.erase(edge);sources_.erase(edge);
    }
    std::map<uint64_t,risa::Word> read_sources(uint64_t t,const std::vector<risa::Word>& snapshot) {
        std::vector<risa::ReadInput> batch;
        for(auto& edge:sources_)batch.push_back({edge.second,snapshot});
        auto values=risa::ReadBatch(reads_,t,batch);
        std::map<uint64_t,risa::Word> out;
        size_t i=0;for(const auto& edge:sources_)out.emplace(edge.first,values[i++]);
        return out;
    }
    risa::ApproximateContribution gather(uint64_t t,const std::map<uint64_t,risa::Word>& messages) {
        return updates_.aggregate(t,messages);
    }
};
}
