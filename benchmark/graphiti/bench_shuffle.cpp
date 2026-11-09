#include "benchmark/graphiti/common.h"

using namespace graphiti_bench;

static Program build(const Args& args) {
    Circuit circuit;
    Program program;
    std::vector<Word> values(args.vec_size);
    for (std::size_t i = 0; i < values.size(); ++i)
        values[i] = Word{1000003} + Word{37} * i + i % 11;
    auto current = input(circuit, program, args, threepc::P0, args.vec_size, values);
    const int group = circuit.freshPermGroupId();
    for (int round = 0; round < args.rounds; ++round)
        current = circuit.addShuffleGate(current, group);
    for (auto wire : current) circuit.setAsOutput(wire);
    program.circuit = circuit.orderGatesByLevel();
    program.compare_multiset = true;
    if (!args.helper() && args.check) program.expected = std::move(values);
    program.details = {{"vec_size", args.vec_size}, {"num_repeats", args.rounds},
        {"workload", "repeated_grouped_shuffle"}, {"validation_scope", "output_multiset"}};
    return program;
}

int main(int argc, char** argv) { return run(argc, argv, Workload::Shuffle, build); }
