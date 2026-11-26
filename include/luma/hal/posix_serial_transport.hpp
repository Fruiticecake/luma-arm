#pragma once

#include <cstdint>
#include <string>

#include "luma/hal/transport.hpp"

namespace luma::hal {

// Linux / RK3588 / WSL POSIX 串口实现（默认 1,000,000 bps，Raw 模式，poll 超时控制）
class PosixSerialTransport final : public ITransport {
public:
    explicit PosixSerialTransport(std::string device_path, uint32_t baudrate = 1000000);
    ~PosixSerialTransport() override;

    PosixSerialTransport(const PosixSerialTransport&) = delete;
    PosixSerialTransport& operator=(const PosixSerialTransport&) = delete;

    core::Status open() override;
    void close() override;
    bool isOpen() const override { return fd_ >= 0; }
    core::Status writeAll(const uint8_t* data, std::size_t len) override;
    core::Status readSome(uint8_t* buf, std::size_t max_len, int timeout_ms,
                          std::size_t& out_read) override;

private:
    std::string device_path_;
    uint32_t baudrate_;
    int fd_ = -1;
};

}  // namespace luma::hal
