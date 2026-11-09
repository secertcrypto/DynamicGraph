#include "read.h"
#include "spiral_u64.h"
#include <array>
#include <limits>
#include <stdexcept>
#include <unordered_set>

namespace risa {
namespace {
using Bytes=std::vector<uint8_t>;
using Pair=std::array<emp::AES_KEY,2>;
Word prf(const emp::AES_KEY& key,Word t,Word x) {
    // Injective fixed-width pair encoding. AES PRP truncated to the data ring.
    auto block=emp::makeBlock(t,x);
    emp::AES_ecb_encrypt_blks(&block,1,&key);
    return dfss_minimal::low64(block);
}
Word bit(Word x,unsigned j,unsigned ell) { return (x>>(ell-1-j))&1; }
}
namespace detail {
struct ReadResources {
    Backend& net;
    Word n, mask;
    unsigned ell=0;
    spiral_u64::Client client;
    spiral_u64::PeerParameters peer;
    static Word ring_mask(unsigned bits) {
        if (!bits || bits>64) throw std::invalid_argument("Read requires 1..64 value bits");
        return ~Word{0} >> (64-bits);
    }
    ReadResources(Backend& b,Word size,unsigned bits):net(b),n(size),mask(ring_mask(bits)),client(size) {
        while((Word{1}<<ell)<n) ++ell;
        peer=spiral_u64::PeerParameters::decode(n,net.exchange_read_frame(client.public_parameters()));
    }
};
}
ReadContext::ReadContext(Backend& net,Word size,unsigned bits)
    :resources_(std::make_shared<detail::ReadResources>(net,size,bits)) {}
Word ReadContext::size() const {return resources_->n;}

struct ReadState::Impl {
    std::shared_ptr<detail::ReadResources> ctx;
    Word sigma;
    spiral_u64::Query incoming;
    std::vector<Pair> sender;
    std::vector<emp::AES_KEY> selected;
    bool used=false;
    Word last=0;
};
ReadState::ReadState(std::unique_ptr<Impl> impl):impl_(std::move(impl)) {}
ReadState::~ReadState()=default;
ReadState::ReadState(ReadState&&) noexcept=default;
ReadState& ReadState::operator=(ReadState&&) noexcept=default;
bool ReadState::can_read(Word t) const {return impl_ && (!impl_->used || t>impl_->last);}

ReadState CompileRead(ReadContext& context,Word sigma) {
    auto ctx=context.resources_;
    if(sigma>=ctx->n) throw std::invalid_argument("noncanonical read index share");
    auto st=std::make_unique<ReadState::Impl>();
    st->ctx=ctx;st->sigma=sigma;
    auto query=ctx->client.make_query(sigma);
    st->incoming=spiral_u64::Query::decode(ctx->peer,ctx->net.exchange_read_frame(query.encode()));
    st->sender.resize(ctx->ell);st->selected.resize(ctx->ell);
    for(unsigned j=0;j<ctx->ell;++j) {
        auto zero=ctx->net.random_block(), one=ctx->net.random_block();
        emp::AES_set_encrypt_key(zero,&st->sender[j][0]);
        emp::AES_set_encrypt_key(one,&st->sender[j][1]);
        for(int sender=0;sender<2;++sender) {
            auto received=ctx->net.read_key_ot(sender,zero,one,bit(sigma,j,ctx->ell));
            if(sender!=ctx->net.party())emp::AES_set_encrypt_key(received,&st->selected[j]);
        }
    }
    return ReadState(std::move(st));
}

std::vector<Word> ReadBatch(ReadContext& context,Word t,const std::vector<ReadInput>& batch) {
    if(batch.empty())return {};
    auto& ctx=*context.resources_;
    std::unordered_set<const ReadState*> seen;
    for(const auto& item:batch) {
        if(!item.state.can_read(t))throw std::logic_error("replayed t or moved-from read state");
        if(item.state.impl_->ctx.get()!=&ctx || item.array_share.size()!=ctx.n ||
           !seen.insert(&item.state).second)throw std::invalid_argument("read batch context/size/duplicate mismatch");
    }
    const auto stride=spiral_u64::Response::wire_size();
    if(batch.size()>Bytes{}.max_size()/stride)throw std::length_error("read batch frame too large");
    Bytes outgoing;outgoing.reserve(batch.size()*stride);
    std::vector<Word> pads(batch.size()),output(batch.size()),masked(ctx.n);
    for(size_t i=0;i<batch.size();++i) {
        auto& st=*batch[i].state.impl_;
        pads[i]=ctx.net.random_word() & ctx.mask;
        for(Word x=0;x<ctx.n;++x) {
            Word v=batch[i].array_share[(x+st.sigma)%ctx.n]+pads[i];
            for(unsigned j=0;j<ctx.ell;++j)v+=prf(st.sender[j][bit(x,j,ctx.ell)],t,x);
            masked[x]=v & ctx.mask;
        }
        auto frame=spiral_u64::Server(masked).answer(st.incoming).encode();
        if(frame.size()!=stride)throw std::logic_error("unexpected PIR response size");
        outgoing.insert(outgoing.end(),frame.begin(),frame.end());
    }
    // Never retry a published PRF domain after a partial exchange.
    for(const auto& item:batch){item.state.impl_->last=t;item.state.impl_->used=true;}
    auto peer=ctx.net.exchange_read_frame(outgoing);
    for(size_t i=0;i<batch.size();++i) {
        const auto& st=*batch[i].state.impl_;
        auto response=spiral_u64::Response::decode(ctx.n,peer.data()+i*stride,stride);
        Word y=ctx.client.recover(response,st.sigma)-pads[i];
        for(const auto& key:st.selected)y-=prf(key,t,st.sigma);
        output[i]=y & ctx.mask;
    }
    return output;
}
Word Read(ReadContext& ctx,ReadState& state,Word t,const std::vector<Word>& db) {
    return ReadBatch(ctx,t,{{state,db}}).front();
}
}
