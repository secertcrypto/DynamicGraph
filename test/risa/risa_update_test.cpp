#include "risa/update.h"
#include "risa/index_bits.h"
#include "dynamic_graph/delta_updates.h"
#include <iostream>
#include <limits>
#include <map>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>

using risa::Word;
static_assert(!std::is_copy_constructible<risa::LwrUpdateState>::value,
              "Copying a compiled state could reuse streaming masks");

void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
template <typename Exception, typename Function>
void rejects(Function function) {
    bool rejected = false;
    try { function(); } catch (const Exception&) { rejected = true; }
    require(rejected, "expected local input/state rejection");
}

int main(int argc, char** argv) {
    try {
        if (argc != 3) return 2;
        const int party = std::stoi(argv[1]), port = std::stoi(argv[2]);
        if ((party != 0 && party != 1) || port < 1024 || port > 65535) return 2;
        rejects<std::invalid_argument>([] { risa::Setup(0); });
        rejects<std::invalid_argument>([] { risa::Setup((Word{1} << 24) + 1); });
        emp::NetIO io(party == 0 ? nullptr : "127.0.0.1", port);
        risa::Backend backend(party, io);

        // Exhaustive check against ordinary integer arithmetic. Outputs are
        // reconstructed only here, not in Compile or in the graph wrapper.
        std::size_t conversions = 0;
        auto check_index = [&](Word n, Word a0, Word a1) {
            auto bits = risa::detail::index_bits_mod_n(backend, n, party ? a1 : a0);
            Word opened = 0;
            for (auto bit : bits) opened = (opened << 1) | backend.open_bit(bit);
            require(opened == (a0 + a1) % n, "secret reduction mod N failed");
            ++conversions;
        };
        for (Word n = 1; n <= 7; ++n)
            for (Word a0 = 0; a0 < n; ++a0)
                for (Word a1 = 0; a1 < n; ++a1) check_index(n, a0, a1);
        for (Word n : {Word{9}, Word{17}, (Word{1} << 24) - 1, Word{1} << 24}) {
            check_index(n, n - 1, n - 1);
            check_index(n, n - 1, 1);
            check_index(n, 0, n - 1);
        }
        rejects<std::invalid_argument>([&] { risa::Compile(backend, risa::Setup(5), 5); });

        std::size_t coordinates = 0, nonzero_residuals = 0;
        for (Word n : {Word{1}, Word{2}, Word{3}, Word{5}, Word{8}, Word{9}}) {
            for (Word alpha : {Word{0}, n - 1}) {
                const Word a0 = n - 1, a1 = (alpha + 1) % n;
                auto state = risa::Compile(backend, risa::Setup(n), party ? a1 : a0);
                require(state.size() == n, "wrong state size");
                Word previous_output = 0;
                for (Word t = 0; t < 4; ++t) {
                    const Word intended = t == 0 ? 0 : t == 1 ? 42 : ~Word{0};
                    Word local_message = party ? intended - 123 : 123;
                    // Last invocation uses input shares selected from prior
                    // local output shares, rather than a fixed input sequence.
                    if (t == 3) local_message = previous_output + 7 + party;
                    const Word message = backend.open_sum(local_message); // test only
                    const auto before = io.counter;
                    auto contribution = risa::Update(backend, state, t, local_message);
                    require(io.counter - before == sizeof(Word), "Update sent more than masked share");
                    require(contribution.max_coordinate_error == 1, "wrong update error bound");
                    previous_output = contribution.shares[t % n];
                    for (Word x = 0; x < n; ++x) {
                        const Word actual = backend.open_sum(contribution.shares[x]);
                        const Word error = actual - (x == alpha ? message : 0);
                        require(x == alpha ? (error == 0 || error == 1 || error == ~Word{0})
                                           : (error == 0 || error == 1), "update residual bound failed");
                        ++coordinates;
                        nonzero_residuals += error != 0;
                    }
                }
                const auto before = io.counter;
                rejects<std::logic_error>([&] { risa::Update(backend, state, 3, 9); });
                rejects<std::logic_error>([&] { risa::Update(backend, state, 1, 9); });
                require(io.counter == before, "invalid t communicated");
                auto moved = std::move(state);
                require(!state.can_update(10), "moved-from state reusable");
                rejects<std::logic_error>([&] { risa::Update(backend, state, 10, 9); });
                auto last = risa::Update(backend, moved, std::numeric_limits<Word>::max(), party ? 11 : 0);
                require(last.shares.size() == n, "moved state failed");
                rejects<std::logic_error>([&] { risa::Update(backend, moved, 0, 1); });
            }
        }
        // Forces a full sum beyond 2^ceil(log2 N): 4+4=8 -> 3 mod 5.
        {
            auto state = risa::Compile(backend, risa::Setup(5), 4);
            auto contribution = risa::Update(backend, state, 0, party ? 100 : 0);
            for (Word x = 0; x < 5; ++x) {
                const Word e = backend.open_sum(contribution.shares[x]) - (x == 3 ? 100 : 0);
                require(e == 0 || e == 1 || (x == 3 && e == ~Word{0}), "full index carry lost");
            }
        }

        dynamic_graph::LwrDeltaUpdates graph(backend, 5);
        auto empty = graph.aggregate(0, {});
        require(empty.shares == std::vector<Word>(5, 0) && empty.max_coordinate_error == 0,
                "empty delta set must be exactly zero");
        // Two distinct edges at the same destination; same t is allowed for
        // different independently compiled states.
        graph.insert(20, party ? 3 : 4); // alpha = 2 mod 5
        graph.insert(10, party ? 3 : 4);
        rejects<std::invalid_argument>([&] { graph.insert(10, 0); });
        rejects<std::invalid_argument>([&] { graph.aggregate(1, {{10, 0}}); });
        rejects<std::invalid_argument>([&] { graph.aggregate(1, {{10, 0}, {30, 0}}); });
        const auto before = io.counter;
        auto delta = graph.aggregate(1, {{10, party ? 17 : 0}, {20, party ? 29 : 0}});
        require(io.counter - before == 2 * sizeof(Word), "wrong batch communication");
        require(delta.max_coordinate_error == 2, "batch error bound not accumulated");
        rejects<std::invalid_argument>([&] { dynamic_graph::combine_base({1}, delta); });
        auto combined = dynamic_graph::combine_base(std::vector<Word>(5, party ? 3 : 7), std::move(delta));
        for (Word x = 0; x < 5; ++x) {
            const Word e = backend.open_sum(combined.shares[x]) - (10 + (x == 2 ? 46 : 0));
            require(e <= 2 || Word{0} - e <= 2, "base plus delta bound failed");
        }
        graph.erase(20);
        graph.insert(30, party ? 1 : 4); // alpha = 0
        auto next = graph.aggregate(2, {{10, party ? 5 : 0}, {30, party ? 31 : 0}});
        for (Word x = 0; x < 5; ++x) {
            const Word e = backend.open_sum(next.shares[x]) - (x == 0 ? 31 : x == 2 ? 5 : 0);
            require(e <= 2 || Word{0} - e <= 2, "insertion/removal lost state alignment");
        }
        rejects<std::logic_error>([&] { graph.aggregate(2, {}); });
        rejects<std::invalid_argument>([&] { graph.erase(99); });
        std::cout << "PASS P" << party << " index_conversions=" << conversions
                  << " checked_update_coordinates=" << coordinates
                  << " nonzero_rounding_residuals=" << nonzero_residuals << '\n';
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
