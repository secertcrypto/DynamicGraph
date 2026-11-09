# RISA.Read in DynamicGraph

RISA returns additive uint64 shares. With honest PIR recovery their sum is
exactly D[(sigma_0+sigma_1) mod N] in Z/(2^64). Existing Update, UpdateBatch
and UpdateBatchGather still implement additive contributions with LWR rounding
error; Read does not introduce that rounding error.

## Build and run

Apply this patch **after** the DynamicGraph graph module, local EMP and batch
update patches. It includes the Spiral, HEXL and cpu_features sources and
licenses. No HEXL system installation is required. From the DynamicGraph root:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCRIS_BUILD_RISA_READ=ON
cmake --build build --parallel 2 --target risa_read_example risa_read_test spiral_u64_test risa_graphiti_read_example
ctest --test-dir build -R '^(risa_read_.*|spiral_u64_correctness)$' --output-on-failure
python3 test/risa/run_two_party.py build/test/risa/risa_read_example --port 28466
```

Alternatively, run the same binary in two terminals with arguments `0 28466`
and `1 28466`. RISA alone does not use a helper. The integration example runs
two NPH compute parties plus their preprocessing helper:

```sh
python3 test/run_graphiti_module.py build/test/risa/risa_graphiti_read_example nph 18600
```

Only P0/P1 run RISA on the separate channel at base port + 50. They read
from the same locally shared vertex snapshot as the base path, use identity
ApplyE, gather two delta messages, and stage each party's own contribution
before the NPH runner starts online evaluation. In the circuit the base and
delta aggregates feed ApplyV once per vertex. Reconstruction is confined to
the final public test fixture. This integration doesn't make the runner accept
unplanned inputs after online execution starts. Set CRIS_BUILD_RISA_READ=OFF
to build only the previous write/graph modules.

## Single and batch reads

```cpp
#include "risa/read.h"
auto ctx = risa::SetupRead(backend, N); // both parties; backend must outlive states
auto first  = risa::CompileRead(ctx, source_a_share);
auto second = risa::CompileRead(ctx, source_b_share);
auto single = risa::Read(ctx, first, 0, H_share);
auto both = risa::ReadBatch(ctx, 1, {{first, next_H_share}, {second, next_H_share}});
// both[0] and both[1] are this party's shares, in input order.
```

Each input can instead reference a different local array of length N. Distinct
states may target the same vertex. A public empty batch returns no values and
sends nothing. The public order, t and shapes must match on the two parties.
State is move-only; the implementation rejects duplicate references, stale or
repeated t, moved-from states and mismatched context/array sizes. Strictly
increasing t is required per state, not across unrelated edges. After a partial
network failure, discard the session rather than replaying a used mask.

* Setup: Spiral generates private client keys and public W/V per party. Only W/V
  cross the channel, once per context. Client offsets and secret recovery state
  stay local. Public size is 1..4,194,304 for this instantiation.
* Compile: use the local canonical sigma_b in [0,N) for an encrypted PIR query.
  Exchange only encrypted selectors. For each of ceil(log2 N) index bits use
  independent random 128-bit sender-key pairs and real bidirectional IKNP OT
  to retain the selected peer key. No bit OTs are needed for N=1. The received
  query and keys are reused across rounds. This path does not convert or open
  the reconstructed index.
* Read: rotate the local input share by sigma_b modulo N, add a fresh private
  pad per state/round, and add the per-bit AES PRF values. The PRF input is an
  injective fixed-width pair (uint64 t, uint64 x), and its output is the low
  64 bits of AES. Answer the peer query on this masked array. Recover using
  the local client and offset, then subtract the selected-key masks and the
  local pad. The two private pads cancel only in the sum of output shares.
* ReadBatch computes each state's own masked array and response. It concatenates
  fixed-size ciphertext frames into one exchange per party; no multi-point PIR
  or FHE-side batching is claimed. Current response size is 98,328 bytes per
  state per direction (including its 24-byte header). Setup/OT/query traffic
  is separate. Mask computation costs O(B N ceil(log2 N)) for B states, in
  addition to B actual PIR answers/recovers.

Spiral decrypts a packed block internally. The per-position masks are essential:
ordinary query privacy alone does not protect the other values in that block.
Do not substitute plaintext indices, exchange shared_ptr objects, or drop
these masks. NetIO here is a research transport; the protocol model assumes
private authenticated communication. Correctness testing is not a security
audit or a proof of a negligible decryption-failure rate across query reuse.

## Dynamic graph

`dynamic_graph::DeltaAccess` compiles separate source read and destination
update states on insertion. `read_sources(t,H_share)` obtains all source shares
with one ReadBatch. The caller computes MPC ApplyE; `gather(t,message_shares)`
uses UpdateBatchGather and returns an approximate local contribution. Use the
existing NphDeltaInput bridge at the Gather-to-ApplyV boundary. There is no
opening of vertex data in these library calls.


The optional third argument to SetupRead is the value width (1..64, default
64). With width k the sender masks its array in Z/(2^k) and each recovered
output share is reduced to that same ring. The [dynamic graph engine](../dynamic_graph/ENGINE.md)
uses this option for q, encodes update messages in the wider ring, corrects
rounding before ApplyV, and carries local vertex shares between MPC phases.
