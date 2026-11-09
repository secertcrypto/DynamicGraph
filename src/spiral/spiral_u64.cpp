#include "spiral_u64.h"
#include "client.h"
#include <memory>
#include <mutex>
#include <stdexcept>

namespace spiral_u64_backend {
bool nonoise=false,direct_upload=true,ternary=false,random_data=false,show_diff=false,output_err=false;
size_t total_offline_query_size=0;
void scalToMat(size_t,MatPoly&,const MatPoly&,const MatPoly&);
void regevToGSW(size_t,size_t,MatPoly&,const std::vector<MatPoly>&,size_t,const MatPoly&,const MatPoly&);
}
namespace spiral_u64 {
namespace b=spiral_u64_backend;
namespace {
std::mutex backend_mutex;
constexpr uint64_t words_per_block=b::n0*b::n2*b::poly_len/8; // 1024 words, 8 bytes each
struct Shape {uint64_t count,blocks,dim0,dim1;unsigned bits1;};
Shape shape(uint64_t count) {
    if(count==0 || count>4096*words_per_block)
        throw std::invalid_argument("database size must be in [1,4194304] uint64 words");
    uint64_t blocks=1;unsigned bits=0;
    const uint64_t needed=(count+words_per_block-1)/words_per_block;
    while(blocks<needed){blocks*=2;++bits;}
    const unsigned bits0=(bits+1)/2,bits1=bits-bits0;
    return {count,blocks,uint64_t{1}<<bits0,uint64_t{1}<<bits1,bits1};
}
void initialize() {
    static std::once_flag once;
    std::call_once(once,[]{
        b::build_table();
        static std::vector<uint64_t> scratch_storage(b::crt_count*b::poly_len);
        b::scratch=scratch_storage.data();
        static intel::hexl::NTT ntt(b::poly_len,b::arb_qprime);b::ntt_qprime=&ntt;
    });
}
struct SecretBinding {
    explicit SecretBinding(const b::MatPoly& sr){b::sr_mp=sr;}
    ~SecretBinding(){b::sr_mp=b::MatPoly();}
};
b::MatPoly external_product(const b::MatPoly& gsw,const b::MatPoly& ct) {
    auto raw=b::from_ntt(ct);
    b::MatPoly digits(b::m2,b::n2,false);
    b::gadget_invert(b::m2,digits,raw,b::n1);
    return b::multiply(gsw,b::to_ntt(digits));
}
}
struct PublicMatrices { b::MatPoly W,V; };
struct PeerParameters::Impl {
    Shape s;
    std::shared_ptr<PublicMatrices> matrices = std::make_shared<PublicMatrices>();
    explicit Impl(Shape shape):s(shape){}
};
struct Client::Impl {
    Shape s;b::MatPoly secret,sp,sr;std::shared_ptr<PublicMatrices> matrices=std::make_shared<PublicMatrices>();
    explicit Impl(Shape value):s(value),secret(b::n0,b::n1,false),sp(b::n0,b::k_param,false),sr(1,1,false) {}
};
struct Server::Impl {Shape s;std::vector<b::MatPoly> blocks;explicit Impl(Shape value):s(value) {}};
struct Query::Impl {Shape s;std::shared_ptr<const PublicMatrices> matrices;std::vector<b::MatPoly> regev;explicit Impl(Shape value):s(value) {}};
struct Response::Impl {uint64_t count;b::MatPoly switched;};
Client::Client(uint64_t count):impl_(std::make_shared<Impl>(shape(count))) {
    std::lock_guard<std::mutex> lock(backend_mutex);initialize();
    auto& p=*impl_;b::keygen(p.secret,p.sp,p.sr);
    auto gadget=b::buildGadget(b::n0,b::n0*b::m_conv);
    auto sr_gadget=b::mul_by_const(b::to_ntt(p.sr),b::to_ntt(gadget));
    b::MatPoly padded(b::n1,b::n0*b::m_conv);b::place(padded,sr_gadget,1,0);
    p.matrices->W=b::add(b::to_ntt(b::get_fresh_public_key_raw(p.sp,b::n0*b::m_conv)),padded);
    auto gv=b::to_ntt(b::buildGadget(1,b::m_conv));
    auto scaled=b::mul_by_const(b::to_ntt(p.sr),gv);
    b::MatPoly together(1,2*b::m_conv);b::place(together,scaled,0,0);b::place(together,gv,0,b::m_conv);
    auto result=b::multiply(b::to_ntt(p.sp),together);
    b::MatPoly result_padded(b::n1,2*b::m_conv);b::place(result_padded,result,1,0);
    p.matrices->V=b::add(b::to_ntt(b::get_fresh_public_key_raw(p.sp,2*b::m_conv)),result_padded);
}
Query Client::make_query(uint64_t index) const {
    if(index>=impl_->s.count)throw std::out_of_range("PIR index out of range");
    std::lock_guard<std::mutex> lock(backend_mutex);initialize();
    const auto& p=*impl_;SecretBinding binding(p.sr);
    auto q=std::make_shared<Query::Impl>(p.s);q->matrices=p.matrices;
    const uint64_t block=index/words_per_block,row=block/p.s.dim1,column=block%p.s.dim1;
    for(uint64_t i=0;i<p.s.dim0;++i)
        q->regev.push_back(b::encryptSimpleRegev(b::single_poly(i==row?b::scale_k:0)));
    const size_t bits_per=b::get_bits_per(b::t_GSW);
    for(unsigned i=0;i<p.s.bits1;++i)
        for(size_t j=0;j<b::t_GSW;++j)
            q->regev.push_back(b::encryptSimpleRegev(b::single_poly(((column>>i)&1)?uint64_t{1}<<(j*bits_per):0)));
    Query out;out.impl_=std::move(q);return out;
}
Server::Server(const std::vector<uint64_t>& database):impl_(std::make_shared<Impl>(shape(database.size()))) {
    std::lock_guard<std::mutex> lock(backend_mutex);initialize();
    auto& p=*impl_;p.blocks.reserve(p.s.blocks);
    for(uint64_t block=0;block<p.s.blocks;++block) {
        b::MatPoly raw(b::n0,b::n2,false);
        for(uint64_t slot=0;slot<words_per_block;++slot) {
            const uint64_t index=block*words_per_block+slot;
            const uint64_t word=index<database.size()?database[index]:0;
            for(unsigned j=0;j<8;++j) {
                const uint64_t digit=(word>>(j*8))&255;
                raw.data[slot*8+j]=digit>=128?b::Q_i-(256-digit):digit;
            }
        }
        p.blocks.push_back(b::to_ntt(raw));
    }
}
uint64_t Server::size() const {return impl_->s.count;}
Response Server::answer(const Query& query) const {
    if(!query.impl_ || query.impl_->s.count!=impl_->s.count)
        throw std::invalid_argument("query/database shape mismatch");
    std::lock_guard<std::mutex> lock(backend_mutex);initialize();
    const auto& p=*impl_;const auto& q=*query.impl_;
    std::vector<b::MatPoly> partial;
    for(uint64_t j=0;j<p.s.dim1;++j)partial.emplace_back(b::n1,b::n2);
    for(uint64_t i=0;i<p.s.dim0;++i) {
        b::MatPoly composed(b::n1,b::n0);b::scalToMat(b::m_conv,composed,q.regev[i],q.matrices->W);
        for(uint64_t j=0;j<p.s.dim1;++j) {
            auto product=b::multiply(composed,p.blocks[i*p.s.dim1+j]);
            b::add(partial[j],partial[j],product);
        }
    }
    auto gadget=b::to_ntt(b::buildGadget(b::n1,b::m2));
    // Pair adjacent columns, consuming the least significant selector bit first.
    for(unsigned bit=0;bit<p.s.bits1;++bit) {
        b::MatPoly gsw(b::n1,b::m2);
        b::regevToGSW(b::m_conv,b::t_GSW,gsw,q.regev,p.s.dim0+bit*b::t_GSW,q.matrices->W,q.matrices->V);
        b::MatPoly inverse(b::n1,b::m2);
        for(size_t j=0;j<b::n1*b::m2*b::crt_count*b::poly_len;++j) {
            const uint64_t modulus=b::moduli[(j/b::poly_len)%b::crt_count];
            inverse.data[j]=(gadget.data[j]+modulus-gsw.data[j])%modulus;
        }
        std::vector<b::MatPoly> next;next.reserve(partial.size()/2);
        for(size_t j=0;j<partial.size();j+=2)
            next.push_back(b::add(external_product(inverse,partial[j]),external_product(gsw,partial[j+1])));
        partial=std::move(next);
    }
    auto raw=b::from_ntt(partial[0]);
    auto first=b::getRescaled(b::pick(raw,0,0,1,b::n2),b::Q_i,b::arb_qprime);
    auto rest=b::getRescaled(b::pick(raw,1,0,b::n0,b::n2),b::Q_i,4*b::p_db);
    auto r=std::make_shared<Response::Impl>();r->count=p.s.count;r->switched=b::MatPoly(b::n1,b::n2,false);
    b::place(r->switched,first,0,0);b::place(r->switched,rest,1,0);
    Response response;response.impl_=std::move(r);return response;
}
uint64_t Client::recover(const Response& response,uint64_t index) const {
    if(index>=impl_->s.count)throw std::out_of_range("PIR index out of range");
    if(!response.impl_ || response.impl_->count!=impl_->s.count)
        throw std::invalid_argument("response/client shape mismatch");
    std::lock_guard<std::mutex> lock(backend_mutex);initialize();
    auto sp=impl_->sp;b::to_ntt_qprime(sp);
    auto first=b::pick(response.impl_->switched,0,0,1,b::n2);b::to_ntt_qprime(first);
    auto decoded=b::mul_over_qprime(sp,first);b::from_ntt_qprime(decoded);
    auto rest=b::pick(response.impl_->switched,1,0,b::n0,b::n2);
    constexpr int64_t q1=4*b::p_db,denom=b::arb_qprime*4;
    uint64_t word=0;
    for(unsigned j=0;j<8;++j) {
        const size_t at=(index%words_per_block)*8+j;
        int64_t a=decoded.data[at],v=rest.data[at];
        if(a>=static_cast<int64_t>(b::arb_qprime/2))a-=b::arb_qprime;
        if(v>=q1/2)v-=q1;
        const __int128 value=__int128{a}*q1+__int128{v}*b::arb_qprime;
        __int128 digit=(value+(value>=0?denom/2:-denom/2))/denom;
        digit=(digit%256+256)%256;
        word|=static_cast<uint64_t>(digit)<<(j*8);
    }
    return word;
}
uint64_t read(const Client& c,const Server& s,uint64_t index) {
    return c.recover(s.answer(c.make_query(index)),index);
}
}

// Fixed, little-endian v1 frames. Dimensions come from local parameters, never
// from serialized matrix metadata. Only ciphertexts and W/V are serializable.
namespace spiral_u64 {
namespace {
constexpr uint64_t magic=0x3152495053495243ULL;
void put(std::vector<uint8_t>& v,uint64_t x) {
    for(unsigned i=0;i<8;++i) v.push_back(static_cast<uint8_t>(x>>(8*i)));
}
std::vector<uint8_t> header(uint64_t kind,uint64_t count) {
    std::vector<uint8_t> out;put(out,magic);put(out,kind);put(out,count);return out;
}
struct Reader {
    const uint8_t* ptr;
    uint64_t word() {
        uint64_t v=0;for(unsigned i=0;i<8;++i)v|=uint64_t{*ptr++}<<(8*i);
        return v;
    }
    Reader(const uint8_t* data,size_t len,size_t payload_words,uint64_t kind,uint64_t count):ptr(data) {
        if(!data || len!=(3+payload_words)*8 || word()!=magic || word()!=kind || word()!=count)
            throw std::invalid_argument("bad Spiral v1 frame type, shape or size");
    }
};
size_t words(size_t rows,size_t cols,bool ntt) {
    return rows*cols*b::poly_len*(ntt?b::crt_count:1);
}
uint64_t modulus(size_t i,bool ntt) {
    return ntt ? b::moduli[(i/b::poly_len)%b::crt_count] :
           (i<b::n2*b::poly_len ? b::arb_qprime : 4*b::p_db);
}
void pack(std::vector<uint8_t>& out,const b::MatPoly& m) {
    for(size_t i=0;i<words(m.rows,m.cols,m.isNTT);++i)put(out,m.data[i]%modulus(i,m.isNTT));
}
b::MatPoly unpack(Reader& in,size_t rows,size_t cols,bool ntt) {
    b::MatPoly m(rows,cols,ntt);
    for(size_t i=0;i<words(rows,cols,ntt);++i) {
        auto w=in.word();if(w>=modulus(i,ntt))throw std::invalid_argument("invalid polynomial coefficient");
        m.data[i]=w;
    }
    return m;
}
}
std::vector<uint8_t> Client::public_parameters() const {
    auto out=header(1,impl_->s.count);
    pack(out,impl_->matrices->W);pack(out,impl_->matrices->V);return out;
}
PeerParameters PeerParameters::decode(uint64_t count,const std::vector<uint8_t>& bytes) {
    auto p=std::make_shared<Impl>(shape(count));
    Reader in(bytes.data(),bytes.size(),words(b::n1,(b::n0+2)*b::m_conv,true),1,count);
    p->matrices->W=unpack(in,b::n1,b::n0*b::m_conv,true);
    p->matrices->V=unpack(in,b::n1,2*b::m_conv,true);
    PeerParameters out;out.impl_=std::move(p);return out;
}
std::vector<uint8_t> Query::encode() const {
    if(!impl_)throw std::logic_error("empty query");
    auto out=header(2,impl_->s.count);
    for(const auto& ct:impl_->regev)pack(out,ct);
    return out;
}
Query Query::decode(const PeerParameters& pp,const std::vector<uint8_t>& bytes) {
    if(!pp.impl_)throw std::invalid_argument("empty peer parameters");
    auto q=std::make_shared<Impl>(pp.impl_->s);
    size_t count=q->s.dim0+q->s.bits1*b::t_GSW;
    Reader in(bytes.data(),bytes.size(),count*words(b::n0,1,true),2,q->s.count);
    q->matrices=pp.impl_->matrices;
    for(size_t i=0;i<count;++i)q->regev.push_back(unpack(in,b::n0,1,true));
    Query out;out.impl_=std::move(q);return out;
}
size_t Response::wire_size() {return (3+words(b::n1,b::n2,false))*8;}
std::vector<uint8_t> Response::encode() const {
    if(!impl_)throw std::logic_error("empty response");
    auto out=header(3,impl_->count);out.reserve(wire_size());
    pack(out,impl_->switched);return out;
}
Response Response::decode(uint64_t count,const uint8_t* bytes,size_t len) {
    shape(count);
    Reader in(bytes,len,words(b::n1,b::n2,false),3,count);
    auto r=std::make_shared<Impl>();r->count=count;
    r->switched=unpack(in,b::n1,b::n2,false);
    Response out;out.impl_=std::move(r);return out;
}
}
