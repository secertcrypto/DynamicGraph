#pragma once
#include <cstdint>
#include <cstddef>
#include <memory>
#include <vector>

namespace spiral_u64 {
// Opaque ciphertext messages. No plaintext index or client secret in Query.
// Only public conversion matrices cross the setup channel.
class PeerParameters {
    struct Impl;
    std::shared_ptr<const Impl> impl_;
    friend class Query;
public:
    static PeerParameters decode(uint64_t count, const std::vector<uint8_t>&);
};
class Query {
    struct Impl;std::shared_ptr<const Impl> impl_;
    friend class Client;friend class Server;
public: Query()=default;
    std::vector<uint8_t> encode() const;
    static Query decode(const PeerParameters&, const std::vector<uint8_t>&);
};
class Response {
    struct Impl;std::shared_ptr<const Impl> impl_;
    friend class Client;friend class Server;
public: Response()=default;
    std::vector<uint8_t> encode() const;
    static std::size_t wire_size();
    static Response decode(uint64_t count, const uint8_t* data, std::size_t length);
};
class Client {
    struct Impl;std::shared_ptr<Impl> impl_;
public:
    explicit Client(uint64_t database_size);
    std::vector<uint8_t> public_parameters() const;
    Query make_query(uint64_t index) const;
    // index stays client-side and chooses the uint64_t within the decrypted block.
    uint64_t recover(const Response&,uint64_t index) const;
};
class Server {
    struct Impl;std::shared_ptr<Impl> impl_;
public:
    explicit Server(const std::vector<uint64_t>& database);
    Response answer(const Query&) const;
    uint64_t size() const;
};
// Local end-to-end convenience helper. Server::answer never receives index.
uint64_t read(const Client&,const Server&,uint64_t index);
}
