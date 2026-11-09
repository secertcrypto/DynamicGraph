#pragma once
#include "backend.h"
#include <array>
namespace dfss_minimal {
struct TreeKey {
    int depth=0;
    int party=0;
    Block root{};
    std::vector<Block> seed_cw;
    std::vector<std::array<uint8_t,2>> control_cw;
};
struct Key : TreeKey { uint64_t payload_cw=0; };
// Both components are additive shares in Z/(2^64), NOT XOR-shared AES keys.
using Payload2 = std::array<uint64_t,2>;
struct Key2 : TreeKey { Payload2 payload_cw{}; };
// Each process supplies only its own shares. alpha_bits are MSB-first XOR shares.
Key generate(Backend&,const std::vector<uint8_t>& alpha_bits,uint64_t beta_share);
Key generate_arithmetic(Backend&,int depth,uint64_t alpha_share,uint64_t beta_share);
Key2 generate_pair(Backend&,const std::vector<uint8_t>& alpha_bits,Payload2 beta_share);
Key2 generate_pair_arithmetic(Backend&,int depth,uint64_t alpha_share,Payload2 beta_share);
uint64_t evaluate(const Key&,uint64_t public_index);
Payload2 evaluate(const Key2&,uint64_t public_index);
// Public N: return local shares for x=0,...,N-1, in that order.
// N=0 is allowed; N>2^depth throws. No interaction or reconstruction.
std::vector<uint64_t> evaluate_prefix(const Key&,uint64_t N);
std::vector<Payload2> evaluate_prefix(const Key2&,uint64_t N);

// Product payload Z/(2^64) x (Z/(2^128))^512 for (indicator, KH-PRF key).
struct LwrPayload {
    uint64_t indicator=0;
    LwrKhPrf::Key kh_key{};
    bool operator==(const LwrPayload& other) const {
        return indicator==other.indicator && kh_key==other.kh_key;
    }
};
struct LwrDpfKey : TreeKey { LwrPayload payload_cw; };
LwrDpfKey generate_lwr(Backend&,const std::vector<uint8_t>& alpha_bits,
                      const LwrPayload& local_beta);
LwrDpfKey generate_lwr_arithmetic(Backend&,int depth,uint64_t alpha_share,
                                 const LwrPayload& local_beta);
LwrPayload evaluate(const LwrDpfKey&,uint64_t public_index);
std::vector<LwrPayload> evaluate_prefix(const LwrDpfKey&,uint64_t N);
}
