// Deterministic public fixture. Only this example opens outputs to check them.
// The RISA and dynamic-graph library code never opens contribution arrays.
#include "dynamic_graph/delta_updates.h"
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>

int main(int argc, char** argv) {
    try {
        if (argc < 3 || argc > 4) {
            std::cerr << "Usage: risa_update_example PARTY(0|1) PORT [SERVER_IP]\n";
            return 2;
        }
        const int party = std::stoi(argv[1]), port = std::stoi(argv[2]);
        if ((party != 0 && party != 1) || port < 1024 || port > 65535)
            throw std::invalid_argument("invalid party or port");
        const char* host = argc == 4 ? argv[3] : "127.0.0.1";
        emp::NetIO io(party == 0 ? nullptr : host, port);
        risa::Backend backend(party, io);
        constexpr std::uint64_t N = 5;
        dynamic_graph::LwrDeltaUpdates deltas(backend, N);
        const auto index_share = [party](std::uint64_t index) {
            return party == 0 ? N - 1 : (index + 1) % N;
        };
        const auto message_share = [party](std::uint64_t value, std::uint64_t id) {
            const std::uint64_t mask = 1000 + id;
            return party == 0 ? mask : value - mask;
        };

        // Both parties insert in the same public order. Each supplies only
        // its own destination share; source reads and ApplyE belong upstream.
        deltas.insert(10, index_share(2));
        deltas.insert(20, index_share(4));
        for (std::uint64_t t = 0; t < 3; ++t) {
            // Insert an edge between iterations, reusing existing edge states.
            if (t == 1) deltas.insert(30, index_share(1));
            if (t == 2) deltas.erase(20);
            std::map<std::uint64_t, risa::Word> messages;
            std::vector<risa::Word> expected(N, 0);
            messages.emplace(10, message_share(17 + t, 10)); expected[2] += 17 + t;
            if (t != 2) {
                messages.emplace(20, message_share(29 + t, 20)); expected[4] += 29 + t;
            }
            if (t != 0) {
                messages.emplace(30, message_share(41 + t, 30)); expected[1] += 41 + t;
            }
            const auto before = io.counter;
            auto delta = deltas.aggregate(t, messages);
            const auto bytes_sent = io.counter - before;
            std::vector<risa::Word> base(N);
            for (std::size_t x = 0; x < N; ++x) {
                base[x] = message_share(10 + x, 100 + x);
                expected[x] += 10 + x;
            }
            auto total = dynamic_graph::combine_base(base, std::move(delta));
            std::cout << "P" << party << " t=" << t << " active_edges=" << deltas.size()
                      << " update_bytes_sent=" << bytes_sent
                      << " max_coordinate_error=" << total.max_coordinate_error << '\n';
            for (std::size_t x = 0; x < N; ++x) {
                const auto opened = backend.open_sum(total.shares[x]);  // demo only
                const auto error = opened - expected[x];
                const auto bound = total.max_coordinate_error;
                if (error > bound && risa::Word{0} - error > bound)
                    throw std::runtime_error("aggregate outside proven modular error bound");
                if (party == 0)
                    std::cout << "  x=" << x << " intended=" << expected[x]
                              << " reconstructed=" << opened << '\n';
            }
        }
        std::cout << "PASS RISA LWR dynamic updates P" << party
                  << " (bounded error, not exact updates)\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
