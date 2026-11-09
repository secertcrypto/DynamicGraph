#pragma once

#include "benchmark/utils.h"
#include "src/common/protocol_runner.h"
#include "src/graphiti/message_passing.h"
#include "src/graphiti/plain_graph.h"

#include <functional>
#include <optional>

namespace graphiti_bench {

using Word = std::uint64_t;
using Circuit = threepc::Circuit<Word>;
using Wires = graphiti::Wires;
using threepc::wire_t;
using threepc::GateType;
namespace protocol = threepc::protocol;

enum class Workload { Bfs, PageRank, Init, Shuffle };

struct Args {
    protocol::ProtocolConfig protocol;
    std::size_t vertices = 0, edges = 0, vec_size = 0, source = 0;
    int rounds = 10;
    std::uint64_t seed = 0x4752415048495449ULL;
    bool check = true, raw_counts = false;
    std::string output;
    std::size_t rows() const { return vertices + edges; }
    bool helper() const {
        return protocol.kind == protocol::ProtocolKind::Nph &&
               protocol.pid == protocol.num_compute_parties;
    }
};

struct Input {
    int owner;
    Wires wires;
    std::vector<Word> values;
};

struct Program {
    threepc::LevelOrderedCircuit circuit;
    std::vector<Input> inputs;
    // Evaluate these levels as init; all remaining levels are online.
    std::size_t setup_levels = 0;
    std::optional<std::vector<Word>> expected;
    bool compare_multiset = false;
    nlohmann::json details = nlohmann::json::object();
};

struct GraphInputs {
    Wires vertices;
    graphiti::OrderWires orders;
    std::optional<graphiti::GraphList> fixture;
};

// Wire shape is identical on every party. Only the owner stages plaintext.
Wires input(Circuit& circuit, Program& program, const Args& args, int owner,
            std::size_t count, std::vector<Word> values = {});
GraphInputs graphInputs(Circuit& circuit, Program& program, const Args& args,
                        const std::vector<Word>& initial);
std::vector<Word> incoming(const graphiti::GraphList& graph,
                           const std::vector<Word>& values);

using Builder = std::function<Program(const Args&)>;
Args parseArguments(int argc,char** argv,Workload workload);
int run(int argc, char** argv, Workload workload, const Builder& build);

} // namespace graphiti_bench
