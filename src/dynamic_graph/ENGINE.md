# Stateful dynamic graph computation on CrIS-MPC

This runtime implements Init, Insert and Iter on a fixed vertex set. Its
`Config::two_party=true` backend runs only P0/P1, including initialization,
Graphiti message passing, OT arithmetic, exact decoding and RISA read/update.
Set `"backend": "two_party"` in both input JSON files to select it. It requires
`CRIS_BUILD_INIT2PC=ON` and implies `two_party_init=true`.

The default remains two NPH compute parties plus one helper. In that mode,
`Config::two_party_init=true` selects two-party initialization followed by NPH
iterations. Only computing parties retain endpoint/message/vertex shares.

Pure two-party mode retains `Initializer` and `InitState` for the graph lifetime.
Propagate computes vertex differences, performs VertexToSource and local prefix
sums. Gather moves destination prefix sums and messages together back to vertex
order and extracts group differences. ApplyE/ApplyV and carry correction run on
the two-party uint64 evaluator. The original callback API is preserved. No NPH
network, dealer triples or helper process is created on this path.

Communication counters separate `two_party_tx` from `risa_tx`; their sum is the
EMP channel's sent payload count. `nph_tx` and `nph_rx` remain zero in pure mode.
Two-party setup/OT costs are included in the operation that executes them.

## Build and try the public demo

Run from the CrIS-MPC repository root with local EMP populated:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCRIS_BUILD_RISA_READ=ON
cmake --build build --parallel 2 --target dynamic_graph_run dynamic_decode_test
ctest --test-dir build -R '^dynamic_' --output-on-failure
python3 test/dynamic_graph/check_iterations.py build/dynamic_graph/dynamic_graph_run --port 19500
```

The script launches three processes and checks each round against an independent
cleartext recurrence on public test data. To operate on separate input files:

```sh
# Generate public demonstration files in a NEW directory. The deterministic
# sharing is for this public fixture only.
python3 test/dynamic_graph/check_iterations.py --export demo-inputs
# Start each command in a separate terminal (or put them in the background).
./build/dynamic_graph/dynamic_graph_run 0 19500 demo-inputs/party-0.json p0.jsonl
./build/dynamic_graph/dynamic_graph_run 1 19500 demo-inputs/party-1.json p1.jsonl
./build/dynamic_graph/dynamic_graph_run 2 19500 demo-inputs/party-2.json p2.jsonl
```

Each output line reports the completed iteration count, active delta count
and **only that process's local vertex shares**. P2's vertex vector is empty.
The runtime has no output-opening step. Reconstruction by the test script is
outside the runtime. NPH uses the original directed ports base + 1024*i + j
(i=0,1,2; j=0,1). RISA uses a separate TCP channel at base+50. The launcher
uses localhost; the network configuration inherits NetNP's single peer-address
convention and the existing backend's channel/security assumptions.

## Input format and programming interface

Each party gets its own JSON file. Public keys are `vertices`, `base_edges`,
`max_delta`, optional `shift`, optional `application` (default `weighted_sum`),
optional `backend` (`nph` or `two_party`), optional `two_party_init` (default `false`), and an ordered `rounds` array.
Each round has `insert`: a list of new edges to
insert before executing the iteration. P0/P1 supply `base_rows` in vertex
order: the first V rows correspond to vertex IDs 0..V-1, source and destination
both that ID; subsequent rows are base edges in any order. Each row is
`[source_share, destination_share, data_share]`. Endpoint shares must be
canonical modulo V and the two shares must jointly encode a valid DAG list.
Vertex data are initial H; edge data are application inputs (weights).
Delta records have `id,src,dst,data`; P2 uses only each public `id` and does
not need `base_rows`. Participants must agree on dimensions, application,
operation order, delta IDs, scale and number of rounds. No malicious-input
validation of secret semantics is performed.

For C++ applications link `DynamicGraph::engine` and include
`dynamic_graph/engine.h`. Create the engine on all three processes:

```cpp
auto state = dynamic_graph::Engine::Init(config, local_base_dag, program);
// At a boundary between rounds:
state->Insert(public_handle, local_src_share, local_dst_share, local_edge_data);
state->Iter();
const auto& H_next_local = state->vertex_shares();
```

A duplicate handle or insertion beyond Dmax is rejected before compilation.
Parallel edges, self-loops and isolated vertices are allowed. The fixed base
graph never changes; insertion compiles only the new edge's states. A failed
interactive step invalidates the session; do not replay it with old masks.

## Protocol flow

1. **Init:** Securely reduce endpoint shares mod V and decompose them into
   secret key bits; run the existing stable MPC radix-sort to compute source
   and destination ranks. A public tie-break places vertex rows first for
   source groups and last for destination groups. If s,d are push labels,
   the three maps are s, the vector with sd[s[i]]=d[i], and the vector with
   dv[d[i]]=i. These maps and the source-aligned edge-data shares are computed
   once and retained. No endpoint is opened and there is no input owner with
   the entire graph. Initialize read parameters in Z_q and LWR update in Z_M.
2. **Insert:** Store an edge's data share and compile separate reusable
   read and update states at its source and destination, respectively.
3. **Iter:** Take the current H shares. Use Graphiti Propagate, MPC ApplyE and
   Gather for base edges. Read all delta sources from the *same* H via ReadBatch.
   Evaluate the same MPC ApplyE on each source share and edge-data share,
   encode the message shares, and use UpdateBatchGather in Z_M. Securely decode
   each summed delta coordinate **once**, add to the base aggregate, execute
   ApplyV once per vertex, save the local output shares into the first V rows,
   and advance the iteration counter. No future edge schedule is needed by
   the Engine API.

In the NPH backend, the base graph's secret sorting labels are reused. Because the original NPH
evaluator is a one-circuit runner, this implementation evaluates consecutive
MPC phases on a persistent network, with fresh triples, masks and PRGs each
time. It re-masks the cached transition labels with fresh shuffle groups when
building that round's Graphiti circuit; it does **not** re-sort the base DAG
or recompile old delta states. Account for this extra online/preprocessing
cost when comparing to the paper; this is not the paper's optimized persistent
shuffle representation. No raw share is passed to getOutputs(), which would
reconstruct it: phase boundaries retain only the local evaluator share.

## Exact decoding from approximate LWR updates

The current LWR payload is a uint64, so M=2^64 and q=2^(64-ell).
Use 1<=ell<=63 and S=2^ell>2*Dmax. Auto mode picks the smallest such scale;
a larger explicit shift is supported. Keeping q=2^64 while increasing M
would require a wider update primitive. Vertex values, weights and messages
are reduced modulo q. Read masking and outputs use that value width. The
unsigned local operation m<<ell implements S*m mod M, including modular wrap.

Each LWR update has coordinate error in {-1,0,1}. For a sum over active
edges, |eta|<=Dmax. Hence y=S*m+eta (mod M) decodes to m mod q since
0<=eta+S/2<S. This recovers exact message aggregates, conditioned on
underlying PIR/DPF/MPC correctness. The update primitive itself remains
approximate.

For shares y0,y1 first add S/2 to P0's share modulo M. Split each resulting
share wi=hi*S+loi locally. Inside MPC, calculate carry=1-LTZ(lo0+lo1-S) and
form hi0+hi1+carry. The comparison operand is in [-S,S-2] and fits signed
64-bit even when ell=63. The result modulo q is the desired logical right
shift. An overflow across the M boundary changes the high sum by q, which
vanishes modulo q. The low portions, carry and result are not reconstructed.
Independent right shifts on each share **without** this carry are incorrect.
There is no plaintext fallback.

## Applications, costs and verification scope

The default program uses m=h_src*w and H'=H+sum(m) in Z_q. `reachability`
sends a bit indicating a nonzero source and sets the next vertex bit iff it
was active or received a nonzero count. For this application we conservatively
require total base edges + Dmax < q so a positive count cannot wrap to zero.
Custom public deterministic ApplyE/ApplyV callbacks use RingOps (ring
arithmetic, zero test and signed-negative test). Arithmetic is evaluated in
Z_M and projected to Z_q; comparisons scale their input by S before using the
backend gates. Both graph paths use the same functions.

The implementation inherits the Spiral size bound and the cost of one cached
LWR state (~8 KiB per vertex per delta edge per compute party). Functional
tests are small-graph regressions, not a scalability benchmark or a formal
security proof. The secure-init implementation uses the existing MPC sort
rather than claiming a verbatim implementation/cost of the paper's Init.

Verification on the CrIS-MPC port: 144 secure decode boundary cases (including
ell=63 and ring wrap); independent share shifts without the carry would fail
60 cases. Four real three-process fixtures covered 13 successive rounds in
total, verifying every vertex against a cleartext oracle with **zero modular
error**. Cases included colliding delta destinations, insertion between rounds,
parallel/self edges, negative weights, q=16 overflow, an isolated single
vertex, no delta edges, and BFS propagation delayed until the next snapshot.
