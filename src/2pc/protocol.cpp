#include "protocol.h"
#include "libsss/benes.h"
#include <algorithm>
#include <cstring>
#include <limits>
#include <numeric>
#include <stdexcept>

namespace graphiti::twopc::detail {
namespace {
Block pack(Word a,Word b=0) {Word v[2]{a,b};Block out;std::memcpy(&out,v,sizeof out);return out;}
std::array<Word,2> unpack(Block b) {std::array<Word,2> v;std::memcpy(v.data(),&b,sizeof b);return v;}
std::size_t padded(std::size_t n) {std::size_t p=1;while(p<n)p*=2;return p;}
unsigned bits(Word x) {unsigned b=0;do{++b;x>>=1;}while(x);return b;}
Layers topology(std::size_t p) {
    unsigned depth=bits(p)-1;
    Layers out(2*depth-1);
    std::vector<std::size_t> ids(p);std::iota(ids.begin(),ids.end(),0);
    auto visit=[&](auto&& self,const std::vector<std::size_t>& wires,
                  unsigned level,std::size_t slot)->void {
        auto n=wires.size();
        if(n==2){out[level].push_back({wires[0],wires[1],slot});return;}
        auto last=level+2*(bits(n)-1)-2;
        std::vector<std::size_t> even,odd;
        for(std::size_t i=0;i<n;i+=2) {
            Gate g{wires[i],wires[i+1],slot+i/2};
            out[level].push_back(g);out[last].push_back(g);
            even.push_back(wires[i]);odd.push_back(wires[i+1]);
        }
        self(self,even,level+1,slot);self(self,odd,level+1,slot+n/4);
    };
    visit(visit,ids,0,0);
    return out;
}
}
Protocol::Protocol(int party,emp::NetIO& io):party_(party),io_(io),ot0_(&io_),ot1_(&io_) {
    if(party!=0 && party!=1)throw std::invalid_argument("party must be 0 or 1");
}
Words Protocol::Multiply(const Words& x,const Words& y) {
    if(x.size()!=y.size())throw std::invalid_argument("multiplication size mismatch");
    Words out(x.size());
    for(std::size_t i=0;i<x.size();++i)out[i]=x[i]*y[i];
    // Two cross products via bit-decomposed 1-out-of-2 OT. Chunk to bound memory.
    constexpr std::size_t chunk=1024;
    for(int sender=0;sender<2;++sender)for(std::size_t begin=0;begin<x.size();begin+=chunk) {
        auto end=std::min(x.size(),begin+chunk),n=(end-begin)*64;
        std::vector<Block> a,b,received;Bits choices(n);
        if(party_==sender){a.resize(n);b.resize(n);}
        for(std::size_t i=begin;i<end;++i)for(unsigned bit=0;bit<64;++bit) {
            auto j=(i-begin)*64+bit;
            if(party_==sender) {
                Word r=random_word();out[i]-=r;
                a[j]=pack(r);b[j]=pack(r+(x[i]<<bit));
            } else choices[j]=(y[i]>>bit)&1;
        }
        transfer(sender,a,b,choices,received);
        if(party_!=sender)for(std::size_t i=begin;i<end;++i)
            for(unsigned bit=0;bit<64;++bit)out[i]+=unpack(received[(i-begin)*64+bit])[0];
    }
    return out;
}
Words Protocol::EqualZero(const Words& x,unsigned width) {
    if(x.empty())return {};
    auto bits=ToBits(x,width);
    // AND tree over the complemented XOR-shared bits; never open the predicate.
    if(party_==0)for(auto& row:bits)for(auto& bit:row)bit^=1;
    while(bits.size()>1) {
        Bits a,b;
        for(std::size_t k=0;k+1<bits.size();k+=2) {
            a.insert(a.end(),bits[k].begin(),bits[k].end());
            b.insert(b.end(),bits[k+1].begin(),bits[k+1].end());
        }
        auto product=And(a,b);BitRows next;
        for(std::size_t offset=0;offset<product.size();offset+=x.size())
            next.emplace_back(product.begin()+offset,product.begin()+offset+x.size());
        if(bits.size()%2)next.push_back(std::move(bits.back()));
        bits=std::move(next);
    }
    return BitToArithmetic(bits.front());
}
Words Protocol::OpenWords(const Words& local,int target) {
    if(target < -1 || target>1)throw std::invalid_argument("invalid opening target");
    Words other(local.size(),0);auto bytes=local.size()*sizeof(Word);
    if(target<0) {
        if(party_==0){io_.send_data(local.data(),bytes);io_.flush();io_.recv_data(other.data(),bytes);}
        else{io_.recv_data(other.data(),bytes);io_.send_data(local.data(),bytes);io_.flush();}
    } else if(party_==target)io_.recv_data(other.data(),bytes);
    else{io_.send_data(local.data(),bytes);io_.flush();return other;}
    for(std::size_t i=0;i<local.size();++i)other[i]+=local[i];
    return other;
}
void Protocol::agree(const Words& local) {
    Words peer(local.size());auto bytes=local.size()*sizeof(Word);
    if(party_==0){io_.send_data(local.data(),bytes);io_.flush();io_.recv_data(peer.data(),bytes);}
    else{io_.recv_data(peer.data(),bytes);io_.send_data(local.data(),bytes);io_.flush();}
    if(peer!=local)throw std::invalid_argument("two-party public parameters/call order disagree");
}
Word Protocol::random_word(){Word w;rng_.random_data(&w,sizeof w);return w;}
std::vector<int> Protocol::random_permutation(std::size_t n) {
    std::vector<int> p(n);std::iota(p.begin(),p.end(),0);
    for(std::size_t i=n;i>1;--i) {
        Word bound=i,threshold=(Word{0}-bound)%bound,r;
        do{r=random_word();}while(r<threshold); // unbiased Fisher-Yates
        std::swap(p[i-1],p[r%bound]);
    }
    return p;
}
void Protocol::transfer(int sender,const std::vector<Block>& a,const std::vector<Block>& b,
                        const Bits& choice,std::vector<Block>& result) {
    auto n=choice.size();if(!n)return;
    if(n>std::size_t(std::numeric_limits<int>::max()))throw std::length_error("OT batch too large");
    auto& ot=sender==0?ot0_:ot1_;
    if(party_==sender)ot.send(a.data(),b.data(),n);
    else {
        auto c=std::make_unique<bool[]>(n);
        for(std::size_t i=0;i<n;++i)c[i]=choice[i]!=0;
        result.resize(n);ot.recv(result.data(),c.get(),n);
    }
    io_.flush();
}
Bits Protocol::And(const Bits& a,const Bits& b) {
    if(a.size()!=b.size())throw std::invalid_argument("bit vector mismatch");
    Bits out(a.size());
    for(std::size_t i=0;i<a.size();++i)out[i]=a[i]&b[i];
    for(int sender=0;sender<2;++sender) {
        std::vector<Block> m0,m1,received;
        if(party_==sender) {
            m0.resize(a.size());m1.resize(a.size());
            for(std::size_t i=0;i<a.size();++i) {
                auto r=random_word()&1;out[i]^=r;
                m0[i]=pack(r);m1[i]=pack(r^a[i]);
            }
        }
        transfer(sender,m0,m1,b,received);
        if(party_!=sender)for(std::size_t i=0;i<a.size();++i)out[i]^=unpack(received[i])[0]&1;
    }
    return out;
}
Bits Protocol::Open(const Bits& local) {
    Bits out(local.size());
    if(party_==0){io_.send_data(local.data(),local.size());io_.flush();io_.recv_data(out.data(),out.size());}
    else{io_.recv_data(out.data(),out.size());io_.send_data(local.data(),local.size());io_.flush();}
    for(std::size_t i=0;i<out.size();++i)out[i]^=local[i];
    return out;
}
BitRows Protocol::ToBits(const Words& local,unsigned width) {
    if(!width || width>64)throw std::invalid_argument("invalid bit width");
    BitRows out(width,Bits(local.size()));Bits carry(local.size(),0);
    for(unsigned k=0;k<width;++k) {
        Bits a(local.size()),b(local.size());
        for(std::size_t i=0;i<local.size();++i) {
            auto bit=(local[i]>>k)&1;
            a[i]=(party_==0?bit:0)^carry[i];
            b[i]=(party_==1?bit:0)^carry[i];
            out[k][i]=bit^carry[i];
        }
        if(k+1<width) {
            auto ab=And(a,b);
            for(std::size_t i=0;i<carry.size();++i)carry[i]^=ab[i];
        }
    }
    return out;
}
Bits Protocol::Greater(const BitRows& a,const BitRows& b) {
    Bits carry(a[0].size(),0);
    // Carry-out of a + (~b), without carry-in, is [a > b].
    for(std::size_t k=0;k<a.size();++k) {
        Bits ac(carry.size()),bc(carry.size());
        for(std::size_t i=0;i<carry.size();++i) {
            ac[i]=a[k][i]^carry[i];
            bc[i]=b[k][i]^(party_==0)^carry[i];
        }
        auto ab=And(ac,bc);
        for(std::size_t i=0;i<carry.size();++i)carry[i]^=ab[i];
    }
    return carry;
}
Words Protocol::BitToArithmetic(const Bits& local) {
    Words out(local.size());std::vector<Block> a,b,received;
    if(party_==0) {
        a.resize(local.size());b.resize(local.size());
        for(std::size_t i=0;i<local.size();++i) {
            out[i]=random_word();a[i]=pack(Word(local[i])-out[i]);b[i]=pack(Word(1-local[i])-out[i]);
        }
    }
    transfer(0,a,b,local,received);
    if(party_==1)for(std::size_t i=0;i<local.size();++i)out[i]=unpack(received[i])[0];
    return out;
}
Words Protocol::NormalizeIndices(const Words& local,Word modulus) {
    const auto width=bits(2*modulus-1);
    auto sum=ToBits(local,width);
    BitRows limit(width,Bits(local.size(),0));
    if(party_==0)for(unsigned k=0;k<width;++k)
        std::fill(limit[k].begin(),limit[k].end(),((modulus-1)>>k)&1);
    auto correction=BitToArithmetic(Greater(sum,limit));
    Words out(local);
    for(std::size_t i=0;i<out.size();++i)out[i]-=modulus*correction[i];
    return out;
}
Rows Protocol::PrivatePermute(Rows rows,const std::vector<int>& permutation,int owner) {
    const auto n=rows.size();if(!n)return rows;
    const auto cols=rows[0].size(),p=padded(n);
    if(p==1 || !cols)return rows;
    rows.resize(p,Words(cols));
    std::unique_ptr<libsss::Benes> route;
    if(party_==owner) {
        if(permutation.size()!=n)throw std::invalid_argument("wrong permutation size");
        auto full=permutation;
        for(auto i=n;i<p;++i)full.push_back(int(i));
        route=std::make_unique<libsss::Benes>(full);
    }
    const auto layers=topology(p);
    for(std::size_t level=0;level<layers.size();++level) {
        const auto& gates=layers[level];auto count=gates.size()*cols;
        std::vector<Block> a,b,received;Bits choices(count);
        if(party_!=owner){a.resize(count);b.resize(count);}
        for(std::size_t j=0;j<gates.size();++j) {
            const auto& g=gates[j];
            for(std::size_t c=0;c<cols;++c) {
                auto i=j*cols+c;
                Word x=rows[g.left][c],y=rows[g.right][c];
                if(party_!=owner) {
                    Word r0=random_word(),r1=random_word();
                    a[i]=pack(x-r0,y-r1);b[i]=pack(y-r0,x-r1);
                    rows[g.left][c]=r0;rows[g.right][c]=r1;
                } else choices[i]=route->switched[level][g.slot];
            }
        }
        transfer(1-owner,a,b,choices,received);
        if(party_==owner)for(std::size_t j=0;j<gates.size();++j) {
            const auto& g=gates[j];
            for(std::size_t c=0;c<cols;++c) {
                auto i=j*cols+c;auto v=unpack(received[i]);
                if(choices[i])std::swap(rows[g.left][c],rows[g.right][c]);
                rows[g.left][c]+=v[0];rows[g.right][c]+=v[1];
            }
        }
    }
    rows.resize(n);return rows;
}
std::vector<int> inverse(const std::vector<int>& p) {
    std::vector<int> out(p.size());
    for(std::size_t i=0;i<p.size();++i)out.at(p[i])=i;
    return out;
}
Rows Protocol::Shuffle(Rows rows,const std::vector<int>& local,bool inv) {
    auto p=inv?inverse(local):local;
    for(int step=0;step<2;++step) {
        int owner=inv?1-step:step;
        rows=PrivatePermute(std::move(rows),party_==owner?p:std::vector<int>{},owner);
    }
    return rows;
}
std::vector<std::size_t> Protocol::InsecureSort(const Words& keys,unsigned width) {
    auto n=keys.size(),p=padded(n);
    auto shared=ToBits(keys,width);
    // Public +infinity padding: real keys are strictly below 2^width-1.
    for(auto& row:shared)row.resize(p,party_==0?1:0);
    std::vector<std::size_t> mapping(p);std::iota(mapping.begin(),mapping.end(),0);
    for(std::size_t k=2;k<=p;k*=2)for(std::size_t stride=k/2;stride;stride/=2) {
        std::vector<std::pair<std::size_t,std::size_t>> pairs;
        for(std::size_t i=0;i<p;++i)if((i^stride)>i)pairs.emplace_back(i,i^stride);
        BitRows a(width,Bits(pairs.size())),b=a;
        for(std::size_t j=0;j<pairs.size();++j) {
            auto [left,right]=pairs[j];
            if(left&k)std::swap(left,right);
            for(unsigned bit=0;bit<width;++bit){a[bit][j]=shared[bit][left];b[bit][j]=shared[bit][right];}
        }
        const auto swap=Open(Greater(a,b)); // the only intentional opening
        for(std::size_t j=0;j<pairs.size();++j)if(swap[j]) {
            auto [left,right]=pairs[j];std::swap(mapping[left],mapping[right]);
            for(auto& row:shared)std::swap(row[left],row[right]);
        }
    }
    mapping.resize(n);return mapping;
}
Rows public_permute(const Rows& in,const std::vector<std::size_t>& p,bool inv) {
    if(in.size()!=p.size())throw std::invalid_argument("public mapping size mismatch");
    Rows out(in.size());
    for(std::size_t i=0;i<in.size();++i) {
        if(inv)out.at(p[i])=in[i];else out[i]=in.at(p[i]);
    }
    return out;
}
}
