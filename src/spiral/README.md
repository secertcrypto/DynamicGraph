# Spiral uint64 PIR for CrIS-MPC

Source: secertcrypto/dynamic-secure-graph at bf89198, src/spiral. Upstream
versions, authors and licenses are in NOTICE, AUTHORS and LICENSE. See
[the RISA read module](../risa/READ.md) for build, APIs and tests.

Correctness tests and their CMake target live in [test/spiral](../../test/spiral/).
Standalone builds with `cmake -S src/spiral -B build-spiral` use the same tests;
the test executable is generated under `build-spiral/test/spiral/`.

The port adds a little-endian, fixed-shape wire format. Client::public_parameters
contains W/V only. PeerParameters::decode caches these matrices. Query::encode
sends only encrypted Regev selectors; Query::decode binds them to already
received peer parameters. Response frames contain only the switched ciphertext.
All frames carry magic/version, message type and the public database size. Lengths
and coefficient ranges are checked against the locally derived shape; secret-key
material and block offsets have no wire representation. These checks do not
authenticate peers. The original same-process Client/Server convenience API is
preserved. Source contains actual Spiral encryption and noise; see the original
parameter set in include/values.h.

The bundled HEXL v1.2.1 and cpu_features v0.6.0 are local sources, not system
installations. Use Release on x86-64 Linux/WSL. Backend operations still use
the upstream mutex and none of these tests estimates a cryptographic failure rate.
