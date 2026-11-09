#include "benchmark/graphiti/common.h"

using namespace graphiti_bench;

static Program build(const Args& args) {
    Circuit circuit;
    Program program;
    auto inputs = graphInputs(circuit, program, args, std::vector<Word>(args.vertices, 1));
    auto rho = input(circuit, program, args, threepc::P1, args.vertices,
                     std::vector<Word>(args.vertices, 1));
    graphiti::MessagePassing<Word> graph(circuit, args.vertices, args.edges);
    graph.init(inputs.vertices, inputs.orders);
    for (int round = 0; round < args.rounds; ++round) {
        graph.iterate([&](auto& c, std::size_t v, wire_t, wire_t aggregate) {
            return c.addGate(GateType::kMul, rho[v], aggregate);
        });
    }
    for (auto wire : graph.vertices()) circuit.setAsOutput(wire);
    program.circuit = circuit.orderGatesByLevel();
    program.setup_levels = graph.setupEndLevel(program.circuit) + 1;
    program.details = {{"num_iters", args.rounds}, {"alpha", 1}, {"rho", 1},
        {"workload", "unnormalized_incoming_sum"}};
    if (!args.helper() && args.check && args.rows() <= 100000) {
        std::vector<Word> expected(args.vertices, 1);
        for (int round = 0; round < args.rounds; ++round)
            expected = incoming(*inputs.fixture, expected);
        program.expected = std::move(expected);
    }
    return program;
}

int main(int argc, char** argv) { return run(argc, argv, Workload::PageRank, build); }
