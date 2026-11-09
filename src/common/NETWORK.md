# Network transfer lengths

Net3P and NetNP byte lengths and ring element counts, the two-party CountedIO
adapter, and the local EMP IOChannel/NetIO use uint64_t for transport lengths.
Byte APIs count bytes; ring APIs count elements and check multiplication by
sizeof(T) before sending. Host container indices, std::align's space argument,
and fread/fwrite return values remain size_t. Conversion to a host memory size
is checked before IO.

NPH optimized shuffle preprocessing transmits permutation entries as uint64_t,
then checks that received indices fit the local permutation before storing them
as host indices. Other arithmetic payloads retain their declared ring type.

The local EMP point codec now encodes its length as an 8-byte little-endian
uint64_t rather than the previous four bytes copied from a size_t. Both peers
must use this version. Rebuild all parties together. Point payloads and existing
Spiral frames are otherwise unchanged. Base-OT communication increases by four
bytes per serialized point; this change does not compress PIR ciphertexts.

network_u64 tests lengths beyond 2^32 without allocating a large buffer, count
multiplication overflow, boolean/block round trips and the point wire header.
The Graphiti, dynamic graph and RISA suites exercise the actual network paths.
