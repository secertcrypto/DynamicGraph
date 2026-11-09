#pragma once
#include "risa/update.h"
#include <functional>

namespace risa {
namespace detail { struct ReadResources; }
class ReadState;
struct ReadInput;

// One context per compute party. Setup exchanges only public PIR material.
// Backend (and its IO) must outlive this context and its compiled states.
class ReadContext {
public:
    ReadContext(Backend&, Word size, unsigned value_bits=64);
    Word size() const;
    ReadContext(const ReadContext&)=delete;
    ReadContext& operator=(const ReadContext&)=delete;
private:
    std::shared_ptr<detail::ReadResources> resources_;
    friend ReadState CompileRead(ReadContext&,Word);
    friend std::vector<Word> ReadBatch(ReadContext&,Word,const std::vector<ReadInput>&);
};
inline ReadContext SetupRead(Backend& backend,Word size,unsigned value_bits=64) {return ReadContext(backend,size,value_bits);}

class ReadState {
public:
    ~ReadState();
    ReadState(ReadState&&) noexcept;
    ReadState& operator=(ReadState&&) noexcept;
    ReadState(const ReadState&)=delete;
    ReadState& operator=(const ReadState&)=delete;
    bool can_read(Word invocation) const;
private:
    struct Impl;
    explicit ReadState(std::unique_ptr<Impl>);
    std::unique_ptr<Impl> impl_;
    friend ReadState CompileRead(ReadContext&,Word);
    friend std::vector<Word> ReadBatch(ReadContext&,Word,const std::vector<ReadInput>&);
};
struct ReadInput {
    ReadState& state;
    const std::vector<Word>& array_share;
};

ReadState CompileRead(ReadContext&, Word canonical_index_share);
Word Read(ReadContext&, ReadState&, Word t, const std::vector<Word>& array_share);
// One local output word per state, in agreed public order; one framed response
// exchange per nonempty batch. Input arrays may alias the same vertex snapshot.
std::vector<Word> ReadBatch(ReadContext&, Word t, const std::vector<ReadInput>&);
}
