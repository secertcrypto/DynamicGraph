#pragma once
#include "src/common/circuit/circuit.h"
#include <functional>
#include <stdexcept>

// Shared application semantics for static Graphiti and dynamic graph execution.
namespace graphiti::application {
using Word=std::uint64_t;
using Wire=threepc::wire_t;
using Circuit=threepc::Circuit<Word>;
using threepc::GateType;

class Encoding {
    unsigned ell_;
public:
    // Zero selects the smallest supported ell with 2^ell > 2*Dmax.
    explicit Encoding(Word Dmax,unsigned ell=0);
    unsigned shift() const {return ell_;}
    unsigned value_bits() const {return 64-ell_;}
    Word scale() const {return Word{1}<<ell_;}
    Word value_mask() const {return ~Word{0}>>ell_;}
    Word encode(Word local_message) const {return local_message<<ell_;}
};

// Arithmetic gates respect projection Z_(2^64) -> Z_q. Zero and sign tests
// use S*x so high representative bits cannot affect comparison in Z_q.
class RingOps {
    Circuit& c_; const Encoding& enc_;
public:
    RingOps(Circuit& c,const Encoding& enc):c_(c),enc_(enc){}
    Wire add(Wire a,Wire b){return c_.addGate(threepc::GateType::kAdd,a,b);}
    Wire sub(Wire a,Wire b){return c_.addGate(threepc::GateType::kSub,a,b);}
    Wire mul(Wire a,Wire b){return c_.addGate(threepc::GateType::kMul,a,b);}
    Wire scale(Wire a,Word k){return c_.addCGate(threepc::GateType::kCMul,a,k);}
    Wire add_public(Wire a,Word k){return c_.addCGate(threepc::GateType::kCAdd,a,k);}
    Wire is_zero(Wire a){return c_.addEqzGate(scale(a,enc_.scale()));}
    Wire negative(Wire a){return c_.addLtzGate(scale(a,enc_.scale()));}
};
using Function=std::function<Wire(RingOps&,Wire,Wire)>;
struct Program {
    Function apply_e,apply_v;
    bool count_must_fit=false;
    static Program reachability(); // bounded-hop BFS; enforces indegree-count fit
    static Program weighted_sum(); // m=h_src*w; H'=H+sum(incoming m)
};
inline Encoding::Encoding(Word d,unsigned ell):ell_(ell) {
    if(d >= (Word{1}<<62))throw std::invalid_argument("Dmax too large for a nontrivial value ring");
    if(!ell_){ell_=1;while((Word{1}<<ell_)<=2*d)++ell_;}
    if(ell_>63 || scale()<=2*d)throw std::invalid_argument("require 1<=ell<=63 and 2^ell>2*Dmax");
}

inline Program Program::weighted_sum(){
    return {[](RingOps& op,Wire h,Wire w){return op.mul(h,w);},
            [](RingOps& op,Wire old,Wire a){return op.add(old,a);}};
}

inline Program Program::reachability(){
    return {[](RingOps& op,Wire h,Wire){
                return op.add_public(op.scale(op.is_zero(h),~Word{0}),1);
            },
            [](RingOps& op,Wire old,Wire a){
                auto neither=op.mul(op.is_zero(old),op.is_zero(a));
                return op.add_public(op.scale(neither,~Word{0}),1);
            },true};
}

} // namespace graphiti::application
