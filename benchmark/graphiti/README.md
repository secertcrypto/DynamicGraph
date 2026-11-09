# Graphiti benchmarks

The full benchmark runs the two-party initializer and reusable order transitions
from [`src/2pc`](../../src/2pc/README.md), followed by application iterations.
The older drivers benchmark the current [`Graphiti::graphiti`](../../src/graphiti/README.md)
module and `threepc::protocol::IProtocolRunner`. Each executable builds its
workload in a small source file; `common.h` / `common.cpp` handle CLI validation,
input staging, execution, communication accounting, and JSON output.

## Workloads

| Target | API and workload |
| --- | --- |
| `bench_FullGraphiti` | Real shared-endpoint Init followed by persistent two-party iterations; `weighted_sum` or `reachability` |
| `bench_BfsMpaGraphiti` | `MessagePassing::init` and `iterate`; bounded-hop reachability |
| `bench_PrMpaGraphiti` | `MessagePassing::init` and `iterate`; incoming-sum PageRank workload |
| `microbench_InitGraphiti` | Legacy synthetic shuffle/comparison cost model; no real sorting |
| `bench_InitGraphiti2pc` | Real shared-endpoint two-party initialization and secret order-label export |
| `bench_shuffle` | `Circuit::addShuffleGate` repeated with one permutation group |

For BFS/PageRank, P1 owns the graph inputs. It generates a deterministic graph, computes its
order permutations locally with `plain_graph.h`, converts them to destination
labels, and supplies those labels as secret circuit inputs. Only vertex values,
order labels, and application inputs used by the module are staged. The API
initializes from precomputed order labels; it does not securely sort a private
shared topology. Graph generation and local permutation construction are
outside the protocol timings.

BFS starts with one reached source vertex. Every round computes
`reached[v] = (old[v] + incoming[v] != 0)` inside MPC, keeping values Boolean
and avoiding accumulated path-count overflow. Both RSS3 and NPH support this
operation. `--skip-final-applyv` retains the legacy option name and selects raw
ring path counts throughout all rounds; those counts are also checked.

PageRank retains the simplified integer-ring workload: initial values and
secret `rho` are one, and every round computes `rho[v] * incoming[v]`, including
the last round. It has no degree normalization or damping and is not
conventional floating-point PageRank.

`bench_InitGraphiti2pc` checks all three exported secret order-label arrays against the plaintext fixture; the legacy synthetic executable has no sorting correctness check. Application regression
tests exercise the initialized order transitions. Repeated shuffle checks the
output multiset; it does not reveal or independently verify the hidden permutation.

## Build and tests

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 4
ctest --test-dir build -R '^graphiti_' --output-on-failure
```

Programs are under `build/benchmark/`. The module example is now
`test/graphiti/graphiti_module.cpp`, with its executable under
`build/test/graphiti/`. Regression tests cover RSS3, two- and three-party
NPH, generic shuffle, and P0 reconstruction, including zero-edge graphs,
long BFS runs, raw counts, invalid arguments, and JSON phase accounting.

## Full initialization and iteration benchmark

From the repository root:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCRIS_BUILD_INIT2PC=ON
cmake --build build --target bench_FullGraphiti -j4
python3 benchmark/graphiti/run_full.py \
  --vertices 100 --edges 900 --rounds 5 \
  --application weighted_sum --repeat 3 --check
```

The launcher starts P0 and P1 once per trial. Each worker receives additive
shares of endpoint columns, vertex values and edge weights. `init` measures
connection setup, `Initializer::Init` on the shared topology, and private weight
alignment. Every `iter` performs Propagate, ApplyE, Gather and ApplyV, reusing the
private permutation factors and OT contexts. It does not export precomputed
order labels. There is no RISA setup, insertion, read, update or error decoding.
All cryptographic setup/OT work is charged where it executes, including the first
use of an arithmetic OT context; there is no omitted offline phase.

Options:

| Option | Meaning |
| --- | --- |
| `--vertices V` / `--num-verts V` | Vertex count; default 16 |
| `--edges E` / `--base-edges E` / `--num-edges E` | Edge count; default 24; zero allowed |
| `--rounds K` / `--num-iters K` | Iterations after one initialization; default 3 |
| `--application weighted_sum\|reachability` | Same application formulas as the dynamic engine |
| `--max-delta D`, `--shift ell` | Match the dynamic engine's application ring; no insertions are performed |
| `--seed S` | Shared public fixture seed; default 42 |
| `--repeat R`, `--warmup W` | Measured trials and excluded warmups; defaults 3 and 0 |
| `--port P`, `--threads T`, `--timeout SEC` | Local launch settings; defaults 22500, 1 and 300 |
| `--check` | Check every iteration against the plaintext oracle outside timing |
| `--out DIR`, `--binary FILE` | New result directory and worker executable |

`weighted_sum` computes `m=h_src*w` and `h_next=h_old+sum(m)`.
`reachability` starts from vertex 0, propagates nonzero-state bits, and retains
each reached vertex. Both use `src/graphiti/ring_program.h`, shared with the
dynamic engine. They are integer-ring applications, not full PageRank or
fixed-point arithmetic. `ell` is the dynamic encoding shift: values live in
`Z_(2^(64-ell))`; it is not a count of fractional bits. Automatic shift selection
uses the smallest `ell>=1` with `2^ell > 2*D`. Match `D`/`ell` when comparing runs.

Results contain `metadata.json`, `samples.csv`, `summary.csv`, `parties.jsonl`
and per-trial process logs under `Results/graphiti_full/` by default. Summary rows
are `init`, one `iter` per round, and `total`. Times use the maximum party time
per phase, then sum phases within each trial before taking the total median.
Communication sums both parties' sent application payload bytes; received bytes
are checked against the peer's sends, not added again. All communication is
`two_party_bytes`; `nph_bytes` and `risa_bytes` are zero. Input generation and
loading, control pipes, output checks and TCP/IP overhead are excluded.

For a matching dynamic run without insertions:

```bash
python3 benchmark/graphiti/run_full.py \
  --vertices 100 --edges 900 --rounds 5 --application reachability \
  --max-delta 100 --shift 8 --seed 42 --repeat 1 --check
python3 benchmark/dynamic_graph/run.py --backend two_party \
  --vertices 100 --base-edges 900 --rounds 5 --insert-per-round 0 \
  --application reachability --max-delta 100 --shift 8 --seed 42 --repeat 1 --check
```

Both launchers share fixture generation, additive sharing and the plaintext
oracle; `workload_sha256` and `q` in metadata should match. The dynamic engine
still performs its own RISA setup, which is included in its initialization cost.
The static benchmark requires `V+E <= 2^22` and only `CRIS_BUILD_INIT2PC`; it can
be built with `CRIS_BUILD_RISA_READ=OFF`.

## Insertions with full Graphiti reinitialization

`run_rebuild.py` uses the same `bench_FullGraphiti` worker, input generator and
applications, but inserts batches of edges between iterations. It preserves
the last iteration's secret vertex values, appends new edges to all existing
edges, and runs real Init and weight alignment on the entire updated graph.
It retains the TCP connection and OT contexts across reinitializations; it
does not open the graph or reset application values to the initial fixture.

```bash
cmake --build build --target bench_FullGraphiti bench_dynamic_graph -j4

# Full Graphiti rebuild baseline: initialize, then (reinitialize, iterate).
python3 benchmark/graphiti/run_rebuild.py \
  --vertices 100 --base-edges 900 --insert-schedule 0,10,0,10 \
  --application weighted_sum --max-delta 20 --seed 42 --repeat 3 --check

# Dynamic counterpart on the same graph, insertions, value ring and algorithm.
python3 benchmark/dynamic_graph/run.py --backend two_party \
  --vertices 100 --base-edges 900 --insert-schedule 0,10,0,10 \
  --application weighted_sum --max-delta 20 --seed 42 --repeat 3 --check
```

Use `--rounds K --insert-per-round B` for fixed-size batches, or
`--insert-file FILE` for the same public per-round `[id,src,dst,weight]` format
as the dynamic launcher. Without a schedule, the default is three rounds with
one insertion per round. `--max-delta` defaults to the total insertion count.
`V+E`, including every inserted edge, must stay at or below `2^22`.

The default `--reinit-policy every_round` follows a literal rebuild-before-each-
iteration baseline, including empty insertion batches. For a baseline that
only rebuilds when topology changes, use `--reinit-policy on_insert`: an empty
batch then produces a zero-communication `reinit` row and reuses the old state.
The policy is recorded in metadata. Compare using `on_insert` when avoiding
unnecessary rebuilds on unchanged snapshots is part of the baseline definition.

Results go to `Results/graphiti_rebuild/`, with `init`, per-round `reinit` and
`iter`, and `total` rows. `reinit` includes appending shares, sorting the full
current graph, regenerating permutation state, and aligning all weights.
`inserted` is the new batch size; `active_delta` is the cumulative insertion
count; `current_edges` includes original and inserted edges. `reinitialized`
records whether the phase actually rebuilt. All communication is two-party
computation; there is no RISA cost in this Graphiti baseline.

For end-to-end comparison, use **Graphiti init + all reinit + all iter** versus
**dynamic init + all insert + all iter**. Do not compare just the iteration
columns. Check `workload_sha256`, `q`, application, rounds, seed and backend
for agreement. Each launcher validates every round against the same plaintext
oracle with `--check`. This measures continuing message passing as the graph
changes; it does not restart BFS or PageRank from scratch for every snapshot.

## Options for the older drivers

All programs accept `--pid`, `--protocol rss3|nph|two_party`, `--num-parties`, `--port`,
`--peer`, `--output`, `--pking`, `--disable-optimized-shuffle`, and `--no-check`.
The defaults are NPH with two compute parties, plus a helper whose PID is 2.
RSS3 requires three compute parties. `two_party` runs only P0/P1, requires
`CRIS_BUILD_INIT2PC=ON`, and rejects NPH-only flags `--pking` and
`--disable-optimized-shuffle`. `bench_InitGraphiti2pc` requires `two_party`. `--help` prints workload-specific usage.

Graph workloads require either `--graph-size N` (V=N/10, E=N-V), or both
`--num-verts V --num-edges E`. Zero edges are supported; V must be positive.
Generated edges have no self-loops, so E>0 requires V>=2. `--seed S` selects
the deterministic graph. Graph correctness checks run on every compute party
for at most 100,000 rows; larger workloads and `--no-check` report SKIP.

| Workload | Additional options |
| --- | --- |
| BFS | `--num-hops H` (default 10), `--source-vertex V` (default 0), `--skip-final-applyv` |
| PageRank | `--num-iters R` (default 10) |
| Shuffle | `--vec-size N`, `--num-repeats R` (both required) |

The legacy synthetic initialization retains `--sort-levels`; it is not an option of the real two-party initializer.

## Run

`run.sh` starts all required processes and saves their logs. It searches
`build/` and the current directory, so for `build/` run:

```bash
cd build
../run.sh bench_BfsMpaGraphiti --protocol nph --num-parties 2 \
  --num-verts 100 --num-edges 900 --num-hops 10
../run.sh bench_PrMpaGraphiti --protocol rss3 --num-parties 3 \
  --graph-size 1000 --num-iters 10
../run.sh microbench_InitGraphiti --protocol nph --num-parties 2 --graph-size 1000
../run.sh bench_shuffle --protocol nph --num-parties 2 --vec-size 1000 --num-repeats 10
```

Use `RESULTS_DIR=/tmp/graphiti-results` to change the launcher's results root.
For network emulation, source `network.sh` from the repository root and use
its `tc_lan` or `tc_nph_pairs` functions; the configured port must match the run.

## Timing and results

For BFS/PageRank with RSS3/NPH, `offline` measures preprocessing, `init` measures online
levels through `MessagePassing::setupEndLevel`, and `online` measures remaining
iteration levels. Local gates sharing the setup boundary level belong to
`init`. All requested rounds are built before execution, as required by the
runner API.

For `bench_InitGraphiti2pc`, `online` measures actual initialization plus
secret order-label export and `init` has no protocol work. This preserves the phase name
used by existing evaluation scripts. For repeated shuffle, `online` measures
all shuffle evaluation and `init` likewise has no protocol work.

`total` is the sum of `offline`, `init`, and `online`. Final output reconstruction
is measured separately as `reconstruction`. Graph generation, circuit building,
connections, and local correctness comparisons are excluded from these totals.
JSON records `build_and_input_generation_ms`, circuit dimensions, per-peer
bytes, and peak memory. `details.schema_version` is 2; `correct` is null when
not checked. A failed check or JSON write returns a nonzero exit status.

For `two_party`, circuit `offline` only validates and allocates. OT setup and
all cryptographic computation are included in `init`/`online` when executed;
there is no helper-generated offline material. Do not compare its online-only
numbers to NPH while excluding NPH preprocessing.

`bench_InitGraphiti2pc` and the legacy synthetic model measure different work. BFS now normalizes every hop, and PageRank includes ApplyV in every
round. These measurements must not be mixed with old results as if they
represented identical workloads. Existing target names and the evaluation
scripts' `offline` / `online` fields remain available.

Pure two-party examples from the repository root:

```sh
./run.sh bench_BfsMpaGraphiti --protocol two_party --num-parties 2 --graph-size 1000 --num-hops 4
./run.sh bench_PrMpaGraphiti --protocol two_party --num-parties 2 --graph-size 1000 --num-iters 4
./run.sh bench_shuffle --protocol two_party --num-parties 2 --vec-size 1000 --num-repeats 3
./run.sh bench_InitGraphiti2pc --protocol two_party --num-parties 2 --graph-size 1000
```

The BFS/PageRank circuit benchmarks retain their precomputed-order input model.
For end-to-end shared-topology initialization and persistent iteration, use the
[dynamic graph benchmark](../dynamic_graph/README.md) with `--backend two_party`.
