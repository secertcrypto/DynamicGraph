#pragma once
#include <cstdint>
#include <memory>
#include <vector>
#include <emp-tool/io/net_io_channel.h>

namespace graphiti::twopc {
using Word=std::uint64_t;
using Words=std::vector<Word>;
using Rows=std::vector<Words>;
enum class IndexSharing { ModuloVertices, Ring64 };

// Each process stores only its own two private permutation factors.
// Source/destination maps are PUBLIC pull permutations; all other state is local.
class InitState {
    friend class Initializer;
    std::size_t n_=0,vertices_=0;
    std::vector<int> a_,b_;
    Words c_share_;
    std::vector<std::size_t> source_,destination_;
public:
    std::size_t size() const {return n_;}
    std::size_t vertices() const {return vertices_;}
    const auto& source_mapping() const {return source_;}
    const auto& destination_mapping() const {return destination_;}
    const Words& return_mapping_share() const {return c_share_;}
};

// Push labels expected by graphiti::MessagePassing: out[labels[i]] = in[i].
// Each vector is additively shared in Z_(2^64), never reconstructed.
struct OrderShares {
    Words vertex_to_source,source_to_destination,destination_to_vertex;
};

// Exactly two computing parties, IDs 0 and 1. No dealer or trusted preprocessing.
// The caller owns the connected EMP channel for the lifetime of this object.
class Initializer {
    struct Impl;
    std::unique_ptr<Impl> impl_;
public:
    Initializer(int party,emp::NetIO& io);
    ~Initializer();
    std::uint64_t sent() const;
    std::uint64_t received() const;
    Initializer(const Initializer&)=delete;
    Initializer& operator=(const Initializer&)=delete;
    // Inputs are the endpoint columns of a vertex-ordered DAG list (V vertices,
    // then E edges). Vertex row i has reconstructed src=dst=i. Data is untouched.
    // ModuloVertices accepts canonical local shares in Z_V, matching BaseDagShare.
    InitState Init(std::size_t vertices,const Words& src,const Words& dst,
                   IndexSharing sharing=IndexSharing::ModuloVertices);
    OrderShares ExportOrderShares(const InitState&);
    Rows VertexToSource(const InitState&,Rows local_rows);
    Rows SourceToDestination(const InitState&,Rows local_rows);
    Rows DestinationToVertex(const InitState&,Rows local_rows);
    // Arbitrary columns in Z_(2^64) travel together under the same row permutation.
    // The graph's endpoint topology must remain fixed for this InitState.
};
}
