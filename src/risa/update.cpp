#include "update.h"
#include "index_bits.h"
#include "sfssgen/dpf.h"

#include <unordered_set>
#include <stdexcept>
#include <utility>

namespace risa {

UpdateParameters Setup(std::uint64_t size) {
    if (size == 0 || size > (std::uint64_t{1} << 24))
        throw std::invalid_argument("RISA requires 1 <= N <= 2^24 (underlying DPF limit)");
    return {size};
}

namespace detail {
int index_depth(std::uint64_t size) {
    Setup(size);
    int depth = 1;
    while ((std::uint64_t{1} << depth) < size) ++depth;
    return depth;
}

std::vector<std::uint8_t> index_bits_mod_n(Backend& backend,
                                        std::uint64_t size, Word local) {
    const int depth = index_depth(size);
    if (local >= size) throw std::invalid_argument("index share must be canonical modulo N");
    if (size == 1) return {0};
    if ((size & (size - 1)) == 0)
        return backend.arithmetic_to_bits(local, depth);

    // The true integer sum of two canonical shares fits in depth+1 bits.
    // Using only depth bits here would lose a carry before reduction mod N.
    const int width = depth + 1;
    std::vector<std::uint8_t> sum(width), difference(width);
    std::uint8_t carry = 0;
    for (int i = 0; i < width; ++i) {
        const std::uint8_t a = backend.party() == 0 ? ((local >> i) & 1) : 0;
        const std::uint8_t b = backend.party() == 1 ? ((local >> i) & 1) : 0;
        sum[i] = a ^ b ^ carry;
        if (i + 1 < width)
            carry ^= backend.bit_and(a ^ carry, b ^ carry);
    }

    // Add 2^width-N. The final carry is the secret bit [sum >= N].
    const auto complement = (std::uint64_t{1} << width) - size;
    carry = 0;
    for (int i = 0; i < width; ++i) {
        const bool public_bit = ((complement >> i) & 1) != 0;
        const std::uint8_t public_share = backend.party() == 0 && public_bit;
        difference[i] = sum[i] ^ carry ^ public_share;
        const auto both = backend.bit_and(sum[i], carry);
        carry = public_bit ? (sum[i] ^ carry ^ both) : both;
    }
    std::vector<std::uint8_t> bits(depth);
    for (int i = 0; i < depth; ++i)
        bits[depth - 1 - i] = sum[i] ^ backend.bit_and(carry, sum[i] ^ difference[i]);
    return bits;
}
}  // namespace detail

struct LwrUpdateState::Impl {
    int party;
    dfss_minimal::LwrKhPrf::Key key_share;
    std::vector<dfss_minimal::LwrPayload> positions;
    bool used = false;
    std::uint64_t last_invocation = 0;
};

LwrUpdateState::LwrUpdateState(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
LwrUpdateState::~LwrUpdateState() = default;
LwrUpdateState::LwrUpdateState(LwrUpdateState&&) noexcept = default;
LwrUpdateState& LwrUpdateState::operator=(LwrUpdateState&&) noexcept = default;

std::uint64_t LwrUpdateState::size() const {
    if (!impl_) throw std::logic_error("moved-from RISA state");
    return impl_->positions.size();
}

bool LwrUpdateState::can_update(std::uint64_t invocation) const {
    return impl_ && (!impl_->used || invocation > impl_->last_invocation);
}

LwrUpdateState Compile(Backend& backend, const UpdateParameters& pp, Word index_share) {
    const auto bits = detail::index_bits_mod_n(backend, pp.size, index_share);
    auto impl = std::make_unique<LwrUpdateState::Impl>();
    impl->party = backend.party();
    impl->key_share = dfss_minimal::LwrKhPrf::random_key();
    dfss_minimal::LwrPayload beta;
    beta.indicator = backend.party() == 0 ? 1 : 0;
    beta.kh_key = impl->key_share;
    const auto key = dfss_minimal::generate_lwr(backend, bits, beta);
    impl->positions = dfss_minimal::evaluate_prefix(key, pp.size);
    return LwrUpdateState(std::move(impl));
}

namespace detail {
class BatchEvaluator {
public:
    static void validate(Backend& backend, const UpdateParameters& pp, Word t,
                         const std::vector<BatchInput>& batch) {
        Setup(pp.size);
        std::unordered_set<const LwrUpdateState*> seen;
        for (const auto& item : batch) {
            const auto& st = item.state;
            if (!st.can_update(t)) throw std::logic_error("stale t or moved-from RISA state");
            if (st.size() != pp.size || st.impl_->party != backend.party())
                throw std::invalid_argument("wrong state size or party in RISA batch");
            if (!seen.insert(&st).second) throw std::invalid_argument("repeated state in RISA batch");
        }
    }

    // Shared kernel for both output layouts. All validation and output
    // allocation is finished before any mask is used on the wire.
    template<class Emit>
    static void run(Backend& backend, Word t, const std::vector<BatchInput>& batch, Emit emit) {
        if (batch.empty()) return;
        const auto input = dfss_minimal::LwrKhPrf::expand_input(t);
        std::vector<Word> c(batch.size());
        for (size_t i = 0; i < batch.size(); ++i) {
            const auto& st = *batch[i].state.impl_;
            c[i] = batch[i].message_share +
                   dfss_minimal::LwrKhPrf::eval_expanded(st.key_share, input);
        }
        for (const auto& item : batch) {
            item.state.impl_->used = true;
            item.state.impl_->last_invocation = t;
        }
        backend.open_sums(c);
        for (size_t i = 0; i < batch.size(); ++i) {
            const auto& positions = batch[i].state.impl_->positions;
            for (size_t x = 0; x < positions.size(); ++x) {
                const auto& pos = positions[x];
                emit(i, x, pos.indicator * c[i] -
                           dfss_minimal::LwrKhPrf::eval_expanded(pos.kh_key, input));
            }
        }
    }
};
}  // namespace detail

std::vector<ApproximateContribution> UpdateBatch(
        Backend& backend, const UpdateParameters& pp, Word t, const std::vector<BatchInput>& batch) {
    detail::BatchEvaluator::validate(backend, pp, t, batch);
    std::vector<ApproximateContribution> result;
    result.reserve(batch.size());
    for (size_t i = 0; i < batch.size(); ++i)
        result.push_back({std::vector<Word>(pp.size), 1});
    detail::BatchEvaluator::run(backend, t, batch,
        [&](size_t i, size_t x, Word v) { result[i].shares[x] = v; });
    return result;
}

ApproximateContribution UpdateBatchGather(
        Backend& backend, const UpdateParameters& pp, Word t, const std::vector<BatchInput>& batch) {
    detail::BatchEvaluator::validate(backend, pp, t, batch);
    ApproximateContribution result{std::vector<Word>(pp.size, 0), batch.size()};
    detail::BatchEvaluator::run(backend, t, batch,
        [&](size_t, size_t x, Word v) { result.shares[x] += v; });
    return result;
}

ApproximateContribution Update(Backend& backend, LwrUpdateState& state, Word t, Word m) {
    return UpdateBatchGather(backend, {state.size()}, t, {{state, m}});
}

}  // namespace risa
