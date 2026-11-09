#include "evaluator.h"
#include "protocol.h"
#include <map>
#include <stdexcept>
#include <unordered_map>

namespace graphiti::twopc {
using namespace threepc;
namespace {
bool local(GateType t) {
    return t==GateType::kInp || t==GateType::kAdd || t==GateType::kSub ||
        t==GateType::kCAdd || t==GateType::kCSub || t==GateType::kCMul || t==GateType::kLocalPerm;
}
}
struct Evaluator::Impl {
    detail::Protocol p;
    Words values,plain;
    std::vector<bool> known;
    std::unordered_map<wire_t,Word> inputs;
    std::map<std::pair<int,int>,std::vector<int>> groups;
    const LevelOrderedCircuit* circuit=nullptr;
    std::size_t next=0,total=0;
    Impl(int party,emp::NetIO& io):p(party,io){}
    Word v(wire_t w) const {return values.at(w);}
    Words read(const std::vector<wire_t>& wires) const {
        Words out;out.reserve(wires.size());for(auto w:wires)out.push_back(v(w));return out;
    }
    void write(const std::vector<wire_t>& wires,const Words& out) {
        if(wires.size()!=out.size())throw std::logic_error("two-party output shape");
        for(std::size_t i=0;i<wires.size();++i)values.at(wires[i])=out[i];
    }
    void permute(const std::vector<wire_t>& ins,const std::vector<wire_t>& outs,
                 int group,bool inverse=false,int owner=-1) {
        std::vector<int> fresh;
        const std::vector<int>* permutation;
        if(group<0){fresh=p.random_permutation(ins.size());permutation=&fresh;}
        else {
            auto key=std::make_pair(owner,group);auto it=groups.find(key);
            if(it==groups.end()) {
                if(inverse)throw std::logic_error("unshuffle without prior group");
                it=groups.emplace(key,p.random_permutation(ins.size())).first;
            }
            if(it->second.size()!=ins.size())throw std::invalid_argument("shuffle group size mismatch");
            permutation=&it->second;
        }
        Rows rows(ins.size(),Words(1));
        for(std::size_t i=0;i<ins.size();++i)rows[i][0]=v(ins[i]);
        rows=owner<0 ? p.Shuffle(std::move(rows),*permutation,inverse)
            : p.PrivatePermute(std::move(rows),p.party()==owner?*permutation:std::vector<int>{},owner);
        for(std::size_t i=0;i<outs.size();++i)values.at(outs[i])=rows[i][0];
    }
    void evalLocal(const Gate& gate) {
        switch(gate.type) {
        case GateType::kInp:
            if(gate.owner==p.party()) {
                auto it=inputs.find(gate.out);
                if(it==inputs.end())throw std::invalid_argument("missing two-party input");
                values.at(gate.out)=it->second;
            }
            break;
        case GateType::kAdd: case GateType::kSub: {
            auto& g=static_cast<const FIn2Gate&>(gate);
            values.at(g.out)=g.type==GateType::kAdd?v(g.in1)+v(g.in2):v(g.in1)-v(g.in2);break;
        }
        case GateType::kCAdd: case GateType::kCSub: case GateType::kCMul: {
            auto& g=static_cast<const CIn1Gate<Word>&>(gate);
            Word c=p.party()==0?g.cval:0;
            values.at(g.out)=g.type==GateType::kCMul ? v(g.in)*g.cval :
                g.type==GateType::kCAdd ? v(g.in)+c : g.inv ? c-v(g.in) : v(g.in)-c;
            break;
        }
        case GateType::kLocalPerm: {
            auto& g=static_cast<const LocalPermGate&>(gate);std::vector<bool> seen(g.outs.size());
            for(std::size_t i=0;i<g.outs.size();++i) {
                auto w=g.perm_wires.at(i);
                if(!known.at(w) || plain.at(w)>=g.outs.size() || seen[plain[w]])
                    throw std::invalid_argument("invalid public permutation");
                auto j=plain[w];seen[j]=true;
                values.at(g.outs[g.inv?j:i])=v(g.payload.at(g.inv?i:j));
            }
            break;
        }
        default:throw std::logic_error("unexpected local gate");
        }
    }
};
Evaluator::Evaluator(int party,emp::NetIO& io):impl_(std::make_unique<Impl>(party,io)){}
Evaluator::~Evaluator()=default;
void Evaluator::prepare(const LevelOrderedCircuit& c) {
    auto& s=*impl_;
    if(s.circuit && s.next!=s.total)throw std::logic_error("unfinished two-party circuit");
    for(const auto& level:c.gates_by_level)for(const auto& gp:level) {
        if((gp->type==GateType::kInp || gp->type==GateType::kRecP) && (gp->owner<0 || gp->owner>1))
            throw std::invalid_argument("two-party input/output owner must be 0 or 1");
        if(gp->type==GateType::kPermSh) {
            auto& g=static_cast<const PermShGate&>(*gp);
            if(g.target<0 || g.target>1)throw std::invalid_argument("two-party permutation owner");
        }
        if(gp->type==GateType::kAmorPermShare && static_cast<const AmorPermShareGate&>(*gp).outs.size()!=2)
            throw std::invalid_argument("two-party permutation needs two outputs");
        if(gp->type>=GateType::kInvalid)throw std::invalid_argument("unsupported two-party gate");
    }
    s.circuit=&c;s.next=0;s.total=c.depth();s.inputs.clear();s.groups.clear();
    s.values.assign(c.num_wires,0);s.plain.assign(c.num_wires,0);s.known.assign(c.num_wires,false);
}
void Evaluator::setInputs(const std::vector<wire_t>& wires,const Words& values) {
    auto& s=*impl_;
    if(!s.circuit || s.next || wires.size()!=values.size())throw std::invalid_argument("two-party input staging");
    for(std::size_t i=0;i<wires.size();++i) {
        if(wires[i]>=s.values.size())throw std::invalid_argument("input wire out of range");
        s.inputs[wires[i]]=values[i];
    }
}
void Evaluator::evalLevel(std::size_t depth,const LevelOrderedCircuit& c) {
    auto& s=*impl_;
    if(s.circuit!=&c || depth!=s.next || depth>=c.depth())throw std::logic_error("two-party circuit level order");
    const auto& level=c.gates_by_level[depth];
    for(auto type:{GateType::kMul,GateType::kEqz,GateType::kLtz,GateType::kRec,GateType::kRecP}) {
        for(int target=0;target<(type==GateType::kRecP?2:1);++target) {
            Words a,b;std::vector<wire_t> out;
            for(const auto& gp:level)if(gp->type==type && (type!=GateType::kRecP || gp->owner==target)) {
                out.push_back(gp->out);
                if(type==GateType::kMul) {
                    auto& g=static_cast<const FIn2Gate&>(*gp);a.push_back(s.v(g.in1));b.push_back(s.v(g.in2));
                } else a.push_back(s.v(static_cast<const FIn1Gate&>(*gp).in));
            }
            if(out.empty())continue;
            Words result;
            if(type==GateType::kMul)result=s.p.Multiply(a,b);
            else if(type==GateType::kEqz)result=s.p.EqualZero(a);
            else if(type==GateType::kLtz)result=s.p.BitToArithmetic(s.p.ToBits(a,64).back());
            else {
                result=s.p.OpenWords(a,type==GateType::kRec?-1:target);
                for(std::size_t i=0;i<out.size();++i) {
                    s.plain[out[i]]=result[i];s.known[out[i]]=type==GateType::kRec;
                    if(s.p.party()!=(type==GateType::kRec?0:target))result[i]=0;
                }
            }
            s.write(out,result);
        }
    }
    for(const auto& gp:level) {
        if(gp->type==GateType::kShuffle) {
            auto& g=static_cast<const ShuffleGate&>(*gp);s.permute(g.ins,g.outs,g.perm_group_id);
        } else if(gp->type==GateType::kUnshuffle) {
            auto& g=static_cast<const UnshuffleGate&>(*gp);s.permute(g.ins,g.outs,g.perm_group_id,true);
        } else if(gp->type==GateType::kPermSh) {
            auto& g=static_cast<const PermShGate&>(*gp);s.permute(g.ins,g.outs,g.perm_group_id,false,g.target);
        } else if(gp->type==GateType::kAmorPermShare) {
            auto& g=static_cast<const AmorPermShareGate&>(*gp);
            for(int owner=0;owner<2;++owner)s.permute(g.ins,g.outs[owner],g.perm_group_id,false,owner);
        }
    }
    for(const auto& gp:level)if(local(gp->type))s.evalLocal(*gp);
    ++s.next;
}
Word Evaluator::share(wire_t w) const {return impl_->v(w);}
Words Evaluator::outputs(const LevelOrderedCircuit& c) {
    if(impl_->circuit!=&c || impl_->next!=c.depth())throw std::logic_error("unfinished output");
    return impl_->p.OpenWords(impl_->read(c.outputs));
}
std::uint64_t Evaluator::sent() const{return impl_->p.sent();}
std::uint64_t Evaluator::received() const{return impl_->p.received();}
}
