#pragma once
#include <array>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <openssl/rand.h>
namespace spiral_u64_backend {
class SecureRandom {
    std::array<uint64_t,512> buffer_{};
    size_t offset_=buffer_.size();
public:
    using result_type=uint64_t;
    static constexpr result_type min() {return 0;}
    static constexpr result_type max() {return UINT64_MAX;}
    result_type operator()() {
        if(offset_==buffer_.size()) {
            if(RAND_bytes(reinterpret_cast<unsigned char*>(buffer_.data()),sizeof(buffer_))!=1)
                throw std::runtime_error("RAND_bytes failed");
            offset_=0;
        }
        return buffer_[offset_++];
    }
};
inline uint64_t uniform_mod(uint64_t q) {
    thread_local SecureRandom rng;
    const uint64_t threshold=(uint64_t{0}-q)%q;
    uint64_t v;do {v=rng();}while(v<threshold);return v%q;
}
}
