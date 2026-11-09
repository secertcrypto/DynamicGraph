#pragma once
#include "src/graphiti/ring_program.h"
#include <array>
#include <memory>
#include <string>

namespace dynamic_graph {
using Word=std::uint64_t;
using Wire=threepc::wire_t;
using Circuit=threepc::Circuit<Word>;

using graphiti::application::Encoding;
using graphiti::application::RingOps;
using graphiti::application::Function;
using graphiti::application::Program;

struct Config {
    int party; std::size_t vertices; Word max_delta;
    int nph_port=19200, risa_port=19250;
    std::string peer="127.0.0.1";
    unsigned shift=0;
    // Init uses P0/P1 only; later graph iterations still use the NPH helper.
    bool two_party_init=false;
    // Full helper-free runtime; implies two_party_init.
    bool two_party=false;
};
struct BaseDagShare {
    // Vertex order: V vertex rows in vertex-index order, then E edge rows.
    // Endpoint shares are canonical modulo V. data is local in Z_q.
    std::vector<Word> src,dst,data;
};

// Stateful runtime: two processes with two_party=true, otherwise three.
// In NPH mode the helper follows scheduling/preprocessing, never RISA calls.
// Cumulative per-process application payload bytes. RISA uses the compute-only
// EMP channel; two_party_tx separately counts initialization and OT arithmetic.
// NPH includes preprocessing as well as online traffic.
struct Traffic {
    std::array<Word,3> nph_tx{},nph_rx{};
    Word risa_tx=0;
    Word two_party_tx=0;
};

class Engine {
    struct Impl;
    std::unique_ptr<Impl> impl_;
    explicit Engine(std::unique_ptr<Impl>);
public:
    ~Engine();
    Engine(const Engine&)=delete;
    Engine& operator=(const Engine&)=delete;
    static std::unique_ptr<Engine> Init(Config,BaseDagShare,Program=Program::weighted_sum());
    void Insert(Word public_edge_id, Word local_src, Word local_dst, Word local_edge_data);
    void Iter();
    Traffic traffic() const;
    Word iteration() const;
    std::size_t delta_count() const;
    // Secret shares, not opened values. Helper obtains an empty vector.
    const std::vector<Word>& vertex_shares() const;
};

// Inputs are arithmetic MPC wires of the sum of the local high portions and
// of the local low portions after P0 adds S/2. Result is correct modulo q.
Wire decode_parts(Circuit&,const Encoding&,Wire high_sum,Wire low_sum);
}
