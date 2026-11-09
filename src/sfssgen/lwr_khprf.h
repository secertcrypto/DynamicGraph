#pragma once
#include <array>
#include <cstdint>
#include <cstring>
#include <emp-tool/emp-tool.h>

namespace dfss_minimal {
// GCC/Clang extension; unsigned overflow implements arithmetic mod 2^128.
using Word128 = unsigned __int128;
inline Word128 block_word128(emp::block b) {
    uint64_t words[2];std::memcpy(words,&b,16);
    return (Word128{words[1]}<<64)|words[0];
}
inline emp::block word128_block(Word128 x) {
    return emp::makeBlock(static_cast<uint64_t>(x>>64),static_cast<uint64_t>(x));
}
// Matches OpenSFSS's CRH expansion and inner-product/high-bit truncation structure.
// 128 bits PER coefficient, NOT a 128-bit total secret key.
// These are functional parameters, not a certified concrete security estimate.
struct LwrKhPrf {
    static constexpr size_t dimension=512;
    using Key=std::array<Word128,dimension>;
    using PublicInput=Key;
    static Key random_key() {
        emp::PRG prg;Key key;
        for(auto& x:key) {emp::block b;prg.random_block(&b,1);x=block_word128(b);}
        return key;
    }
    static PublicInput expand_input(uint64_t counter) {
        emp::block input[dimension],output[dimension];
        for(size_t j=0;j<dimension;++j) input[j]=emp::makeBlock(counter,j);
        emp::CRH hash;hash.Hn(output,input,dimension);
        PublicInput result;
        for(size_t j=0;j<dimension;++j) result[j]=block_word128(output[j]);
        return result;
    }
    static Word128 inner_product(const Key& key,const PublicInput& input) {
        Word128 sum=0;
        for(size_t j=0;j<dimension;++j) sum+=key[j]*input[j];
        return sum;
    }
    static uint64_t eval_expanded(const Key& key,const PublicInput& input) {
        return static_cast<uint64_t>(inner_product(key,input)>>64);
    }
    static uint64_t eval(const Key& key,uint64_t counter) {
        return eval_expanded(key,expand_input(counter));
    }
};
}
