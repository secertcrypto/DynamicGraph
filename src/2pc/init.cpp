#include "init.h"
#include "protocol.h"
#include <stdexcept>

namespace graphiti::twopc {
struct Initializer::Impl {
    detail::Protocol protocol;
    explicit Impl(int party,emp::NetIO& io):protocol(party,io){}
    void validate(const InitState& state,const Rows& rows,Word operation) {
        const auto width=rows.empty()?0:rows.front().size();
        bool valid=state.size()>0 && rows.size()==state.size() && width>0;
        for(const auto& row:rows)valid=valid && row.size()==width;
        protocol.agree({operation,state.size(),rows.size(),width,Word(valid)});
        if(!valid)throw std::invalid_argument("invalid row matrix or uninitialized state");
    }
};
Initializer::Initializer(int party,emp::NetIO& io):impl_(std::make_unique<Impl>(party,io)){}
Initializer::~Initializer()=default;
std::uint64_t Initializer::sent() const{return impl_->protocol.sent();}
std::uint64_t Initializer::received() const{return impl_->protocol.received();}
InitState Initializer::Init(std::size_t vertices,const Words& src,const Words& dst,IndexSharing sharing) {
    auto& p=impl_->protocol;const auto n=src.size();
    bool valid=vertices>0 && vertices<=n && n<=(1u<<22) && dst.size()==n &&
        (sharing==IndexSharing::ModuloVertices || sharing==IndexSharing::Ring64);
    if(sharing==IndexSharing::ModuloVertices) {
        for(auto x:src)valid=valid && x<vertices;
        for(auto x:dst)valid=valid && x<vertices;
    }
    p.agree({1,vertices,n,dst.size(),Word(sharing),Word(valid)});
    if(!valid)throw std::invalid_argument("invalid vertex-ordered endpoint shares");
    Words endpoints(src);endpoints.insert(endpoints.end(),dst.begin(),dst.end());
    if(sharing==IndexSharing::ModuloVertices)endpoints=p.NormalizeIndices(endpoints,vertices);
    Rows keys(n,Words(2));
    for(std::size_t i=0;i<n;++i) {
        // The original row ID is a SECRET tie-break after shuffling. Distinct
        // keys make the opened comparison transcript a function of the mapping.
        const Word source_tag=i<vertices?0:1,dest_tag=i<vertices?1:0;
        keys[i][0]=2*n*endpoints[i]+(p.party()==0?source_tag*n+i:0);
        keys[i][1]=2*n*endpoints[n+i]+(p.party()==0?dest_tag*n+i:0);
    }
    InitState state;state.n_=n;state.vertices_=vertices;
    state.a_=p.random_permutation(n);state.b_=p.random_permutation(n);
    auto a=p.Shuffle(std::move(keys),state.a_);
    Rows dest(n,Words(1));
    Words source_keys(n);
    for(std::size_t i=0;i<n;++i){source_keys[i]=a[i][0];dest[i][0]=a[i][1];}
    auto b=p.Shuffle(std::move(dest),state.b_);
    Words dest_keys(n);for(std::size_t i=0;i<n;++i)dest_keys[i]=b[i][0];
    unsigned width=0;for(Word bound=2*Word(vertices)*n;bound;bound>>=1)++width;
    state.source_=p.InsecureSort(source_keys,width);
    state.destination_=p.InsecureSort(dest_keys,width);
    // C = undo B, then undo A. C is kept both as local factor states for
    // application and as additive shares of its pull mapping. Never opened.
    Rows identity(n,Words(1));
    if(p.party()==0)for(std::size_t i=0;i<n;++i)identity[i][0]=i;
    auto c=p.Shuffle(p.Shuffle(std::move(identity),state.b_,true),state.a_,true);
    state.c_share_.resize(n);for(std::size_t i=0;i<n;++i)state.c_share_[i]=c[i][0];
    return state;
}
OrderShares Initializer::ExportOrderShares(const InitState& s) {
    auto& p=impl_->protocol;
    Rows ids(s.size(),Words(1));
    if(p.party()==0)for(std::size_t i=0;i<s.size();++i)ids[i][0]=i;
    impl_->validate(s,ids,5);
    // Move destination-position labels BACKWARD to their input rows.
    auto vs=p.Shuffle(detail::public_permute(ids,s.source_,true),s.a_,true);
    auto sd=detail::public_permute(p.Shuffle(
        detail::public_permute(ids,s.destination_,true),s.b_,true),s.source_);
    auto dv=detail::public_permute(p.Shuffle(p.Shuffle(ids,s.a_),s.b_),s.destination_);
    OrderShares out;
    for(std::size_t i=0;i<s.size();++i) {
        out.vertex_to_source.push_back(vs[i][0]);
        out.source_to_destination.push_back(sd[i][0]);
        out.destination_to_vertex.push_back(dv[i][0]);
    }
    return out;
}
Rows Initializer::VertexToSource(const InitState& s,Rows rows) {
    impl_->validate(s,rows,2);
    return detail::public_permute(impl_->protocol.Shuffle(std::move(rows),s.a_),s.source_);
}
Rows Initializer::SourceToDestination(const InitState& s,Rows rows) {
    impl_->validate(s,rows,3);
    return detail::public_permute(impl_->protocol.Shuffle(
        detail::public_permute(rows,s.source_,true),s.b_),s.destination_);
}
Rows Initializer::DestinationToVertex(const InitState& s,Rows rows) {
    impl_->validate(s,rows,4);
    return impl_->protocol.Shuffle(impl_->protocol.Shuffle(
        detail::public_permute(rows,s.destination_,true),s.b_,true),s.a_,true);
}
}
