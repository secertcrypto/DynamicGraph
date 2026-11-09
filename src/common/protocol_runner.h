#pragma once
#ifdef CRIS_HAS_2PC
#include "src/2pc/evaluator.h"
#endif
#include <type_traits>

#include "src/3pc/arith/offline_evaluator.h"
#include "src/3pc/arith/online_evaluator.h"
#include "src/common/circuit/circuit.h"
#include "src/3pc/net/net3p.h"
#include "src/nph/arith/offline_evaluator.h"
#include "src/nph/arith/online_evaluator.h"
#include "src/nph/net/net_np.h"
#include "src/nph/utils/preproc.h"
#include "src/nph/utils/prg_np.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <memory>
#include <random>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace threepc::protocol {

enum class ProtocolKind {
  Rss3,
  Nph,
  TwoParty
};

inline ProtocolKind parseProtocolKind(const std::string& s) {
  if (s == "rss3" || s == "3pc" || s == "rss") return ProtocolKind::Rss3;
  if (s == "nph" || s == "nparty-helper" || s == "np-helper") return ProtocolKind::Nph;
  if (s == "two_party" || s == "2pc") return ProtocolKind::TwoParty;
  throw std::invalid_argument("Unknown protocol. Use rss3, nph, or two_party");
}

inline const char* protocolName(ProtocolKind p) {
  switch (p) {
    case ProtocolKind::TwoParty: return "two_party";
    case ProtocolKind::Rss3: return "rss3";
    case ProtocolKind::Nph:  return "nph";
  }
  return "unknown";
}

struct ProtocolConfig {
  ProtocolKind kind{ProtocolKind::Rss3};
  int pid{-1};

  // Number of compute parties. For rss3 this must be 3. For nph, helper pid
  // is num_compute_parties and total processes are num_compute_parties + 1.
  int num_compute_parties{3};

  int port{13700};
  std::string peer{"127.0.0.1"};

  // NPH only: use P0 as reconstruction king.  In this mode, reconstruct-to-all
  // uses two communication rounds: parties send shares to P0, then P0 sends the
  // reconstructed plaintexts to all parties.  rss3 ignores this flag.
  bool pking{false};

  // NPH only: force generic shuffle/unshuffle even when the optimized
  // two-compute-party protocol would otherwise be selected.
  bool disable_optimized_shuffle{false};
};

namespace detail {

template <typename T>
inline T randomRingValue(std::mt19937_64& rng) {
  T v{};
  auto* out = reinterpret_cast<unsigned char*>(&v);

  size_t off = 0;
  while (off < sizeof(T)) {
    uint64_t r = rng();
    const size_t take = std::min(sizeof(uint64_t), sizeof(T) - off);
    std::memcpy(out + off, &r, take);
    off += take;
  }

  return v;
}

template <typename T>
inline std::vector<T> randomRingVector(size_t n) {
  std::random_device rd;
  std::mt19937_64 rng((static_cast<uint64_t>(rd()) << 32) ^ rd());

  std::vector<T> vals(n);
  for (auto& v : vals) v = randomRingValue<T>(rng);
  return vals;
}

}  // namespace detail

template <typename T>
class IProtocolRunner {
 public:
  virtual ~IProtocolRunner() = default;

  virtual int pid() const = 0;
  virtual int numParties() const = 0;          // total processes including helper
  virtual int numComputeParties() const = 0;   // data-owning compute parties
  virtual bool isHelper() const = 0;
  virtual const char* protocolName() const = 0;

  virtual uint64_t bytesSentTo(int party) const = 0;
  virtual uint64_t bytesRecvFrom(int party) const = 0;
  virtual void resetCounters() = 0;
  virtual void increaseSocketBuffers(int buffer_size) = 0;

  // Stage plaintext values for input wires owned by this process. These values
  // are pushed into the concrete online evaluator before online evaluation
  // starts. Calling this after online evaluation starts is an error.
  virtual void setInputs(const std::vector<wire_t>& ws,
                         const std::vector<T>& vals) = 0;

  // Convenience benchmark helper: sample random plaintext values for the given
  // input wires and stage them as this process's inputs.
  virtual void setRandomInputs(const std::vector<wire_t>& ws) = 0;

  virtual void offline(const LevelOrderedCircuit& lc) = 0;

  // Evaluate the entire online phase.
  virtual void online(const LevelOrderedCircuit& lc) = 0;

  // Evaluate exactly one communication level. This is useful for fine-grained
  // benchmarking. Levels must be evaluated sequentially from 0 upward.
  virtual void evalLevel(size_t idx, const LevelOrderedCircuit& lc) = 0;

  virtual std::vector<T> getOutputs(const LevelOrderedCircuit& lc) = 0;
};

/** Wrapper for the existing 3-party replicated-secret-sharing backend. */
template <typename T>
class Rss3ProtocolRunner final : public IProtocolRunner<T> {
 public:
  Rss3ProtocolRunner(int pid, const std::string& peer, int port)
      : pid_(pid), net_(pid, makeIps(peer), port) {
    if (pid_ < 0 || pid_ >= 3)
      throw std::invalid_argument("Rss3ProtocolRunner: pid must be in {0,1,2}");
  }

  int pid() const override { return pid_; }
  int numParties() const override { return 3; }
  int numComputeParties() const override { return 3; }
  bool isHelper() const override { return false; }
  const char* protocolName() const override { return "rss3"; }

  uint64_t bytesSentTo(int party) const override { return net_.bytesSentTo(party); }
  uint64_t bytesRecvFrom(int party) const override { return net_.bytesRecvFrom(party); }
  void resetCounters() override { net_.resetCounters(); }
  void increaseSocketBuffers(int buffer_size) override { net_.increaseSocketBuffers(buffer_size); }

  void setInputs(const std::vector<wire_t>& ws,
                 const std::vector<T>& vals) override {
    if (ws.size() != vals.size())
      throw std::invalid_argument("Rss3ProtocolRunner::setInputs: mismatched input sizes");
    if (online_)
      throw std::runtime_error("Rss3ProtocolRunner::setInputs called after online evaluation started");

    input_wires_.push_back(ws);
    input_values_.push_back(vals);
  }

  void setRandomInputs(const std::vector<wire_t>& ws) override {
    setInputs(ws, detail::randomRingVector<T>(ws.size()));
  }

  void offline(const LevelOrderedCircuit& lc) override {
    OfflineEvaluator<T> offline(pid_, net_);
    offline.run(lc);
    prg_ = offline.take_prg();
    offline_done_ = true;
  }

  void online(const LevelOrderedCircuit& lc) override {
    if (online_ || next_level_ != 0)
      throw std::runtime_error("Rss3ProtocolRunner::online called after level-wise evaluation started");

    createOnlineEvaluator();
    online_->evaluate(lc);
    next_level_ = lc.gates_by_level.size();
  }

  void evalLevel(size_t idx, const LevelOrderedCircuit& lc) override {
    if (idx != next_level_) {
      throw std::runtime_error(
          "Rss3ProtocolRunner::evalLevel requires sequential levels starting at 0");
    }

    createOnlineEvaluator();
    online_->evalLevel(idx, lc);
    ++next_level_;
  }

  std::vector<T> getOutputs(const LevelOrderedCircuit& lc) override {
    if (!online_) throw std::runtime_error("Rss3ProtocolRunner: online/evalLevel not called");
    return online_->getOutputs(lc).vals;
  }

 private:
  void createOnlineEvaluator() {
    if (online_) return;
    if (!offline_done_)
      throw std::runtime_error("Rss3ProtocolRunner: offline() must be called before online/evalLevel");

    online_ = std::make_unique<OnlineEvaluator<T>>(pid_, net_, std::move(prg_));
    for (size_t i = 0; i < input_wires_.size(); ++i)
      online_->setInputs(input_wires_[i], input_values_[i]);
  }

  static const char** makeIps(const std::string& peer) {
    // Net3P consumes the strings during construction. Keep this static storage
    // valid through the constructor call.
    static std::string ips_storage[3];
    static const char* ips[3];
    ips_storage[0] = peer;
    ips_storage[1] = peer;
    ips_storage[2] = peer;
    ips[0] = ips_storage[0].c_str();
    ips[1] = ips_storage[1].c_str();
    ips[2] = ips_storage[2].c_str();
    return ips;
  }

  int pid_;
  Net3P net_;
  PRG3P prg_;
  bool offline_done_{false};
  std::unique_ptr<OnlineEvaluator<T>> online_;
  size_t next_level_{0};
  std::vector<std::vector<wire_t>> input_wires_;
  std::vector<std::vector<T>> input_values_;
};

/** Wrapper for the n-party additive-sharing backend with preprocessing helper. */
template <typename T>
class NphProtocolRunner final : public IProtocolRunner<T> {
 public:
  NphProtocolRunner(int pid,
                    int num_compute_parties,
                    const std::string& peer,
                    int port,
                    bool pking = false,
                    bool disable_optimized_shuffle = false)
      : pid_(pid),
        num_compute_parties_(num_compute_parties),
        net_(pid, num_compute_parties + 1, peer, port),
        pking_(pking),
        disable_optimized_shuffle_(disable_optimized_shuffle) {
    if (num_compute_parties_ < 2)
      throw std::invalid_argument("NphProtocolRunner: num_compute_parties must be >= 2");
    if (pid_ < 0 || pid_ > helper_pid())
      throw std::invalid_argument("NphProtocolRunner: pid must be in 0..num_parties");
  }

  int pid() const override { return pid_; }
  int numParties() const override { return num_compute_parties_ + 1; }
  int numComputeParties() const override { return num_compute_parties_; }
  bool isHelper() const override { return pid_ == helper_pid(); }
  const char* protocolName() const override { return "nph"; }

  uint64_t bytesSentTo(int party) const override { return net_.bytesSentTo(party); }
  uint64_t bytesRecvFrom(int party) const override { return net_.bytesRecvFrom(party); }
  void resetCounters() override { net_.resetCounters(); }
  void increaseSocketBuffers(int buffer_size) override { net_.increaseSocketBuffers(buffer_size); }

  void setInputs(const std::vector<wire_t>& ws,
                 const std::vector<T>& vals) override {
    if (ws.size() != vals.size())
      throw std::invalid_argument("NphProtocolRunner::setInputs: mismatched input sizes");
    if (online_)
      throw std::runtime_error("NphProtocolRunner::setInputs called after online evaluation started");

    input_wires_.push_back(ws);
    input_values_.push_back(vals);
  }

  void setRandomInputs(const std::vector<wire_t>& ws) override {
    setInputs(ws, detail::randomRingVector<T>(ws.size()));
  }

  void offline(const LevelOrderedCircuit& lc) override {
    nph::OfflineEvaluator<T> offline(
        pid_, num_compute_parties_, net_, disable_optimized_shuffle_);
    offline.run(lc);
    preproc_ = offline.take_preprocessing();
    pairwise_prg_ = offline.take_pairwise_prg();
    offline_done_ = true;
  }

  void online(const LevelOrderedCircuit& lc) override {
    if (online_ || next_level_ != 0)
      throw std::runtime_error("NphProtocolRunner::online called after level-wise evaluation started");

    createOnlineEvaluator();
    online_->evaluate(lc);
    next_level_ = lc.gates_by_level.size();
  }

  void evalLevel(size_t idx, const LevelOrderedCircuit& lc) override {
    if (idx != next_level_) {
      throw std::runtime_error(
          "NphProtocolRunner::evalLevel requires sequential levels starting at 0");
    }

    createOnlineEvaluator();
    online_->evalLevel(idx, lc);
    ++next_level_;
  }

  std::vector<T> getOutputs(const LevelOrderedCircuit& lc) override {
    if (!online_) throw std::runtime_error("NphProtocolRunner: online/evalLevel not called");
    return online_->getOutputs(lc).vals;
  }

 private:
  void createOnlineEvaluator() {
    if (online_) return;
    if (!offline_done_)
      throw std::runtime_error("NphProtocolRunner: offline() must be called before online/evalLevel");

    online_ = std::make_unique<nph::OnlineEvaluator<T>>(
        pid_,
        num_compute_parties_,
        net_,
        std::move(preproc_),
        std::move(pairwise_prg_),
        pking_,
        disable_optimized_shuffle_);
    for (size_t i = 0; i < input_wires_.size(); ++i)
      online_->setInputs(input_wires_[i], input_values_[i]);
  }

  int helper_pid() const { return num_compute_parties_; }

  int pid_;
  int num_compute_parties_;
  bool pking_{false};
  bool disable_optimized_shuffle_{false};
  nph::NetNP net_;
  nph::Preprocessing<T> preproc_;
  nph::PairwisePRG pairwise_prg_;
  bool offline_done_{false};
  std::unique_ptr<nph::OnlineEvaluator<T>> online_;
  size_t next_level_{0};
  std::vector<std::vector<wire_t>> input_wires_;
  std::vector<std::vector<T>> input_values_;
};

#ifdef CRIS_HAS_2PC
class TwoPartyProtocolRunner final : public IProtocolRunner<std::uint64_t> {
  using Word=std::uint64_t;
  int pid_;
  emp::NetIO io_;
  graphiti::twopc::Evaluator evaluator_;
  std::uint64_t sent_base_=0,recv_base_=0;
  std::vector<std::pair<std::vector<wire_t>,std::vector<Word>>> inputs_;
  bool prepared_=false;
 public:
  TwoPartyProtocolRunner(int pid,const std::string& peer,int port)
      :pid_(pid),io_(pid==0?nullptr:peer.c_str(),port,true),evaluator_(pid,io_){}
  int pid() const override{return pid_;}
  int numParties() const override{return 2;}
  int numComputeParties() const override{return 2;}
  bool isHelper() const override{return false;}
  const char* protocolName() const override{return "two_party";}
  uint64_t bytesSentTo(int p) const override{return p==1-pid_?evaluator_.sent()-sent_base_:0;}
  uint64_t bytesRecvFrom(int p) const override{return p==1-pid_?evaluator_.received()-recv_base_:0;}
  void resetCounters() override{sent_base_=evaluator_.sent();recv_base_=evaluator_.received();}
  void increaseSocketBuffers(int size) override{
    setsockopt(io_.consocket,SOL_SOCKET,SO_SNDBUF,&size,sizeof size);
    setsockopt(io_.consocket,SOL_SOCKET,SO_RCVBUF,&size,sizeof size);
  }
  void setInputs(const std::vector<wire_t>& w,const std::vector<Word>& v) override{
    if(prepared_ || w.size()!=v.size())throw std::logic_error("stage two-party inputs before offline");
    inputs_.push_back({w,v});
  }
  void setRandomInputs(const std::vector<wire_t>& w) override{setInputs(w,detail::randomRingVector<Word>(w.size()));}
  void offline(const LevelOrderedCircuit& c) override{
    evaluator_.prepare(c);
    for(const auto& input:inputs_)evaluator_.setInputs(input.first,input.second);
    inputs_.clear();prepared_=true;
  }
  void evalLevel(size_t i,const LevelOrderedCircuit& c) override{evaluator_.evalLevel(i,c);}
  void online(const LevelOrderedCircuit& c) override{for(size_t i=0;i<c.depth();++i)evalLevel(i,c);}
  std::vector<Word> getOutputs(const LevelOrderedCircuit& c) override{return evaluator_.outputs(c);}
};
#endif

template <typename T>
std::unique_ptr<IProtocolRunner<T>> makeProtocolRunner(const ProtocolConfig& cfg) {
  switch (cfg.kind) {
    case ProtocolKind::TwoParty:
      if(cfg.num_compute_parties!=2 || cfg.pid<0 || cfg.pid>1)
        throw std::invalid_argument("two_party requires exactly two parties with PID 0 or 1");
      if(cfg.pking || cfg.disable_optimized_shuffle)
        throw std::invalid_argument("NPH shuffle/reconstruction flags do not apply to two_party");
#ifdef CRIS_HAS_2PC
      if constexpr (std::is_same_v<T,std::uint64_t>)
        return std::make_unique<TwoPartyProtocolRunner>(cfg.pid,cfg.peer,cfg.port);
      else throw std::invalid_argument("two_party currently supports uint64 circuits");
#else
      throw std::invalid_argument("link Graphiti::graphiti with CRIS_BUILD_INIT2PC=ON for two_party");
#endif
    case ProtocolKind::Rss3:
      if (cfg.num_compute_parties != 3)
        throw std::invalid_argument("rss3 requires --num-parties 3");
      return std::make_unique<Rss3ProtocolRunner<T>>(cfg.pid, cfg.peer, cfg.port);

    case ProtocolKind::Nph:
      return std::make_unique<NphProtocolRunner<T>>(
          cfg.pid,
          cfg.num_compute_parties,
          cfg.peer,
          cfg.port,
          cfg.pking,
          cfg.disable_optimized_shuffle);
  }

  throw std::invalid_argument("Unsupported protocol kind");
}

}  // namespace threepc::protocol
