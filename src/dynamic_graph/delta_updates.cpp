#include "delta_updates.h"
#include <stdexcept>
#include <utility>

namespace dynamic_graph {

LwrDeltaUpdates::LwrDeltaUpdates(risa::Backend& backend, std::uint64_t vertices)
    : backend_(backend), pp_(risa::Setup(vertices)) {}

void LwrDeltaUpdates::insert(std::uint64_t edge, risa::Word destination) {
    if (states_.count(edge)) throw std::invalid_argument("duplicate delta edge ID");
    states_.emplace(edge, risa::Compile(backend_, pp_, destination));
}

void LwrDeltaUpdates::erase(std::uint64_t edge) {
    if (states_.erase(edge) != 1) throw std::invalid_argument("unknown delta edge ID");
}

risa::ApproximateContribution LwrDeltaUpdates::aggregate(
        std::uint64_t t, const std::map<std::uint64_t, risa::Word>& messages) {
    if (used_ && t <= last_iteration_)
        throw std::logic_error("graph iteration must increase strictly");
    if (messages.size() != states_.size())
        throw std::invalid_argument("provide one message per active delta edge");
    // Validate the full batch before consuming any state or sending messages.
    for (const auto& entry : states_)
        if (!messages.count(entry.first) || !entry.second.can_update(t))
            throw std::invalid_argument("delta edge/state mismatch");
    std::vector<risa::BatchInput> batch;
    batch.reserve(states_.size());
    for (auto& entry : states_) batch.push_back({entry.second, messages.at(entry.first)});
    used_ = true;
    last_iteration_ = t;
    return risa::UpdateBatchGather(backend_, pp_, t, batch);
}

risa::ApproximateContribution combine_base(const std::vector<risa::Word>& base,
                                          risa::ApproximateContribution delta) {
    if (base.size() != delta.shares.size())
        throw std::invalid_argument("base and delta must have the same vertex indexing and size");
    for (std::size_t x = 0; x < base.size(); ++x) delta.shares[x] += base[x];
    return delta;
}

}  // namespace dynamic_graph
