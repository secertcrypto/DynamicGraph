// Adapted from xingpz2008/dealerless-FSS_public, e8fefb537253ee5b0317ea484c934656f09a66f5.
// Copyright (c) 2024 Pengzhi Xing. Apache-2.0; see LICENSE and NOTICE.
// Ordinary full-GGM DPF only. EMP replaces cryptoTools; backend replaces SCI.
#include "dpf.h"
#include <utility>
namespace dfss_minimal {
namespace {
void expand(Block seed,Block* children) {
    emp::AES_KEY key;
    emp::AES_set_encrypt_key(seed,&key);
    children[0]=emp::makeBlock(0,0);children[1]=emp::makeBlock(0,1);
    emp::AES_ecb_encrypt_blks(children,2,&key);
}
void validate_depth(int d) {
    if(d<1 || d>24) throw std::invalid_argument("full-tree depth must be in [1,24]");
}

void validate_key(const TreeKey& key) {
    validate_depth(key.depth);
    if((key.party!=0 && key.party!=1) ||
       key.seed_cw.size()!=static_cast<size_t>(key.depth) ||
       key.control_cw.size()!=static_cast<size_t>(key.depth))
        throw std::invalid_argument("invalid key shape");
    for(auto cw:key.control_cw) if(cw[0]>1 || cw[1]>1)
        throw std::invalid_argument("non-bit control correction");
}
// Distinct PRF inputs for vector components. Never reuse the same mask twice.
Payload2 pair_masks(Block seed) {
    emp::AES_KEY aes;
    emp::AES_set_encrypt_key(seed,&aes);
    Block out[2]={emp::makeBlock(0x4450465041594c44ULL,0),
                  emp::makeBlock(0x4450465041594c44ULL,1)};
    emp::AES_ecb_encrypt_blks(out,2,&aes);
    return {low64(out[0]),low64(out[1])};
}
struct TreeMaterial {
    TreeKey key;
    std::vector<Block> nodes;
    std::vector<uint8_t> controls;
};
TreeMaterial generate_tree(Backend& backend,const std::vector<uint8_t>& alpha_bits) {
    const int depth=static_cast<int>(alpha_bits.size()),party=backend.party();
    validate_depth(depth);
    for(auto b:alpha_bits) if(b>1) throw std::invalid_argument("non-bit index share");
    TreeKey key;key.depth=depth;key.party=party;key.root=backend.random_block();
    std::vector<Block> nodes(1,key.root);
    std::vector<uint8_t> controls(1,static_cast<uint8_t>(party));
    for(int level=0;level<depth;++level) {
        std::vector<Block> children(nodes.size()*2);
        std::vector<uint8_t> next_controls(children.size());
        Block left=emp::zero_block,right=emp::zero_block;
        for(size_t j=0;j<nodes.size();++j) {
            expand(nodes[j],children.data()+2*j);
            left=left^children[2*j];right=right^children[2*j+1];
        }
        uint8_t bit=alpha_bits[level];
        Block cw=backend.mux_block(bit^party,left,right);
        uint8_t tau0=lsb(left)^bit^party,tau1=lsb(right)^bit;
        cw=backend.open_xor(cw);
        tau0=backend.open_bit(tau0);tau1=backend.open_bit(tau1);
        key.seed_cw.push_back(cw);key.control_cw.push_back({tau0,tau1});
        for(size_t j=0;j<nodes.size();++j) {
            for(int branch=0;branch<2;++branch) {
                auto pos=2*j+branch;
                next_controls[pos]=lsb(children[pos]);
                if(controls[j]) {
                    children[pos]=children[pos]^cw;
                    next_controls[pos]^=branch?tau1:tau0;
                }
            }
        }
        nodes.swap(children);controls.swap(next_controls);
    }

    return {std::move(key),std::move(nodes),std::move(controls)};
}
template<class K,size_t M>
K generate_impl(Backend& backend,const std::vector<uint8_t>& alpha_bits,
                const std::array<uint64_t,M>& beta) {
    auto material=generate_tree(backend,alpha_bits);
    K key;static_cast<TreeKey&>(key)=std::move(material.key);
    const auto& nodes=material.nodes;const auto& controls=material.controls;
    const int party=backend.party();
    std::array<uint64_t,M> sums{},correction{};
    uint64_t control_sum=0;
    for(size_t j=0;j<nodes.size();++j) {
        if constexpr(M==1) sums[0]+=low64(nodes[j]);
        else {
            const auto masks=pair_masks(nodes[j]);
            for(size_t c=0;c<M;++c) sums[c]+=masks[c];
        }
        control_sum+=controls[j];
    }
    const uint64_t signed_control=party==0?control_sum:uint64_t{0}-control_sum;
    const uint8_t choice=(signed_control>>1)&1;
    for(size_t c=0;c<M;++c) {
        const uint64_t first=beta[c]+(party==0?uint64_t{0}-sums[c]:sums[c]);
        correction[c]=backend.open_sum(backend.mux_word(choice,first,uint64_t{0}-first));
    }
    if constexpr(M==1) key.payload_cw=correction[0];
    else key.payload_cw=correction;
    return key;
}
LwrPayload lwr_masks(Block seed) {
    emp::AES_KEY aes;emp::AES_set_encrypt_key(seed,&aes);
    Block blocks[LwrKhPrf::dimension+1];
    for(size_t j=0;j<LwrKhPrf::dimension+1;++j)
        blocks[j]=emp::makeBlock(0x4c57525041594c44ULL,j);
    emp::AES_ecb_encrypt_blks(blocks,LwrKhPrf::dimension+1,&aes);
    LwrPayload out;out.indicator=low64(blocks[0]);
    for(size_t j=0;j<LwrKhPrf::dimension;++j) out.kh_key[j]=block_word128(blocks[j+1]);
    return out;
}
LwrDpfKey generate_lwr_impl(Backend& backend,const std::vector<uint8_t>& alpha_bits,
                           const LwrPayload& beta) {
    auto material=generate_tree(backend,alpha_bits);
    LwrDpfKey key;static_cast<TreeKey&>(key)=std::move(material.key);
    LwrPayload sum;uint64_t control_sum=0;
    for(size_t i=0;i<material.nodes.size();++i) {
        auto masks=lwr_masks(material.nodes[i]);sum.indicator+=masks.indicator;
        for(size_t j=0;j<LwrKhPrf::dimension;++j) sum.kh_key[j]+=masks.kh_key[j];
        control_sum+=material.controls[i];
    }
    const int party=backend.party();
    const uint64_t signed_control=party?uint64_t{0}-control_sum:control_sum;
    const uint8_t choice=(signed_control>>1)&1;
    const uint64_t first=beta.indicator+(party?sum.indicator:uint64_t{0}-sum.indicator);
    key.payload_cw.indicator=backend.open_sum(backend.mux_word(choice,first,uint64_t{0}-first));
    for(size_t j=0;j<LwrKhPrf::dimension;++j) {
        const Word128 v=beta.kh_key[j]+(party?sum.kh_key[j]:Word128{0}-sum.kh_key[j]);
        key.payload_cw.kh_key[j]=backend.open_sum128(backend.mux_word128(choice,v,Word128{0}-v));
    }
    return key;
}
struct Node { Block seed; uint8_t control; };
Node child(const TreeKey& key,Block raw,uint8_t parent_control,int level,int branch) {
    Node result{raw,lsb(raw)};
    if(parent_control) {
        result.seed=result.seed^key.seed_cw[level];
        result.control^=key.control_cw[level][branch];
    }
    return result;
}
Node leaf(const TreeKey& key,uint64_t index) {
    validate_key(key);
    if(index >= (uint64_t{1}<<key.depth)) throw std::invalid_argument("index outside domain");
    Node node{key.root,static_cast<uint8_t>(key.party)};
    for(int level=0;level<key.depth;++level) {
        Block children[2];expand(node.seed,children);
        const int branch=(index>>(key.depth-1-level))&1;
        node=child(key,children[branch],node.control,level,branch);
    }
    return node;
}
uint64_t finish(const Key& key,Node node) {
    const uint64_t value=low64(node.seed)+key.payload_cw*node.control;
    return key.party==0?value:uint64_t{0}-value;
}
Payload2 finish(const Key2& key,Node node) {
    auto value=pair_masks(node.seed);
    for(size_t c=0;c<2;++c) {
        value[c]+=key.payload_cw[c]*node.control;
        if(key.party) value[c]=uint64_t{0}-value[c];
    }
    return value;
}
LwrPayload finish(const LwrDpfKey& key,Node node) {
    auto value=lwr_masks(node.seed);
    value.indicator+=key.payload_cw.indicator*node.control;
    if(key.party) value.indicator=uint64_t{0}-value.indicator;
    for(size_t j=0;j<LwrKhPrf::dimension;++j) {
        value.kh_key[j]+=key.payload_cw.kh_key[j]*node.control;
        if(key.party) value.kh_key[j]=Word128{0}-value.kh_key[j];
    }
    return value;
}
// DFS visits the prefix's shared ancestors once and prunes subtrees >= N.
// O(N+depth) work, O(depth) traversal storage, plus O(N) returned values.
template<class K,class V>
void visit_prefix(const K& key,Node node,int level,uint64_t first,uint64_t N,
                  std::vector<V>& output) {
    if(first>=N) return;
    if(level==key.depth) { output.push_back(finish(key,node));return; }
    Block children[2];expand(node.seed,children);
    visit_prefix(key,child(key,children[0],node.control,level,0),level+1,first,N,output);
    const uint64_t right_first=first+(uint64_t{1}<<(key.depth-level-1));
    if(right_first<N)
        visit_prefix(key,child(key,children[1],node.control,level,1),level+1,right_first,N,output);
}
template<class V,class K>
std::vector<V> prefix(const K& key,uint64_t N) {
    validate_key(key);
    if(N>(uint64_t{1}<<key.depth)) throw std::invalid_argument("N exceeds DPF domain");
    std::vector<V> output;output.reserve(static_cast<size_t>(N));
    visit_prefix(key,Node{key.root,static_cast<uint8_t>(key.party)},0,0,N,output);
    return output;
}
} // namespace
Key generate(Backend& b,const std::vector<uint8_t>& a,uint64_t beta) {
    return generate_impl<Key,1>(b,a,{beta});
}
Key generate_arithmetic(Backend& b,int d,uint64_t a,uint64_t beta) {
    return generate(b,b.arithmetic_to_bits(a,d),beta);
}
Key2 generate_pair(Backend& b,const std::vector<uint8_t>& a,Payload2 beta) {
    return generate_impl<Key2,2>(b,a,beta);
}
Key2 generate_pair_arithmetic(Backend& b,int d,uint64_t a,Payload2 beta) {
    return generate_pair(b,b.arithmetic_to_bits(a,d),beta);
}
uint64_t evaluate(const Key& key,uint64_t x) {return finish(key,leaf(key,x));}
Payload2 evaluate(const Key2& key,uint64_t x) {return finish(key,leaf(key,x));}
std::vector<uint64_t> evaluate_prefix(const Key& key,uint64_t N) {return prefix<uint64_t>(key,N);}
std::vector<Payload2> evaluate_prefix(const Key2& key,uint64_t N) {return prefix<Payload2>(key,N);}
LwrDpfKey generate_lwr(Backend& b,const std::vector<uint8_t>& a,const LwrPayload& beta) {
    return generate_lwr_impl(b,a,beta);
}
LwrDpfKey generate_lwr_arithmetic(Backend& b,int d,uint64_t a,const LwrPayload& beta) {
    return generate_lwr(b,b.arithmetic_to_bits(a,d),beta);
}
LwrPayload evaluate(const LwrDpfKey& key,uint64_t x) {return finish(key,leaf(key,x));}
std::vector<LwrPayload> evaluate_prefix(const LwrDpfKey& key,uint64_t N) {
    return prefix<LwrPayload>(key,N);
}
} // namespace dfss_minimal
