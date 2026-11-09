#pragma once

#include "sfssgen/backend.h"
#include <cstdint>
#include <memory>
#include <vector>

namespace risa {

using Word = std::uint64_t;
using Backend = dfss_minimal::Backend;

// This concrete implementation uses R=G=Z/(2^64) and the existing LWR
// key group (Z/(2^128))^512. Setup does not assert a concrete security level.
struct UpdateParameters { std::uint64_t size; };
UpdateParameters Setup(std::uint64_t size);

// Shares of Delta + eta, NOT an exact sparse update. The modular distance of
// each reconstructed coordinate from Delta is at most max_coordinate_error.
struct ApproximateContribution {
    std::vector<Word> shares;
    std::uint64_t max_coordinate_error = 0;
};

namespace detail { class BatchEvaluator; }

class LwrUpdateState {
public:
    ~LwrUpdateState();
    LwrUpdateState(LwrUpdateState&&) noexcept;
    LwrUpdateState& operator=(LwrUpdateState&&) noexcept;
    LwrUpdateState(const LwrUpdateState&) = delete;
    LwrUpdateState& operator=(const LwrUpdateState&) = delete;
    std::uint64_t size() const;
    bool can_update(std::uint64_t invocation) const;

private:
    friend class detail::BatchEvaluator;
    struct Impl;
    explicit LwrUpdateState(std::unique_ptr<Impl>);
    std::unique_ptr<Impl> impl_;
    friend LwrUpdateState Compile(Backend&, const UpdateParameters&, Word);
    friend ApproximateContribution Update(Backend&, LwrUpdateState&, std::uint64_t, Word);
};

// Each party supplies ONLY its own canonical alpha share in [0,N).
// Reconstructs alpha=(alpha_0+alpha_1) mod N INSIDE secure bit operations;
// no index, plaintext message, or output vector is opened by this module.
LwrUpdateState Compile(Backend&, const UpdateParameters&, Word index_share);

// Both parties call with the same public t, in the same state/edge order.
// t must increase strictly for each state (first t may be zero).
// Exactly one uint64 masked-message share is sent and received per invocation.
ApproximateContribution Update(Backend&, LwrUpdateState&, std::uint64_t t,
                               Word message_share);

// Caller supplies the same public edge order and t to both parties. Each
// entry represents an independently compiled single-point state, exactly once.
struct BatchInput {
    LwrUpdateState& state;
    Word message_share;
};

std::vector<ApproximateContribution> UpdateBatch(
    Backend&, const UpdateParameters&, std::uint64_t t, const std::vector<BatchInput>&);
ApproximateContribution UpdateBatchGather(
    Backend&, const UpdateParameters&, std::uint64_t t, const std::vector<BatchInput>&);

}  // namespace risa
