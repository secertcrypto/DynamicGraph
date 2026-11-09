# Two-party Graphiti initialization (libSSS routing port)

Exactly P0 and P1 perform Init and all order conversions. The standalone
Graphiti::init2pc target links EMP, not the NPH evaluator or a trusted helper.
It implements shuffle-then-sort with comparison results opened, not endpoint
values. This is a semi-honest implementation, not malicious security or a UC
security claim.

## Build and run

From the dynamic-graph root, with local EMP 0.2.5 and vendored EMP OT present:

~~~sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCRIS_BUILD_INIT2PC=ON
cmake --build build --target graphiti_init2pc_run graphiti_init2pc_primitives -j2
ctest --test-dir build -R '^graphiti_init2pc_' --output-on-failure
~~~

The tests start exactly two independent processes. To run them manually,
use the included PUBLIC test inputs in two terminals:

~~~sh
# Terminal 1: P0
./build/test/graphiti/init2pc/graphiti_init2pc_run 0 29800 \
  test/graphiti/init2pc/example-party-0.json /tmp/init-p0.json

# Terminal 2: P1 (append P0's IP if on separate hosts)
./build/test/graphiti/init2pc/graphiti_init2pc_run 1 29800 \
  test/graphiti/init2pc/example-party-1.json /tmp/init-p1.json
~~~

Use an unused port. The output files contain each party's local shares and
the public sorting mappings. The protocol never reconstructs the graph.
`test/graphiti/init2pc/check.py` reconstructs the public test fixtures for correctness only.

Library sources live under `src/2pc/`. Test drivers and fixtures
live under `test/graphiti/init2pc/` and are built only with `BUILD_TESTING=ON`.
`CRIS_BUILD_INIT2PC=OFF` disables this library and its integration tests; the
existing NPH initialization remains available.

If only this module is required, set -DCRIS_BUILD_RISA_READ=OFF to avoid
configuring Spiral. If the checkout lacks third_party/emp-tool/emp-tool/emp-tool.cpp,
use a full EMP 0.2.5 checkout via -DEMP_TOOL_SOURCE_DIR=/absolute/path/to/emp-tool.
The module uses the existing `src/sfssgen/vendor/emp-ot/iknp.h` headers,
which must be present in the checkout.

## API and sharing conventions

~~~cpp
#include "src/2pc/init.h"
using namespace graphiti::twopc;

emp::NetIO io(party == 0 ? nullptr : peer_ip, port);
Initializer initializer(party, io);
auto state = initializer.Init(V, local_src, local_dst); // default Z_V indices
auto source = initializer.VertexToSource(state, local_rows);
auto destination = initializer.SourceToDestination(state, source);
auto vertex = initializer.DestinationToVertex(state, destination);
~~~

Link the consuming target to Graphiti::init2pc.

- N = V + E, with 1 <= V <= N <= 2^22, vertex rows first (row i is vertex i),
  then edge rows.
- Default endpoint shares are canonical in Z_V, matching the existing engine's
  BaseDagShare. A two-party circuit normalizes their sum into Z_(2^64) without
  opening the carry or endpoint. IndexSharing::Ring64 accepts already normalized
  additive sharing in Z_(2^64).
- Input endpoints must reconstruct to IDs in [0,V). This is an honest-input
  precondition, not a malicious input validity proof.
- The row matrix is row-major, with any positive number of 64-bit columns.
  Every column follows the same row permutation. Payload shares travel in
  Z_(2^64), or by projection to the existing power-of-two value ring.
- To permute endpoint columns themselves, use ring64 shares (the working
  rows are in Z_(2^64)); the original Z_V shares are for Init's endpoint inputs.
- Public sorting maps use output[i] = input[map[i]] (pull).
- ExportOrderShares(state) outputs additive shares of three PUSH label arrays,
  compatible with graphiti::OrderWires:
  output[label[i]] = input[i]. It performs additional two-party shuffles;
  the labels are not opened.
- Local states store only that party's own random factors. They are reusable
  across changed payloads while the base topology and row identities are fixed.
  Every application uses fresh masks and advancing OT randomness.
- Both parties must execute the same sequence of operations, on matching states
  and public dimensions. Calls on one channel are sequential, not thread-safe.

## Initialization and leakage

1. Each party samples independent A and B factors using a cryptographic PRG and
   unbiased Fisher-Yates sampling. A full hidden shuffle applies P0's factor
   followed by P1's. Neither party knows the full permutation.
2. Locally form source and destination sort-key shares. Carry the two columns
   through A, then carry the destination column through B.
3. Execute two bitonic sorts. Values are converted to XOR-shared bits, compared
   with OT-based GMW gates, and ONLY comparison outcomes are opened. These
   public outcomes permit local swaps and produce the public maps S and D.
   The endpoints and key values remain shared throughout.
4. Materialize additive shares of C's pull mapping by applying inverse B then
   inverse A to a public identity-label vector. Retain the private A/B factors
   to apply the same return transformation on later payloads.

Sort keys include a unique original row ID as a SECRET tie-break:
(2*src + (1-isV))*N + row_id and (2*dst + isV)*N + row_id.
Only the pre-shuffle position is public; its shuffled alignment is not opened.
This makes the keys distinct, so the fixed sorting network's comparison
transcript is determined by the relative-order permutation, including duplicate
edges. Without distinct tie-breaks, an equality-dependent transcript would need
a separate leakage analysis. Padding values sort after every real key, and are
public, fixed dummy positions.

For source order each vertex precedes its outgoing edges; for destination order
incoming edges precede their vertex. Within equal endpoints/type, original
row order breaks ties, matching the engine's stable sorting convention.

The intended semi-honest argument is composition of OT-secure masked switches
and Boolean gates. Each unopened honest random factor hides the alignment; the
opened sort maps are randomized relative orders. There is no open of the
original-to-sorted graph mapping. Correctness tests do not constitute an
independent security audit.

## Counts: logical shuffles versus OSNs

A logical two-party hidden shuffle uses TWO private-owner OSN evaluations.
The implementation of the composite inverse retains four factors; it does
not compile them into a single OSN under a secret-shared route.

| Operation | Private-owner OSN evaluations |
|---|---:|
| A during Init | 2 |
| B during Init | 2 |
| Materialize C during Init | 4 |
| Vertex to Source | 2 |
| Source to Destination | 2 |
| Destination to Vertex | 4 |
| Export all three push-label arrays | 8 |

Each OSN uses a padded Beneš network, O(N log N) switches per payload column.
Sorting uses O(N log^2 N) comparisons with width O(log(VN)).
Index normalization and exporting labels are additional work. This reference
implementation does NOT claim Graphiti's original constant factors or the
upstream libSSS Silent-OT performance.

## Complete two-party execution

The library also includes `Evaluator`, a uint64 arithmetic circuit evaluator.
It supports local arithmetic, batched OT multiplication, secret Eqz/Ltz,
explicit reconstruction, grouped shuffle/unshuffle and public permutations.
`prepare()` only validates and allocates; cryptographic work including base OT
is charged during evaluation. Multiplication uses two sets of bit-choice OTs
for the cross products, rather than helper-generated triples. Eqz/Ltz use
secret bit decomposition and Boolean gates; their predicates are not opened.
The implementation remains semi-honest and its tests are correctness checks.

Graphiti circuit benchmarks accept `--protocol two_party --num-parties 2`.
Their precomputed-order input contract is unchanged. `bench_InitGraphiti2pc`
measures actual shared-endpoint initialization plus secret order-label export;
`microbench_InitGraphiti` remains the separate legacy synthetic cost model.

```sh
./run.sh bench_BfsMpaGraphiti --protocol two_party --num-parties 2 \
  --num-verts 100 --num-edges 900 --num-hops 4
./run.sh bench_InitGraphiti2pc --protocol two_party --num-parties 2 \
  --num-verts 100 --num-edges 900
```

The dynamic engine supports `Config::two_party=true` (`"backend":"two_party"`
in input JSON). Both initialization and all iterations use only P0/P1. It
retains A/B factors and public maps rather than exporting and re-masking order
labels each round. The existing `two_party_init=true` flag on the NPH backend
still selects mixed mode, with P2 required for subsequent iterations.

```sh
python3 benchmark/dynamic_graph/run.py --binary build/benchmark/bench_dynamic_graph \
  --backend two_party --vertices 16 --base-edges 32 \
  --insert-schedule 0,2,0,1 --repeat 3 --check
ctest --test-dir build -R 'two_party|init2pc' --output-on-failure
```

Dynamic benchmark schema version 2 reports `two_party_bytes` and `risa_bytes`
separately. Earlier results included initialization in `risa_bytes`. Pure
runtime Init includes weight alignment and Spiral setup but does not require
ExportOrderShares; standalone initialization/export timing is a different scope.

Tests cover singleton/edgeless graphs, isolated vertices, non-power-of-two sizes,
self-loops, parallel edges, both index-sharing modes, secret C, exported labels,
repeated conversions, arithmetic wraparound, sign-bit boundaries and exact LWR
carry correction. Full engine tests compare insertion/iteration, weighted sums
and reachability to independent plaintext results using exactly two processes.
