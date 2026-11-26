#pragma once

#include <cstdint>
#include <string>

#include "luma/hal/transport.hpp"

namespace luma::hal {

// TCP Socket 传输实现（用于 NexArm Wi-Fi 热点 192.168.4.1:8080 可行性验证与无线联调）
class TcpTransport final : public ITransport {
public:
    TcpTransport(std::string host, uint16_t port, int connect_timeout_ms = 2000);
    ~TcpTransport() override;

    TcpTransport(const TcpTransport&) = delete;
    TcpTransport& operator=(const TcpTransport&) = delete;

    core::Status open() override;
    void close() override;
    bool isOpen() const override { return fd_ >= 0; }
    core::Status writeAll(const uint8_t* data, std::size_t len) override;
    core::Status readSome(uint8_t* buf, std::size_t max_len, int timeout_ms,
                          std::size_t& out_read) override;

private:
    std::string host_;
    uint16_t port_;
    int connect_timeout_ms_;
    int fd_ = -1;
};

}  // namespace luma::hal
