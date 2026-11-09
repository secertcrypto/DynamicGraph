# Primitive benchmarks

Arithmetic, permutation and sorting drivers live in this directory. The
standalone RISA driver below measures the real `RISA::read` and `RISA::update`
implementations without Graphiti, NPH or the graph arithmetic evaluator.

## RISA setup, insertion compilation, update and read

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCRIS_BUILD_RISA_READ=ON
cmake --build build --target bench_risa -j4

# Fixed database; compile eight address states once, then update/read five times.
python3 benchmark/primitives/run_risa.py \
  --database-size 256 --batch-size 8 --rounds 5 --repeat 3 --check

# Grow the batch of compiled states between rounds: active counts 4,6,6,7.
python3 benchmark/primitives/run_risa.py \
  --database-size 256 --insert-schedule 4,2,0,1 \
  --update-batch 6 --read-batch 3 --repeat 3 --check
```

The launcher starts two parties once per trial and retains the connection, OT
contexts, compiled states and database across rounds. The database has a fixed
number of uint64 entries. An "insertion" compiles a new handle for a secret
address; it does not enlarge the database or itself write a value. Each handle
has independent Read and Update state for that address. Repeated addresses are
allowed. Input addresses and values are additively shared before worker launch;
only each party's own shares are supplied to it.

Each round compiles new handles, executes `UpdateBatchGather`, locally adds its
output shares to the database, and executes `ReadBatch` on that updated database.
Update is additive rather than assignment. The default is to update and read
all active handles. Optional batch limits select the first `min(limit,active)`
handles in insertion order; they limit execution, not compilation. Messages
change each round, and invocation counters increase monotonically.

| Phase | Measured work |
| --- | --- |
| `connect` | EMP connection and RISA Backend construction |
| `setup_update` | `risa::Setup(N)`; local parameter validation, normally zero bytes |
| `setup_read` | Spiral client/public material setup and parameter exchange |
| `compile_read` | `CompileRead` for every new handle: queries and masking-key OT |
| `compile_update` | `Compile` for every new handle: index processing, distributed DPF generation and local expansion |
| `update` | Build batch and execute `UpdateBatchGather` over the chosen active states |
| `apply_update` | Add the approximate update vector to local database shares; zero bytes |
| `read` | Build batch and execute `ReadBatch`: masked database preparation, PIR responses, recovery and mask removal |

Read compilation precedes Update compilation in each round. Both use the same
RISA Backend, so first-use base OT costs are charged to the phase where setup
actually occurs (normally the first nonempty `compile_read`, or Update for
single-entry databases). There is no unreported preprocessing phase. Empty
compilation/execution batches still have rows, with zero protocol communication.

Options:

| Option | Meaning |
| --- | --- |
| `--database-size N` / `--db-size N` | Fixed database length in uint64 words; default 64; range 1..2^22 |
| `--batch-size B` | Initial handles compiled before round 0; default 4, then no further insertions |
| `--insert-per-round B` | Compile B additional handles each round |
| `--insert-schedule B0,B1,...` | Explicit insertion counts; mutually exclusive with the other insertion options |
| `--rounds K` | Default 3, or explicit schedule length; must match when both are supplied |
| `--update-batch B` | Per-round update limit; default all active handles; zero disables updates |
| `--read-batch B` | Per-round read limit; default all active handles; zero disables reads |
| `--repeat R`, `--warmup W` | Measured trials/excluded warmups, default 3/0; each trial starts fresh |
| `--seed S`, `--threads T` | Fixture seed and worker OpenMP thread count; default 42/1 |
| `--port P`, `--timeout SEC` | Local two-party port and timeout per trial; default 23100/300 |
| `--check` | Validate every round outside timed regions |
| `--binary FILE`, `--out DIR` | Worker path and a new output directory |

The LWR Update state stores roughly 8 KiB per database entry per active handle
per party. Runtime and memory therefore grow with both database size and active
batch size, even though the online update messages are small.

## Timing, communication and correctness

Every phase reports maximum party elapsed time, P0 sent bytes, P1 sent bytes and
their sum. Only application payload sent through EMP is counted, including PIR
ciphertexts, OT traffic and protocol metadata. Receive bytes are not added a
second time. TCP/IP headers, ACKs, retransmissions, launcher pipes, fixture
generation/loading and correctness-check file I/O are excluded. Connection time
is measured even though socket handshakes are not application payload bytes.

For a chosen update batch of U handles, `update` sends exactly `8*U` bytes per
party, `16*U` in total. For a read batch of R handles, `read` sends
`R*Response::wire_size()` per party. The launcher validates these counts on every
trial. These are execution costs; setup and compilation are separate rows.

Results default to `Results/risa/<timestamp>/`:

- `metadata.json`: dimensions, schedule, batch limits, seed, semantics and check status.
- `samples.csv`: all measured per-trial phases; warmups excluded.
- `parties.jsonl`: raw timing and send counters for each party and phase.
- `summary.csv`: phase-by-phase medians, followed by a total row.
- `phase_totals.csv`: median cumulative cost of each phase type across rounds.
- `trial-*/p*.log`: worker diagnostic output.

Totals are computed by summing phase costs within each trial and then taking
the median; they are not obtained by adding phase medians. Timing includes local
computation as well as communication. Measurements use local loopback.

Raw RISA Update returns an approximate delta: each coordinate has modular error
at most U per batch. This benchmark intentionally does not include the dynamic
engine's encoding, secure truncation or decoding. `--check` reconstructs only the
public benchmark fixture's output shares in the launcher, checks every delta's
error bound, checks accumulation into the database, and checks each read exactly
against the actual approximate database. It does not claim exact recovery of an
ideal noiseless update. Errors can accumulate over rounds.

Regression tests cover non-power-of-two and single-entry databases, a database
spanning multiple PIR blocks, colliding indices, multiple insertion batches,
state reuse, empty updates/reads and per-phase communication accounting:

```bash
ctest --test-dir build -R '^risa_primitive_benchmark$' --output-on-failure
```
