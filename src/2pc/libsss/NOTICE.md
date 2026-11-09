# Upstream provenance and scope of the port

The routing implementation in benes.cpp is adapted from:

- https://github.com/secertcrypto/libSSS
- Commit: b16b0c467f915d297158594f68ecad25f371acf6
- Files: osn/benes.cpp and osn/benes.h
- The upstream README credits https://github.com/dujiajun/PSU.

Retained: the Beneš route construction (DFS and recursive switch assignment).
Changed: namespace, validated power-of-two sizes, explicit signed path bytes,
and standard-library-only types. A routing call receives a private pull
permutation. Real lengths are padded to a power of two, with padding positions
fixed by the private permutation.

The upstream OSNSender/OSNReceiver executables are NOT compiled here. Their
sample path generates test inputs internally, has fixed PRNG seeds in several
places, and depends on older libOTe/cryptoTools versions. Instead protocol.cpp
evaluates the routed switches directly with the project's EMP IKNP OT and
fresh additive masks in Z_(2^64). This is an adaptation of the routing component
and a new shared-input evaluator, not a byte-for-byte port or a performance
reproduction of upstream libSSS (including its Silent OT backend).

The examined upstream commit has no top-level license file. This notice records
provenance and does not grant additional redistribution rights.
