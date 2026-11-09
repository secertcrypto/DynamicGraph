#pragma once
#include "src/graphiti/message_passing.h"
#include "src/common/protocol_runner.h"
#include "risa/update.h"
#include <string_view>

namespace dynamic_graph {
// Input wires are constructed identically on all three processes. Only P0/P1
// stage their local RISA shares. No graph or delta value is opened here.
class NphDeltaInput {
public:
    NphDeltaInput(threepc::Circuit<risa::Word>& circuit, const graphiti::Wires& base) {
        for (auto w : base) {
            auto p0 = circuit.newInputWire(threepc::P0);
            auto p1 = circuit.newInputWire(threepc::P1);
            inputs_[0].push_back(p0);
            inputs_[1].push_back(p1);
            auto delta = circuit.addGate(threepc::GateType::kAdd, p0, p1);
            combined_.push_back(circuit.addGate(threepc::GateType::kAdd, w, delta));
        }
    }
    const graphiti::Wires& combined() const { return combined_; }

    // Call before the runner's online phase. The caller retains the LWR bound.
    void stage(threepc::protocol::IProtocolRunner<risa::Word>& runner,
               const risa::ApproximateContribution* share) const {
        if (std::string_view(runner.protocolName()) != "nph" || runner.numComputeParties() != 2)
            throw std::invalid_argument("RISA bridge requires two NPH compute parties");
        if (runner.isHelper()) {
            if (share) throw std::invalid_argument("helper must not receive a RISA share");
            return;
        }
        if (!share || share->shares.size()!=combined_.size())
            throw std::invalid_argument("wrong RISA vertex-share count");
        runner.setInputs(inputs_[runner.pid()], share->shares);
    }
private:
    graphiti::Wires inputs_[2], combined_;
};
}
