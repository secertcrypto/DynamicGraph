# Graphiti message-passing module

`message_passing.h` extracts the running Graphiti computation from the BFS and
PageRank benchmarks into `graphiti::MessagePassing<T>`. It is a header-only
circuit builder; link the CMake target `Graphiti::graphiti`. Networking,
preprocessing, execution, timers, graph generation and result reconstruction
remain the caller's responsibility. Both existing benchmarks now use this API.

## Scope of initialization

`init(vertex_values, orders)` takes secret circuit wires for **already computed
order-transition permutations**. It masks and opens those permutation labels
once, and retains the shuffle group IDs and vertex wires for subsequent rounds.
It does not take plaintext endpoints, reconstruct vertex data, or mark any
application value as an output.

The upstream benchmark obtains these permutations from a graph known to P1.
`plain_graph.h` contains that input-owner utility, including conversion from
pull permutations to destination labels. Never use this utility by opening a
private shared topology. A caller that starts with shares of the topology must
first supply a secure permutation-generation procedure.
`microbench_InitGraphiti` measures this module's actual `init` call from
precomputed secret order labels; it does not perform that sorting procedure.
This extraction does not implement the complete initialization protocol of
Figures 13--14 of [the paper](https://eprint.iacr.org/2024/1756).

For initialization from shared endpoints, the separate
[`Graphiti::init2pc`](../2pc/README.md) library provides two-party shuffle and
sorting, reusable order conversions, and `ExportOrderShares` for this module's
destination-label inputs. It runs directly on an EMP channel rather than
appending gates to this circuit. The dynamic graph engine exposes it through
`Config::two_party_init`; the precomputed-order API below remains available.

Permutation contracts (all have `V+E` entries):

* Vertex order: rows `0..V-1` are the vertices in vertex-index order, followed
  by edge rows.
* Source order: increasing source vertex, with each vertex row before its edges.
* Destination order: increasing destination vertex, with each vertex row after
  its incoming edges.
* Every secret permutation uses destination labels: `out[label[i]] = in[i]`.
  `destinationLabelsFromPull<T>` converts `out[i] = in[pull[i]]` labels and
  rejects duplicates and out-of-range indices on the input owner's side.

Inputs must satisfy these contracts. The module checks public vector lengths,
not secret permutation validity. The underlying model remains semi-honest.

## API

```cpp
#include "src/graphiti/message_passing.h"

threepc::Circuit<std::uint64_t> circuit;
// Build V vertex-value wires and three V+E secret permutation columns here.
graphiti::MessagePassing<std::uint64_t> graph(circuit, V, E);
graph.init(vertex_values, {vertex_to_source,
                           source_to_destination,
                           destination_to_vertex});

// Identity ApplyE; ApplyV adds the aggregate to the old vertex value.
auto apply_v = [](auto& c, std::size_t v, auto old_value, auto aggregate) {
    return c.addGate(threepc::GateType::kAdd, old_value, aggregate);
};
for (std::size_t t = 0; t < rounds; ++t) graph.iterate(apply_v);
const auto& final_vertex_wires = graph.vertices();
```

The full overload is `iterate(apply_e, apply_v)`:

* `apply_e(circuit, source_rows)` returns `V+E` message wires in **source
  order**. It must express data-dependent operations as MPC gates, not C++
  branches on secrets. Vertex rows are interspersed; Gather excludes their
  messages. The default callback is identity.
* `apply_v(circuit, vertex_index, old_value, aggregate)` returns one vertex
  wire. All calls read the same old snapshot. The module updates its vertex
  state only after building every vertex's update.
* `toSourceOrder(payload)` aligns a `V+E` vertex-ordered column, such as edge
  weights, for use in ApplyE. An unchanged column can be aligned once and reused.

For base-plus-delta computation, defer ApplyV:

```cpp
auto base = graph.aggregate(apply_e);       // V wires; preserves H^t
for (std::size_t v = 0; v < V; ++v)
    base[v] = circuit.addGate(threepc::GateType::kAdd, base[v], delta[v]);
graph.applyVertices(base, apply_v);         // one ApplyV, now H^{t+1}
```

`delta` must already be wires in this circuit and in the same vertex order.
This is a connection point, not an implementation of RSIA or a claim that the
two modules' security has been proved to compose. It is valid to import
additive shares by giving each share to a separate input owner and adding the
input wires inside MPC; this does not reveal the reconstructed input.

`vertices()` returns wire IDs, not opened values or a backend-native share
buffer. Copy the vector if you need to retain an earlier snapshot. The module
must stay in its original circuit; it must not be copied into another circuit.

## Building versus executing

`init`, `aggregate`, `applyVertices` and `iterate` **append circuit gates**.
Build the requested rounds first, stage inputs, then run `offline` and `online`
using `IProtocolRunner`. The runner does not support adding new inputs after
online execution begins. An indefinitely running service with online edge
insertions needs a separate incremental-execution/share-import interface; this
refactoring does not add one. Do not repeat evaluation of the same gate with
consumed preprocessing. Repeated rounds append fresh gates while reusing only
the intended permutation groups.

For the existing benchmark's timing convention,
`setupEndLevel(ordered_circuit)` identifies the final level opening masked
permutation labels. Local gates at that level may also execute. This timing
does not include private-topology sorting.

## Runnable example and regression coverage

`test/graphiti/graphiti_module.cpp` has no benchmark dependency. It builds two rounds,
uses weighted ApplyE, combines a delta contribution before ApplyV, imports
additive shares, and checks results on parallel edges, a self-loop, an isolated
vertex, and a separate one-vertex zero-edge graph. Reconstruction occurs only
in the example's final check. The fixture is public and tests correctness, not
privacy. The BFS benchmark normalizes reachability bits every round. The PageRank
benchmark remains an undamped, unnormalized sum workload, applying its secret
`rho` multiplier in every round.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target graphiti_module bench_BfsMpaGraphiti bench_PrMpaGraphiti --parallel
ctest --test-dir build -R '^graphiti_module_' --output-on-failure
```

CTest launches three processes for each case: NPH with two computing parties
plus a helper (optimized and generic shuffle), and RSS3 with three computing
parties. Python 3 is needed for the launcher.

## Helper-free evaluator

With `CRIS_BUILD_INIT2PC=ON`, linking `Graphiti::graphiti` enables
`ProtocolKind::TwoParty` in `makeProtocolRunner<uint64_t>`. Use exactly P0/P1.
The circuit API and precomputed-order input contract above are unchanged;
shared endpoint initialization is provided by `Graphiti::init2pc` and can
export secret destination labels for the circuit. The dynamic engine's
`Config::two_party` path instead retains the initializer's private factors
across rounds and performs Propagate/Gather with those order conversions.

The two-party evaluator supports uint64 arithmetic, secret Eqz/Ltz, explicit
opening, and grouped permutations. Its offline call validates/allocates only;
OT setup and arithmetic cost occur during evaluation. Predicates in iterations
remain secret, unlike the explicitly opened comparisons used during sorting.
