#include "benchmark/graphiti/common.h"

using namespace graphiti_bench;

static Program build(const Args& args) {
    Circuit circuit;
    Program program;
    std::vector<Word> initial(args.vertices, 0);
    initial[args.source] = 1;
    auto inputs = graphInputs(circuit, program, args, initial);
    graphiti::MessagePassing<Word> graph(circuit, args.vertices, args.edges);
    graph.init(inputs.vertices, inputs.orders);

    for (int hop = 0; hop < args.rounds; ++hop) {
        graph.iterate([&](auto& c, std::size_t, wire_t old, wire_t incoming) {
            auto sum = c.addGate(GateType::kAdd, old, incoming);
            // Normalize every round: accumulating path counts until the last
            // round can wrap to zero and misclassify a reachable vertex.
            return args.raw_counts ? sum
                : c.addCGate(GateType::kCSub, c.addEqzGate(sum), Word{1}, true);
        });
    }
    for (auto wire : graph.vertices()) circuit.setAsOutput(wire);
    program.circuit = circuit.orderGatesByLevel();
    program.setup_levels = graph.setupEndLevel(program.circuit) + 1;
    program.details = {{"num_hops", args.rounds}, {"source_vertex", args.source},
        {"final_applyv", !args.raw_counts}, {"normalize_each_hop", !args.raw_counts},
        {"workload", args.raw_counts ? "ring_path_counts" : "bounded_hop_reachability"}};

    if (!args.helper() && args.check && args.rows() <= 100000) {
        auto expected = initial;
        for (int hop = 0; hop < args.rounds; ++hop) {
            auto next = incoming(*inputs.fixture, expected);
            for (std::size_t v = 0; v < next.size(); ++v) {
                next[v] += expected[v];
                if (!args.raw_counts) next[v] = next[v] != 0;
            }
            expected = std::move(next);
        }
        program.expected = std::move(expected);
    }
    return program;
}

int main(int argc, char** argv) { return run(argc, argv, Workload::Bfs, build); }
