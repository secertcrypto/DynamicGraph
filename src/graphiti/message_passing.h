#pragma once

#include "src/common/circuit/circuit.h"

#include <climits>
#include <limits>
#include <stdexcept>
#include <type_traits>
#include <unordered_set>
#include <utility>
#include <vector>

namespace graphiti {

using Wires = std::vector<threepc::wire_t>;

// Secret destination labels: out[labels[i]] = in[i]. Each column has V+E
// entries. Vertex order has vertex rows 0..V-1 followed by edge rows.
struct OrderWires {
    Wires vertex_to_source;
    Wires source_to_destination;
    Wires destination_to_vertex;
};

// A circuit-building module, not an online evaluator. All calls append gates
// to the same caller-owned circuit. The circuit must outlive this object.
// Initialization accepts already computed secret permutations; it does NOT
// implement the paper's secure sorting of a secret-shared DAG-list.
template <typename T>
class MessagePassing {
public:
    MessagePassing(threepc::Circuit<T>& circuit, std::size_t vertices,
                   std::size_t edges)
        : circuit_(circuit), vertices_(vertices), rows_(checkedRows(vertices, edges)) {
        static_assert(std::is_integral<T>::value && std::is_unsigned<T>::value,
                      "Graphiti requires an unsigned ring type");
        if (rows_ - 1 > std::numeric_limits<T>::max())
            throw std::invalid_argument("Graphiti row labels do not fit the ring");
    }

    MessagePassing(const MessagePassing&) = delete;
    MessagePassing& operator=(const MessagePassing&) = delete;

    // Call once. Inputs may be existing secret circuit wires, including sums
    // of separately supplied additive shares. No vertex data is reconstructed.
    void init(const Wires& vertex_values, const OrderWires& orders) {
        if (initialized_) throw std::logic_error("Graphiti is already initialized");
        requireSize(vertex_values, vertices_);
        requireSize(orders.vertex_to_source, rows_);
        requireSize(orders.source_to_destination, rows_);
        requireSize(orders.destination_to_vertex, rows_);
        public_orders_.vertex_to_source = maskOrder(orders.vertex_to_source, vs_group_);
        public_orders_.source_to_destination = maskOrder(orders.source_to_destination, sd_group_);
        public_orders_.destination_to_vertex = maskOrder(orders.destination_to_vertex, dv_group_);
        values_ = vertex_values;
        initialized_ = true;
    }

    const Wires& vertices() const {
        requireInit();
        return values_;
    }

    // Align an existing vertex-ordered payload (e.g. edge weights) with the
    // propagated rows. Vertex rows are interspersed in source order; callbacks
    // must not branch on their secret positions.
    Wires toSourceOrder(const Wires& payload) {
        requireInit();
        requireSize(payload, rows_);
        return circuit_.addLocalPermGate(
            circuit_.addShuffleGate(anchor(payload), vs_group_),
            public_orders_.vertex_to_source);
    }

    // Read the current snapshot without updating it. ApplyE(circuit, source_rows)
    // returns V+E message wires in source order. Gather excludes vertex rows.
    template <typename ApplyE>
    Wires aggregate(ApplyE&& apply_e) {
        requireInit();
        const auto zero = circuit_.addGate(threepc::GateType::kSub, values_[0], values_[0]);
        Wires padded(rows_, zero);
        std::copy(values_.begin(), values_.end(), padded.begin());
        auto source = circuit_.addSubCircPropagate(
            anchor(padded), public_orders_.vertex_to_source, vertices_, vs_group_, false);
        auto messages = apply_e(circuit_, source);
        requireSize(messages, rows_);
        auto destination = circuit_.addLocalPermGate(
            circuit_.addShuffleGate(messages, sd_group_), public_orders_.source_to_destination);
        auto gathered = circuit_.addSubCircGather(
            destination, public_orders_.destination_to_vertex, vertices_, dv_group_);
        gathered.resize(vertices_);
        return gathered;
    }

    Wires aggregate() {
        return aggregate([](auto&, const Wires& source) { return source; });
    }

    // ApplyV(circuit, vertex_index, old_value, aggregate) returns one wire.
    // Caller can add delta-edge contributions to aggregate before this call.
    // All vertices read the same old snapshot, then the state changes once.
    template <typename ApplyV>
    const Wires& applyVertices(const Wires& aggregates, ApplyV&& apply_v) {
        requireInit();
        requireSize(aggregates, vertices_);
        Wires next(vertices_);
        for (std::size_t v = 0; v < vertices_; ++v)
            next[v] = apply_v(circuit_, v, values_[v], aggregates[v]);
        values_ = std::move(next);
        return values_;
    }

    template <typename ApplyE, typename ApplyV>
    const Wires& iterate(ApplyE&& apply_e, ApplyV&& apply_v) {
        return applyVertices(aggregate(std::forward<ApplyE>(apply_e)),
                             std::forward<ApplyV>(apply_v));
    }

    template <typename ApplyV>
    const Wires& iterate(ApplyV&& apply_v) {
        return applyVertices(aggregate(), std::forward<ApplyV>(apply_v));
    }

    // Benchmark boundary after the three masked permutations are opened.
    // Local gates at that level may also execute. This is not secure sorting.
    std::size_t setupEndLevel(const threepc::LevelOrderedCircuit& ordered) const {
        requireInit();
        std::unordered_set<threepc::wire_t> remaining;
        for (const auto* column : {&public_orders_.vertex_to_source,
                                   &public_orders_.source_to_destination,
                                   &public_orders_.destination_to_vertex})
            remaining.insert(column->begin(), column->end());
        std::size_t last = 0;
        for (std::size_t d = 0; d < ordered.depth(); ++d)
            for (const auto& gate : ordered.gates_by_level[d])
                if (gate->type == threepc::GateType::kRec && remaining.erase(gate->out))
                    last = d;
        if (!remaining.empty()) throw std::invalid_argument("Circuit does not contain Graphiti setup");
        return last;
    }

private:
    static std::size_t checkedRows(std::size_t vertices, std::size_t edges) {
        // Propagate/Gather use int loop counters internally.
        if (vertices == 0 || vertices > INT_MAX || edges > INT_MAX - vertices)
            throw std::invalid_argument("Graphiti requires 1 <= V <= V+E <= INT_MAX");
        return vertices + edges;
    }

    static void requireSize(const Wires& wires, std::size_t size) {
        if (wires.size() != size) throw std::invalid_argument("Graphiti wire vector has wrong size");
    }

    void requireInit() const {
        if (!initialized_) throw std::logic_error("Call Graphiti init first");
    }

    Wires maskOrder(const Wires& labels, int& group) {
        group = circuit_.freshPermGroupId();
        auto shuffled = circuit_.addShuffleGate(labels, group);
        Wires opened;
        opened.reserve(rows_);
        for (auto w : shuffled) opened.push_back(circuit_.addRecGate(w));
        return opened;
    }

    Wires anchor(const Wires& payload) {
        const auto w = public_orders_.vertex_to_source.front();
        auto zero = circuit_.addGate(threepc::GateType::kSub, w, w);
        Wires anchored;
        anchored.reserve(payload.size());
        for (auto value : payload)
            anchored.push_back(circuit_.addGate(threepc::GateType::kAdd, value, zero));
        return anchored;
    }

    threepc::Circuit<T>& circuit_;
    std::size_t vertices_, rows_;
    bool initialized_ = false;
    int vs_group_ = -1, sd_group_ = -1, dv_group_ = -1;
    OrderWires public_orders_;
    Wires values_;
};

}  // namespace graphiti
