#pragma once

#include "update.h"

namespace risa::detail {
// MSB-first XOR shares of (alpha_0+alpha_1) mod N. Exposed only for
// independent arithmetic tests; production callers use Compile.
std::vector<std::uint8_t> index_bits_mod_n(Backend&, std::uint64_t size,
                                        Word local_share);
int index_depth(std::uint64_t size);
}
