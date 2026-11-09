# DynamicGraph RISA batch update

This port lives in DynamicGraph. It uses its existing EMP target and Graphiti
module. The ordinary full-tree distributed DPF, LWR KH-PRF and vendored OT
sources in ../sfssgen are from secertcrypto/dynamic-secure-graph at bf89198;
see ../sfssgen/LICENSE, NOTICE and vendor/emp-ot/LICENSE. The backend adapter
adds contiguous vector opening. No additional EMP installation is needed.

## Two interfaces

Each edge has a separate compiled single-point state. Compile once from one
local canonical index share in Z_N (including N not a power of two). Inputs
to a batch are ordered references to states and one local message share per
edge; both parties must agree on N, public ordering, t, and batch length.

```cpp
auto pp = risa::Setup(N);
auto a = risa::Compile(backend, pp, dest_a_share);
auto b = risa::Compile(backend, pp, dest_b_share);
std::vector<risa::BatchInput> batch{{a, message_a}, {b, message_b}};

// t=0: one ApproximateContribution per edge, in batch order.
auto per_edge = risa::UpdateBatch(backend, pp, 0, batch);

// t=1: one N-word local aggregate. Supply the new round's message shares.
batch[0].message_share = next_a;
batch[1].message_share = next_b;
auto gathered = risa::UpdateBatchGather(backend, pp, 1, batch);
```

Use either output layout for a particular invocation. To get both layouts at
the same t, call UpdateBatch once and sum its output locally. Never reissue
the same t on a state. Repeated state references, consumed/decreasing t,
wrong state size and wrong party are rejected locally before communicating.
An empty batch sends nothing; its gathered result contains N zero shares.

For B edges the backend exchanges one B-word vector (one send and one receive
per party; 8B sent payload bytes, not counting transport overhead). This is a
protocol exchange, not a guarantee of one TCP packet. Public input expansion
is computed once per batch. Gather writes the coordinate contributions directly
into one N-word result instead of allocating B such arrays. There is no new
multi-point DPF or implicit thread parallelism. Each stored edge still needs
O(ND) space and each round O(BND) local work (D=512). The graph adapter
LwrDeltaUpdates aggregates active edges by public edge ID via UpdateBatchGather.

The LWR backend has bounded error: target residual in {-1,0,1}, elsewhere
in {0,1} modulo 2^64. Each contribution has error bound 1; the batch sum has
bound B before ApplyV, relative to an exact base aggregate. Nonlinear vertex
operations may amplify errors. The test suite checks correctness, not privacy.

## Build and run

Use the DynamicGraph revision with the Graphiti module and local EMP patches.
From its repository root (EMP 0.2.5 populated in third_party/emp-tool):

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 2 --target risa_update_test risa_batch_test risa_update_example risa_graphiti_example
ctest --test-dir build -R '^risa_' --output-on-failure
python3 test/risa/run_two_party.py build/test/risa/risa_update_example --port 28465
```

Standalone RISA uses exactly two compute processes. For the runnable
Graphiti bridge (NPH only) use:

```sh
python3 test/run_graphiti_module.py build/test/risa/risa_graphiti_example nph 18500
```

This launches both compute parties **and** the graph preprocessing helper.
Only P0/P1 exchange the RISA batch, on a separate port (base+50). The helper
does not receive delta shares. NphDeltaInput constructs one input wire per
party per vertex; the wires add both local contributions to Graphiti's base
Gather wires. P0/P1 stage only their own shares before online execution, then
Graphiti applies ApplyV once per vertex. Reconstruction in this example is
only a check on a public fixture. This interface does not accept new external
messages after the existing runner starts online execution, and does not
implement RISA.Read. Network endpoints assume the same semi-honest/private
channel setting as the source implementation; no new security proof is claimed.

For single and batched reads, see [READ.md](READ.md).
