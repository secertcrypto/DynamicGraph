#pragma once
#include "init.h"
#include "src/common/circuit/circuit.h"

namespace graphiti::twopc {
// uint64 arithmetic circuit evaluator. OT work is performed during evaluation;
// prepare() validates/allocates only. No dealer or helper is involved.
class Evaluator {
    struct Impl;
    std::unique_ptr<Impl> impl_;
public:
    Evaluator(int party,emp::NetIO& io);
    ~Evaluator();
    void prepare(const threepc::LevelOrderedCircuit&);
    void setInputs(const std::vector<threepc::wire_t>&,const Words&);
    void evalLevel(std::size_t,const threepc::LevelOrderedCircuit&);
    Word share(threepc::wire_t) const;
    Words outputs(const threepc::LevelOrderedCircuit&);
    std::uint64_t sent() const;
    std::uint64_t received() const;
};
}
