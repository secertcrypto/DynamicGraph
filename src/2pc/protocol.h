#pragma once
#include "init.h"
#include <array>
#include <emp-tool/emp-tool.h>
#include <emp-ot/iknp.h>

namespace graphiti::twopc::detail {
using Bits=std::vector<std::uint8_t>;
using BitRows=std::vector<Bits>; // bit-major, least significant first
using Block=emp::block;
struct Gate {std::size_t left,right,slot;};
using Layers=std::vector<std::vector<Gate>>;

// Count both directions without changing EMP's vendored NetIO implementation.
class CountedIO : public emp::IOChannel<CountedIO> {
    emp::NetIO& io_;
public:
    std::uint64_t received=0;
    explicit CountedIO(emp::NetIO& io):io_(io){}
    void send_data_internal(const void* data,std::uint64_t n){io_.send_data(data,n);}
    void recv_data_internal(void* data,std::uint64_t n){io_.recv_data(data,n);received+=n;}
    void flush(){io_.flush();}
};

class Protocol {
    int party_;
    CountedIO io_;
    emp::IKNP<CountedIO> ot0_,ot1_;
    emp::PRG rng_;
    void transfer(int sender,const std::vector<Block>& m0,const std::vector<Block>& m1,
                  const Bits& choices,std::vector<Block>& result);
public:
    Protocol(int party,emp::NetIO& io);
    int party() const {return party_;}
    std::uint64_t sent() const {return io_.counter;}
    std::uint64_t received() const {return io_.received;}
    Words Multiply(const Words&,const Words&);
    Words EqualZero(const Words&,unsigned width=64);
    Words OpenWords(const Words&,int target=-1);
    void agree(const Words& public_values);
    Word random_word();
    std::vector<int> random_permutation(std::size_t n);
    Bits And(const Bits&,const Bits&);
    Bits Open(const Bits&);
    BitRows ToBits(const Words&,unsigned width);
    Bits Greater(const BitRows&,const BitRows&);
    Words BitToArithmetic(const Bits&);
    Words NormalizeIndices(const Words&,Word modulus);
    // Apply the owner's private pull permutation, generating fresh additive shares.
    // Other party passes an empty permutation. No switch bits are opened.
    Rows PrivatePermute(Rows rows,const std::vector<int>& permutation,int owner);
    Rows Shuffle(Rows rows,const std::vector<int>& local_permutation,bool inverse=false);
    std::vector<std::size_t> InsecureSort(const Words& keys,unsigned width);
};
std::vector<int> inverse(const std::vector<int>&);
Rows public_permute(const Rows&,const std::vector<std::size_t>&,bool inverse=false);
}
