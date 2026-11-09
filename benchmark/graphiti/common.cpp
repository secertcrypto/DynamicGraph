#include "benchmark/graphiti/common.h"

#include <charconv>
#include <climits>
#include <iostream>
#include <limits>
#include <string_view>

namespace graphiti_bench {
namespace {
const char* name(Workload workload) {
    switch (workload) {
        case Workload::Bfs: return "bench_BfsMpaGraphiti";
        case Workload::PageRank: return "bench_PrMpaGraphiti";
        case Workload::Init: return "microbench_InitGraphiti";
        case Workload::Shuffle: return "bench_shuffle";
    }
    throw std::logic_error("unknown workload");
}

std::uint64_t number(std::string_view text, const std::string& option,
                     std::uint64_t maximum = std::numeric_limits<std::uint64_t>::max()) {
    std::uint64_t result = 0;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), result);
    if (text.empty() || parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() || result > maximum)
        throw std::invalid_argument(option + ": expected an unsigned decimal integer in range");
    return result;
}

void usage(const char* executable, Workload workload) {
    std::cout << "Usage: " << executable << " --pid P --protocol nph|rss3|two_party --num-parties N\n"
              << (workload == Workload::Shuffle
                  ? "  --vec-size N --num-repeats R\n"
                  : "  --graph-size N | --num-verts V --num-edges E\n")
              << "  [--port P] [--peer ADDRESS] [--output FILE] [--pking]\n"
              << "  [--disable-optimized-shuffle] [--no-check]\n";
    if (workload != Workload::Shuffle) std::cout << "  [--seed S]\n";
    if (workload == Workload::Bfs)
        std::cout << "  [--num-hops H] [--source-vertex V] [--skip-final-applyv]\n";
    if (workload == Workload::PageRank) std::cout << "  [--num-iters R]\n";
}

Args parse(int argc, char** argv, Workload workload) {
    Args a;
    a.protocol.kind = protocol::ProtocolKind::Nph;
    a.protocol.num_compute_parties = 2;
    a.protocol.port = workload == Workload::Shuffle ? 14700 : workload == Workload::Init ? 14900 : 14800;
    if (workload == Workload::PageRank) a.seed = 0x5041474552414e4bULL;
    if (workload == Workload::Shuffle) a.rounds = 0;
    std::optional<std::size_t> graph_size;
    bool explicit_vertices = false, explicit_edges = false;
    for (int i = 1; i < argc; ++i) {
        const std::string option = argv[i];
        auto value = [&]() -> std::string_view {
            if (i + 1 >= argc) throw std::invalid_argument("missing value for " + option);
            return argv[++i];
        };
        auto integer = [&](std::uint64_t max = INT_MAX) { return number(value(), option, max); };
        if (option == "--pid") a.protocol.pid = static_cast<int>(integer());
        else if (option == "--protocol") a.protocol.kind = protocol::parseProtocolKind(std::string(value()));
        else if (option == "--num-parties") a.protocol.num_compute_parties = static_cast<int>(integer());
        else if (option == "--port") a.protocol.port = static_cast<int>(integer(65535));
        else if (option == "--peer") a.protocol.peer = value();
        else if (option == "--output") a.output = value();
        else if (option == "--pking") a.protocol.pking = true;
        else if (option == "--disable-optimized-shuffle") a.protocol.disable_optimized_shuffle = true;
        else if (option == "--no-check") a.check = false;
        else if (option == "--graph-size" && workload != Workload::Shuffle) graph_size = integer();
        else if (option == "--num-verts" && workload != Workload::Shuffle) { a.vertices = integer(); explicit_vertices = true; }
        else if (option == "--num-edges" && workload != Workload::Shuffle) { a.edges = integer(); explicit_edges = true; }
        else if (option == "--seed" && workload != Workload::Shuffle) a.seed = integer(UINT64_MAX);
        else if (option == "--num-hops" && workload == Workload::Bfs) a.rounds = static_cast<int>(integer());
        else if (option == "--source-vertex" && workload == Workload::Bfs) a.source = integer();
        else if (option == "--skip-final-applyv" && workload == Workload::Bfs) a.raw_counts = true;
        else if (option == "--num-iters" && workload == Workload::PageRank) a.rounds = static_cast<int>(integer());
        else if (option == "--vec-size" && workload == Workload::Shuffle) a.vec_size = integer();
        else if (option == "--num-repeats" && workload == Workload::Shuffle) a.rounds = static_cast<int>(integer());
        else throw std::invalid_argument("unknown or unsupported option: " + option);
    }
    const auto& p = a.protocol;
    if (p.kind == protocol::ProtocolKind::Rss3) {
        if (p.num_compute_parties != 3 || p.pid < 0 || p.pid >= 3)
            throw std::invalid_argument("RSS3 requires three parties, with PID 0..2");
    } else if (p.kind == protocol::ProtocolKind::TwoParty) {
        if (p.num_compute_parties != 2 || p.pid < 0 || p.pid > 1 || p.pking || p.disable_optimized_shuffle)
            throw std::invalid_argument("two_party needs PID 0/1, two parties, and no NPH-only flags");
    } else if (p.num_compute_parties < 2 || p.pid < 0 || p.pid > p.num_compute_parties) {
        throw std::invalid_argument("NPH requires at least two compute parties; helper PID equals their count");
    }
    // NetNP reserves 1024 ports per sender; Net3P uses six directed ports.
    const std::int64_t port_offset = p.kind == protocol::ProtocolKind::Nph
        ? std::int64_t(p.num_compute_parties) * 1024 + p.num_compute_parties - 1 : p.kind == protocol::ProtocolKind::TwoParty ? 0 : 5;
    if (p.port < 1024 || p.port + port_offset > 65535)
        throw std::invalid_argument("base port and party count exceed the TCP port range");
    if (a.rounds <= 0) throw std::invalid_argument("round count must be positive");
    if (workload == Workload::Shuffle) {
        if (!a.vec_size) throw std::invalid_argument("--vec-size must be positive");
    } else {
        if (graph_size) {
            if (explicit_vertices || explicit_edges)
                throw std::invalid_argument("choose --graph-size or explicit vertex/edge counts");
            a.vertices = *graph_size / 10;
            a.edges = *graph_size - a.vertices;
        } else if (!explicit_vertices || !explicit_edges) {
            throw std::invalid_argument("supply --graph-size or both --num-verts and --num-edges");
        }
        if (!a.vertices || a.rows() > INT_MAX || (a.vertices == 1 && a.edges != 0))
            throw std::invalid_argument("require 1 <= V <= V+E <= INT_MAX; self-loop-free edges require V >= 2");
        if (workload == Workload::Bfs && a.source >= a.vertices)
            throw std::invalid_argument("source vertex is out of range");
    }
    return a;
}

nlohmann::json sumPhases(const std::vector<nlohmann::json>& phases) {
    auto result = phases.front();
    for (std::size_t i = 1; i < phases.size(); ++i) {
        result["time_ms"] = result["time_ms"].get<double>() + phases[i]["time_ms"].get<double>();
        for (const char* key : {"total_bytes_sent", "total_bytes_recv"})
            result[key] = result[key].get<std::uint64_t>() + phases[i][key].get<std::uint64_t>();
        for (const char* key : {"bytes_sent", "bytes_recv"})
            for (std::size_t party = 0; party < result[key].size(); ++party)
                result[key][party] = result[key][party].get<std::uint64_t>() + phases[i][key][party].get<std::uint64_t>();
    }
    return result;
}
} // namespace

Args parseArguments(int argc,char** argv,Workload workload){return parse(argc,argv,workload);}

Wires input(Circuit& circuit, Program& program, const Args& args, int owner,
            std::size_t count, std::vector<Word> values) {
    Wires wires(count);
    for (auto& wire : wires) wire = circuit.newInputWire(owner);
    if (args.protocol.pid == owner) {
        if (values.size() != count) throw std::logic_error("input value count mismatch");
        program.inputs.push_back({owner, wires, std::move(values)});
    }
    return wires;
}

GraphInputs graphInputs(Circuit& circuit, Program& program, const Args& args,
                        const std::vector<Word>& initial) {
    GraphInputs inputs;
    const bool owner = args.protocol.pid == threepc::P1;
    // Only the input owner needs permutations. Small public seeded fixtures
    // are independently regenerated by compute parties for validation.
    if (owner || (!args.helper() && args.check && args.rows() <= 100000))
        inputs.fixture = graphiti::generateRandomGraphList(args.vertices, args.edges, args.seed);
    graphiti::GraphOrderPermutations orders;
    if (owner) orders = graphiti::computeGraphOrderPermutations(*inputs.fixture);
    auto labels = [&](const auto& pull) {
        return input(circuit, program, args, threepc::P1, args.rows(),
                     owner ? graphiti::destinationLabelsFromPull<Word>(pull) : std::vector<Word>{});
    };
    inputs.vertices = input(circuit, program, args, threepc::P1, args.vertices, initial);
    inputs.orders = {labels(orders.vertex_to_source), labels(orders.source_to_destination),
                     labels(orders.destination_to_vertex)};
    return inputs;
}

std::vector<Word> incoming(const graphiti::GraphList& graph, const std::vector<Word>& values) {
    std::vector<Word> result(values.size(), 0);
    for (std::size_t row = 0; row < graph.size(); ++row)
        if (!graph.isV[row]) result[graph.dst[row]] += values[graph.src[row]];
    return result;
}

int run(int argc, char** argv, Workload workload, const Builder& build) {
    try {
        if (argc == 2 && std::string_view(argv[1]) == "--help") { usage(argv[0], workload); return 0; }
        const auto args = parse(argc, argv, workload);
        const int pid = args.protocol.pid;
        bench::TimePoint build_start;
        auto program = build(args);
        const double build_ms = bench::TimePoint{} - build_start;
        const auto& circuit = program.circuit;
        if (program.setup_levels > circuit.depth()) throw std::logic_error("invalid setup boundary");
        std::printf("\n=== %s ===\n[P%d] Circuit: %zu gates, %zu wires, depth %zu\n",
                    name(workload), pid, circuit.num_gates, circuit.num_wires, circuit.depth());
        auto runner = protocol::makeProtocolRunner<Word>(args.protocol);
        bench::increaseSocketBuffers(*runner, 128 * 1024 * 1024);
        for (const auto& entry : program.inputs) runner->setInputs(entry.wires, entry.values);
        program.inputs.clear();
        runner->resetCounters();
        using Stats = bench::StatsPoint<protocol::IProtocolRunner<Word>>;
        auto measure = [&](auto&& operation) {
            Stats start(*runner);
            operation();
            return Stats(*runner) - start;
        };
        auto offline = measure([&] { runner->offline(circuit); });
        auto init = measure([&] {
            for (std::size_t level = 0; level < program.setup_levels; ++level) runner->evalLevel(level, circuit);
        });
        auto online = measure([&] {
            for (std::size_t level = program.setup_levels; level < circuit.depth(); ++level) runner->evalLevel(level, circuit);
        });
        std::vector<Word> output;
        auto reconstruction = measure([&] { output = runner->getOutputs(circuit); });
        const bool checked = !runner->isHelper() && program.expected.has_value();
        bool correct = true;
        if (checked) {
            if (program.compare_multiset) {
                std::sort(output.begin(), output.end());
                std::sort(program.expected->begin(), program.expected->end());
            }
            correct = output == *program.expected;
        }
        std::printf("[P%d] %s correctness: %s\n", pid, name(workload), checked ? (correct ? "PASS" : "FAIL") : "SKIP");
        const auto total = sumPhases({offline, init, online});
        for (const auto& phase : std::vector<std::pair<std::string, nlohmann::json>>{
                 {"offline", offline}, {"init", init}, {"online", online}, {"total", total}, {"reconstruction", reconstruction}})
            bench::printPhaseStats(pid, phase.first, phase.second);
        auto& details = program.details;
        if(args.protocol.kind==protocol::ProtocolKind::TwoParty)
            details.update({{"offline_scope","validation_and_allocation"},
                {"ot_scope","OT setup and arithmetic are included in init/online execution"}});
        details.update({{"benchmark", name(workload)}, {"schema_version", 2},
            {"protocol", protocol::protocolName(args.protocol.kind)}, {"num_compute_parties", args.protocol.num_compute_parties},
            {"pid", pid}, {"is_helper", runner->isHelper()}, {"pking", args.protocol.pking},
            {"optimized_shuffle_disabled", args.protocol.disable_optimized_shuffle},
            {"port", args.protocol.port}, {"peer", args.protocol.peer}, {"seed", args.seed},
            {"setup_levels", program.setup_levels}, {"measured_start_level", program.setup_levels},
            {"num_gates", circuit.num_gates}, {"num_wires", circuit.num_wires}, {"depth", circuit.depth()},
            {"build_and_input_generation_ms", build_ms}});
        if (workload != Workload::Shuffle)
            details.update({{"graph_size", args.rows()}, {"num_vertices", args.vertices}, {"num_edges", args.edges},
                            {"initialization", "MessagePassing::init with precomputed secret order labels"}});
        nlohmann::json document = {{"details", details}, {"correctness_checked", checked},
            {"correct", checked ? nlohmann::json(correct) : nlohmann::json(nullptr)},
            {"offline", offline}, {"init", init}, {"online", online}, {"total", total},
            {"reconstruction", reconstruction}, {"memory", {
                {"peak_virtual_memory_kb", bench::peakVirtualMemory()},
                {"peak_resident_set_size_kb", bench::peakResidentSetSize()}}}};
        if (!args.output.empty() && !bench::saveJson(document, args.output))
            throw std::runtime_error("could not save benchmark JSON");
        return correct ? 0 : 1;
    } catch (const std::exception& error) {
        std::cerr << "Fatal: " << error.what() << '\n';
        return 1;
    }
}
} // namespace graphiti_bench
