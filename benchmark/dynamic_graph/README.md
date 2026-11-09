# Dynamic graph benchmark

This benchmark for secertcrypto/dynamic-graph drives the real two-party or three-process NPH + RISA runtime via a separate
worker target; it does not time the Python cleartext oracle.

From the repository root, with the existing local EMP/Spiral dependencies:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCRIS_BUILD_RISA_READ=ON
cmake --build build --parallel 2 --target bench_dynamic_graph
python3 benchmark/dynamic_graph/run.py --binary build/benchmark/bench_dynamic_graph \
  --vertices 16 --base-edges 32 --insert-schedule 0,2,0,1 --warmup 1 --repeat 3 --check
```

On a fresh clone of revision `925ed1a`, the bundled EMP tree is incomplete.
If `third_party/emp-tool/emp-tool/emp-tool.cpp` is absent, clone EMP 0.2.5
next to this repository and pass its path when configuring CMake:

```sh
git clone --depth 1 --branch 0.2.5 https://github.com/emp-toolkit/emp-tool.git ../emp-tool-0.2.5
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCRIS_BUILD_RISA_READ=ON \
  -DEMP_TOOL_SOURCE_DIR="$PWD/../emp-tool-0.2.5"
```

The RISA backend also expects `src/sfssgen/vendor/emp-ot/iknp.h`.
If this directory is absent in a clean clone, apply the accompanying
`dynamic-graph-vendored-ot.patch` from the repository root before building.
Neither dependency needs to be installed system-wide.

The schedule above has four rounds, with 0, 2, 2 and 3 **active** delta edges.
Alternatively use `--rounds 5 --insert-per-round 2`. `--seed` changes the
reproducible public graph and sharing fixtures; `--threads` sets OMP_NUM_THREADS
(default 1). Synthetic directed graphs may contain self-loops and parallel
edges. The initial base graph is generated once and remains fixed across
trials. With the same seed, vertices, and base-edge count, changing the
insertion schedule leaves the initial cleartext fixture unchanged.
Use `--application weighted_sum` (default) or `reachability`.

Use `--backend two_party` for the complete helper-free runtime. It starts only
P0 and P1 and uses two-party initialization, persistent order conversions,
OT arithmetic, and the existing two-party RISA read/update path:

```sh
python3 benchmark/dynamic_graph/run.py --binary build/benchmark/bench_dynamic_graph \
  --backend two_party --vertices 100 --base-edges 900 \
  --insert-schedule 0,2,0,2 --warmup 1 --repeat 5 --threads 1 --check
```

The default remains `--backend nph` (three processes). With that backend,
`--init-backend two_party` selects the older mixed mode: two-party initialization
followed by NPH iterations. Full two-party mode implies two-party initialization
and rejects `--init-backend nph`. `CRIS_BUILD_INIT2PC=ON` is required.
Both two-party modes support at most 2^22 total vertex and edge rows.

The new result schema (`metadata.json`: `schema_version=2`) separates
`two_party_bytes` (OT arithmetic and init/order-conversion protocols) from
`risa_bytes` (RISA/Spiral only). In full two-party mode `nph_bytes` is zero in
every phase. `total_bytes = nph_bytes + two_party_bytes + risa_bytes`.
Older results counted two-party initialization inside `risa_bytes`; compare
total communication across schema versions, not that individual column.
Metadata also records `backend`, `init_backend`, and `num_processes`.

To control the exact inserted data, use a PUBLIC benchmark fixture. Each outer
entry is one round; each edge is [unique public id, src, dst, signed weight].
For example, with at least 5 vertices:

```sh
python3 benchmark/dynamic_graph/run.py --binary build/benchmark/bench_dynamic_graph \
  --vertices 5 --base-edges 5 --insert-file benchmark/dynamic_graph/example-inserts.json --repeat 3 --check
```

Data and endpoints are split into the engine's two local input files; P2's
file contains only dimensions, schedule, and public handles. The generation
seed is for benchmarking only. By default Dmax is the sum of all insertions.
For comparisons using different schedules, fix `--max-delta D` and optionally
`--shift ell` to keep the encoding/value ring comparable. A batch of B active
LWR states needs roughly 8 KiB * V * B per compute party for cached position
data alone. Start small, especially when increasing both V and the edge count.

## Measurements

- **init**: wall time of Engine::Init, including NPH and RISA connection
  setup, secure base sorting, and read setup. Input file construction/loading
  is outside the timed region.
- **insert**: total for *all new edges* inserted before that round; includes
  actual read and update compilation. If no edges are inserted, only loop/timer
  overhead remains and no MPC payload should be sent.
- **iter**: the actual Engine::Iter including fresh preprocessing, base and
  delta paths, MPC ApplyE/ApplyV and secure decoding. This is not an online-only
  time. The benchmark does not change graph processing or fabricate input gates.
- Two or three processes, depending on the backend, are launched once per trial. The parent releases them
  through stdin before each phase, outside its timing and counters. The time
  reported for one phase is the **maximum of the participating processes' local elapsed times**,
  not the sum. The reported total is the sum of these phase maxima in a trial;
  it is not the Python launcher's wall time. Warmups use fresh processes and
  are excluded from the recorded samples/medians.
- Communication is **sent application payload bytes** summed over the participating processes.
  NPH counts include helper links and preprocessing. nph_helper_bytes is a
  subset of nph_bytes, not an extra amount to add. The driver checks each
  NPH link's sender and receiver counts for agreement. EMP sent counters are
  split into two-party computation and RISA categories; P2 sends neither.
  Full two-party Init retains private permutation factors and does not export
  labels for NPH. Mixed-mode Init includes that extra label export. TCP/IP headers, ACKs, retransmissions, control pipes and file I/O
  are excluded. Results reflect local loopback, not emulated WAN latency.
- `--check` stores local output shares only for checking this PUBLIC fixture
  and compares every reconstructed vertex after every round; it is untimed.
  Without it no vertex output file is written.
- The default output is a fresh timestamped directory under Results/dynamic_graph;
  `--out NEW_DIR` selects a specific fresh directory. summary.csv contains stage
  and total medians; samples.csv contains every measured phase of each trial;
  parties.jsonl contains raw per-party times and per-link byte counts;
  metadata.json records parameters and a cleartext workload hash. Keep these
  files together when reporting results. Logs are in trial-*/p{0,1,2}.log.
- Select an unused `--port` (default 19500); NPH also uses base + 1024*i + j
  and RISA uses base + 50. `--timeout` bounds a whole trial in seconds.


  # Graphiti benchmarks

This directory contains the implementation of **Graphiti: Secure Graph Computation Made More Scalable**. See [IACR ePrint 2024/1756](https://eprint.iacr.org/2024/1756). It includes secure BFS and PageRank computation, shuffle protocol benchmarks, and a microbenchmark for Graphiti's initialization. The code is intended for research and experimental comparison.


## Implementations


### `bench_BfsMpaGraphiti`

This benchmark implements bounded-hop breadth-first search over the secret-shared graph list.

Program options:

- `--graph-size N`: Vertices + Edges
- `--num-hops H`: number of BFS expansion rounds; default `10`
- `--source-vertex V`: public source vertex; default `0`
- `--skip-final-applyv`: return raw counts instead of converting them to reachability bits; skips the reference correctness check
- `--no-check`: disable cleartext validation
- `--seed S`: deterministic graph seed

`--graph-size N` sets:

```text
num_vertices = N / 10
num_edges    = N - num_vertices
```

Use `--num-verts V --num-edges E` when an explicit split is required.

Correctness checking is skipped for sufficiently large graphs.

### `bench_PrMpaGraphiti`

This benchmark implements PageRank computation using message passing.

The current benchmark uses `alpha = 1` and a secret-shared `rho = 1`. Its recurrence sums incoming values at each vertex over the ring, starting from vertex values of one. It does not implement conventional normalized, damped PageRank.

Program options:

- `--num-iters R`: PageRank iterations; default `10`
- `--no-check`: disable cleartext validation
- `--seed S`: deterministic graph seed

Correctness checking is skipped for large graphs.

### `bench_shuffle`

This benchmark sequentially applies the same random permutation to a vector using secure shuffle.

Program options:

- `--vec-size N`: number of elements
- `--num-repeats R`: number of shuffle applications

### `microbench_InitGraphiti`

This benchmark builds a synthetic circuit that models Graphiti initialization, including three aligned-column shuffles and source/destination sorts. Each sort instantiates the full balanced quicksort comparison model at its natural depth, $\lceil\log_2(N)\rceil$. Comparisons at the same depth are evaluated in parallel, while consecutive depths and initialization stages retain their communication dependencies.

Program options:

- `--graph-size N`: total number of vertex and edge rows, using the standard `N/10` vertex split
- `--num-verts V --num-edges E`: explicit graph dimensions
- `--seed S`: deterministic graph seed

The model does not implement data-dependent partitioning or swaps, so it does not perform a complete sort or produce valid sorted graph orders.

The number of modeled comparisons grows as $O(N\log N)$, and each comparison creates LTZ and reconstruction gates. Large graph sizes can therefore require substantial circuit-construction time, memory, preprocessing, and communication.

## Build

From the repository root:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

The resulting programs are:

```text
build/benchmark/bench_BfsMpaGraphiti
build/benchmark/bench_PrMpaGraphiti
build/benchmark/bench_shuffle
build/benchmark/microbench_InitGraphiti
```

See the root `README.md` for dependencies and protocol details.

## Run

Run commands from the repository root. `run.sh` starts all processes locally, adds PID and network arguments, and saves their logs.

### BFS

Run BFS with two NPH compute parties and one helper:

```bash
./run.sh bench_BfsMpaGraphiti \
  --protocol nph --num-parties 2 \
  --graph-size 10000 --num-hops 10 --source-vertex 0
```

Run the same workload with explicit graph dimensions:

```bash
./run.sh bench_BfsMpaGraphiti \
  --protocol nph --num-parties 3 \
  --num-verts 1000 --num-edges 9000 \
  --num-hops 10 --source-vertex 5
```

### PageRank

```bash
./run.sh bench_PrMpaGraphiti \
  --protocol nph --num-parties 2 \
  --graph-size 10000 --num-iters 10
```

RSS3 is supported by PageRank. The BFS argument parser currently requires `--skip-final-applyv` with RSS3, so that mode returns raw counts and skips the reachability check:

```bash
./run.sh bench_PrMpaGraphiti \
  --protocol rss3 --num-parties 3 \
  --graph-size 10000 --num-iters 10
```

### Repeated shuffle

```bash
./run.sh bench_shuffle \
  --protocol nph --num-parties 2 \
  --vec-size 10000 --num-repeats 10
```

### Initialization microbenchmark

```bash
./run.sh microbench_InitGraphiti \
  --protocol nph --num-parties 2 \
  --graph-size 10000
```

## Protocol and common options

The Graphiti programs accept these common options:

| Option | Meaning |
| --- | --- |
| `--protocol rss3\|nph` | Select the MPC backend |
| `--num-parties N` | Compute-party count; RSS3 requires `3`, while NPH requires at least `2` |
| `--pking` | For NPH, reconstruct through P0 and redistribute to compute parties |
| `--disable-optimized-shuffle` | Force the generic shuffle protocol based on Chase et. al. instead of the two-party optimized shuffle proposed in Graphiti |
| `--port P` | Base TCP port |
| `--peer ADDRESS` | Address used for local or remote peer connections |
| `--output FILE` | Write detailed benchmark statistics as JSON |

For NPH, compute-party PIDs are `0..N-1` and the preprocessing helper PID is `N`. The launcher starts all `N+1` processes.

## Results

By default, runs are stored under:

```text
Results/<benchmark>/protocol_<protocol>/parties_<n>/<workload>/<timestamp>/
├── meta.txt
├── party_0.log
├── ...
└── summary.txt
```

For example, PageRank with explicit graph dimensions produces:

```text
Results/bench_PrMpaGraphiti/protocol_nph/parties_2/verts_1000/edges_9000/<timestamp>/
```

Override the base directory with `RESULTS_DIR`:

```bash
RESULTS_DIR=/tmp/graphiti-results ./run.sh bench_PrMpaGraphiti \
  --protocol nph --num-parties 2 --graph-size 10000
```

## Network emulation

`network.sh` defines shell functions for loopback experiments. Source it before invoking a function:

```bash
source network.sh
tc_lan 1ms 1Gbit
```

For an NPH run, independent bandwidth and latency can be assigned to every unordered process pair. The base port must match the benchmark command:

```bash
source network.sh
tc_nph_pairs 2 14900 50ms 100Mbit

./run.sh bench_PrMpaGraphiti \
  --protocol nph --num-parties 2 --port 14900 \
  --graph-size 10000 --num-iters 10
```

