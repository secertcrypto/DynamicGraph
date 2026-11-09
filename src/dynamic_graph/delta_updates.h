#pragma once

#include "risa/update.h"
#include <map>

namespace dynamic_graph {

// Update side of a fixed-vertex dynamic graph. Edge IDs/counts, operation
// order and iteration IDs are public. Endpoints and messages stay shared.
// This layer intentionally exposes LWR's approximate output type.
class LwrDeltaUpdates {
public:
    LwrDeltaUpdates(risa::Backend& backend, std::uint64_t vertices);
    LwrDeltaUpdates(const LwrDeltaUpdates&) = delete;
    LwrDeltaUpdates& operator=(const LwrDeltaUpdates&) = delete;

    void insert(std::uint64_t edge_id, risa::Word destination_index_share);
    void erase(std::uint64_t edge_id);
    std::size_t size() const { return states_.size(); }

    // Supply one local ApplyE message share for every active delta edge.
    // Both parties traverse the same public sorted edge IDs. States are reused.
    risa::ApproximateContribution aggregate(
        std::uint64_t iteration,
        const std::map<std::uint64_t, risa::Word>& message_shares);

private:
    risa::Backend& backend_;
    risa::UpdateParameters pp_;
    std::map<std::uint64_t, risa::LwrUpdateState> states_;
    bool used_ = false;
    std::uint64_t last_iteration_ = 0;
};

// Coordinate-wise combination in vertex order, before ApplyV. Preserves the
// delta error bound, assuming the base aggregation shares themselves are exact.
risa::ApproximateContribution combine_base(
    const std::vector<risa::Word>& base_shares,
    risa::ApproximateContribution delta);

}  // namespace dynamic_graph
