// Public, deterministic regression fixture. The core module has no dependency
// on benchmark code, graph generation, output reconstruction or networking.
#include "src/graphiti/message_passing.h"
#include "src/graphiti/plain_graph.h"
#include "src/common/protocol_runner.h"

#include <iostream>
#include <string>

using T = std::uint64_t;
using namespace threepc;

int main(int argc, char** argv) {
    try {
        if (argc < 3 || argc > 5) {
            std::cerr << "Usage: graphiti_module PID nph|rss3 [PORT] [generic]\n";
            return 2;
        }
        protocol::ProtocolConfig config;
        config.pid = std::stoi(argv[1]);
        config.kind = protocol::parseProtocolKind(argv[2]);
        config.num_compute_parties = config.kind == protocol::ProtocolKind::Rss3 ? 3 : 2;
        config.port = argc >= 4 ? std::stoi(argv[3]) : 17300;
        config.disable_optimized_shuffle = argc == 5;

        // Includes parallel edges, a self-loop and isolated vertex 4.
        graphiti::GraphList plain;
        plain.src = {0, 1, 2, 3, 4, 0, 1, 0, 2, 3};
        plain.dst = {0, 1, 2, 3, 4, 1, 2, 1, 2, 0};
        plain.isV = {1, 1, 1, 1, 1, 0, 0, 0, 0, 0};
        plain.data.assign(10, 0);
        auto orders = graphiti::computeGraphOrderPermutations(plain);
        Circuit<T> circuit;
        std::vector<std::pair<graphiti::Wires, std::vector<T>>> local_inputs;
        auto input = [&](int owner, const std::vector<T>& values) {
            graphiti::Wires wires;
            for (std::size_t i = 0; i < values.size(); ++i)
                wires.push_back(circuit.newInputWire(owner));
            if (config.pid == owner) local_inputs.push_back({wires, values});
            return wires;
        };
        auto labels = [&](const auto& pull) {
            return input(P1, graphiti::destinationLabelsFromPull<T>(pull));
        };

        // Import two parties' additive input shares through ordinary MPC input
        // gates; do not reconstruct them outside the circuit.
        auto initial0 = input(P0, {20, 21, 22, 23, 24});
        auto initial1 = input(P1, {T{1}-20, T{2}-21, T{3}-22, T{4}-23, T{5}-24});
        graphiti::Wires initial;
        for (std::size_t v = 0; v < 5; ++v)
            initial.push_back(circuit.addGate(GateType::kAdd, initial0[v], initial1[v]));
        graphiti::MessagePassing<T> graph(circuit, 5, 5);
        graph.init(initial, {labels(orders.vertex_to_source),
                             labels(orders.source_to_destination),
                             labels(orders.destination_to_vertex)});
        auto weights = graph.toSourceOrder(input(P1, {0,0,0,0,0,2,3,5,7,11}));
        auto aggregates = graph.aggregate([&](auto& c, const graphiti::Wires& source) {
            graphiti::Wires messages;
            for (std::size_t i = 0; i < source.size(); ++i)
                messages.push_back(c.addGate(GateType::kMul, source[i], weights[i]));
            return messages;
        });
        // A delta contribution is added before ApplyV, from the same snapshot.
        auto delta = input(P0, {0,10,0,0,0});
        for (std::size_t v = 0; v < 5; ++v)
            aggregates[v] = circuit.addGate(GateType::kAdd, aggregates[v], delta[v]);
        graph.applyVertices(aggregates, [](auto& c, std::size_t, wire_t old, wire_t a) {
            return c.addGate(GateType::kAdd, old, a);
        });
        for (auto w : graph.vertices()) circuit.setAsOutput(w);
        graph.iterate([](auto&, std::size_t, wire_t, wire_t a) { return a; });
        for (auto w : graph.vertices()) circuit.setAsOutput(w);

        // Independent zero-edge instance in the same circuit: shuffle groups
        // must not collide, and a vertex without incoming edges gets zero.
        graphiti::MessagePassing<T> isolated(circuit, 1, 0);
        isolated.init(input(P1, {9}), {input(P1, {0}), input(P1, {0}), input(P1, {0})});
        isolated.iterate([](auto&, std::size_t, wire_t, wire_t a) { return a; });
        circuit.setAsOutput(isolated.vertices()[0]);

        const auto ordered = circuit.orderGatesByLevel();
        auto runner = protocol::makeProtocolRunner<T>(config);
        for (const auto& entry : local_inputs) runner->setInputs(entry.first, entry.second);
        runner->offline(ordered);
        runner->online(ordered);
        // Explicit reconstruction only for this example's regression check.
        auto result = runner->getOutputs(ordered);
        const std::vector<T> expected = {45,19,30,4,5, 4,90,49,0,0, 0};
        if (!runner->isHelper() && result != expected) {
            std::cerr << "Graphiti module FAIL; got:";
            for (auto v : result) std::cerr << ' ' << v;
            std::cerr << '\n';
            return 1;
        }
        std::cout << "Graphiti module PASS P" << config.pid << '\n';
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
