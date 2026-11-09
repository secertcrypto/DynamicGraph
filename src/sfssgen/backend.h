#pragma once
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <vector>
#include <emp-tool/emp-tool.h>
#include <emp-ot/iknp.h>
#include "lwr_khprf.h"

namespace dfss_minimal {
using Block = emp::block;
inline uint64_t low64(Block b) { uint64_t x; std::memcpy(&x,&b,8); return x; }
inline uint8_t lsb(Block b) { return low64(b)&1; }

// Two-party semi-honest backend. IDs 0 and 1; one local share per process.
// mux chooses a if the reconstructed XOR-shared bit is 0, otherwise b.
class Backend {
    int id_;
    emp::NetIO& io_;
    emp::IKNP<emp::NetIO> forward_, reverse_;
    emp::PRG rng_;
    uint64_t vector_exchanges_ = 0;
    uint64_t read_frames_ = 0;
    Block transfer(int sender, Block a, Block b, bool choice) {
        auto& ot = sender==0 ? forward_ : reverse_;
        Block result=emp::zero_block;
        if(id_==sender) ot.send(&a,&b,1);
        else ot.recv(&result,&choice,1);
        io_.flush();
        return result;
    }
public:
    Backend(int id, emp::NetIO& io):id_(id),io_(io),forward_(&io),reverse_(&io) {
        if(id!=0 && id!=1) throw std::invalid_argument("party must be 0 or 1");
    }
    int party() const {return id_;}
    uint64_t read_frame_exchanges() const { return read_frames_; }
    // All peers know the exact frame size from the agreed session/batch.
    std::vector<uint8_t> exchange_read_frame(const std::vector<uint8_t>& local) {
        std::vector<uint8_t> peer(local.size());
        if (local.empty()) return peer;
        if (id_ == 0) {
            io_.send_data(local.data(),local.size()); io_.flush();
            io_.recv_data(peer.data(),peer.size());
        } else {
            io_.recv_data(peer.data(),peer.size());
            io_.send_data(local.data(),local.size()); io_.flush();
        }
        ++read_frames_;
        return peer;
    }
    Block read_key_ot(int sender, Block zero, Block one, bool choice) {
        return transfer(sender, zero, one, choice);
    }
    Block random_block() {Block b; rng_.random_block(&b,1); return b;}
    uint64_t random_word() {return low64(random_block());}
    Block mux_block(uint8_t c, Block a, Block b) {
        if(c>1) throw std::invalid_argument("non-bit share");
        Block mask=random_block();
        Block m0=(c?b:a)^mask, m1=(c?a:b)^mask;
        Block received=emp::zero_block;
        for(int sender=0;sender<2;++sender) {
            Block r=transfer(sender,m0,m1,c!=0);
            if(sender!=id_) received=r;
        }
        return mask^received;
    }
    uint64_t mux_word(uint8_t c, uint64_t a, uint64_t b) {
        if(c>1) throw std::invalid_argument("non-bit share");
        uint64_t mask=random_word();
        Block m0=emp::makeBlock(0,(c?b:a)-mask);
        Block m1=emp::makeBlock(0,(c?a:b)-mask);
        uint64_t received=0;
        for(int sender=0;sender<2;++sender) {
            Block r=transfer(sender,m0,m1,c!=0);
            if(sender!=id_) received=low64(r);
        }
        return mask+received;
    }
    Word128 mux_word128(uint8_t c,Word128 a,Word128 b) {
        if(c>1) throw std::invalid_argument("non-bit share");
        const Word128 mask=block_word128(random_block());
        const Block m0=word128_block((c?b:a)-mask);
        const Block m1=word128_block((c?a:b)-mask);
        Word128 received=0;
        for(int sender=0;sender<2;++sender) {
            Block r=transfer(sender,m0,m1,c!=0);
            if(sender!=id_) received=block_word128(r);
        }
        return mask+received;
    }
    Word128 open_sum128(Word128 x) {return x+exchange(x);}
    template<class T> T exchange(T local) {
        T other;
        if(id_==0) {io_.send_data(&local,sizeof(T));io_.flush();io_.recv_data(&other,sizeof(T));}
        else {io_.recv_data(&other,sizeof(T));io_.send_data(&local,sizeof(T));io_.flush();}
        return other;
    }
    Block open_xor(Block x) {return x^exchange(x);}
    uint8_t open_bit(uint8_t x) {return x^exchange(x);}
    // A nonempty batch uses one contiguous send and one receive per party.
    // The ordered vector size is public and must be agreed by both peers.
    uint64_t vector_exchanges() const { return vector_exchanges_; }
    void open_sums(std::vector<uint64_t>& local) {
        if (local.empty()) return;
        std::vector<uint64_t> peer(local.size());
        const size_t bytes = local.size() * sizeof(uint64_t);
        if (id_ == 0) {
            io_.send_data(local.data(), bytes); io_.flush();
            io_.recv_data(peer.data(), bytes);
        } else {
            io_.recv_data(peer.data(), bytes);
            io_.send_data(local.data(), bytes); io_.flush();
        }
        ++vector_exchanges_;
        for (size_t i = 0; i < local.size(); ++i) local[i] += peer[i];
    }
    uint64_t open_sum(uint64_t x) {return x+exchange(x);}
    uint8_t bit_and(uint8_t a,uint8_t b) {
        return lsb(mux_block(a,emp::zero_block,emp::makeBlock(0,b)));
    }
    // Convert x0+x1 mod 2^depth to MSB-first XOR-shared bits.
    // OT-based ripple carry; deliberately simple, not upstream SCI BitDec.
    std::vector<uint8_t> arithmetic_to_bits(uint64_t local,int depth) {
        if(depth<1 || depth>24) throw std::invalid_argument("depth outside [1,24]");
        std::vector<uint8_t> result(depth);
        uint8_t carry=0;
        for(int i=0;i<depth;++i) {
            uint8_t a=id_==0 ? ((local>>i)&1) : 0;
            uint8_t b=id_==1 ? ((local>>i)&1) : 0;
            result[depth-1-i]=a^b^carry;
            if(i+1<depth) carry=carry^bit_and(a^carry,b^carry);
        }
        return result;
    }
};
}
