#pragma once

#include <cstddef>
#include <cstdint>

#include "luma/core/types.hpp"

namespace luma::hal {

// ============================================================================
// ITransport：硬件传输层纯虚接口（等价于 C 语言 PAL 里的 4 函数指针结构体）
//
// 作用：把底层物理链路差异彻底隔离在 hal 层——
//   - USB 串口实机部署：注入 PosixSerialTransport (/dev/ttyUSB0, 1Mbps)
//   - Wi-Fi 无线验证：注入 TcpTransport (192.168.4.1:8080)
//   - 离线单元测试：注入 MockTransport（纯内存收发，零真机依赖）
//   上层协议栈与驱动层只依赖本接口，切换链路 0 行代码改动。
// ============================================================================
class ITransport {
public:
    virtual ~ITransport() = default;

    // 作用：打开底层物理连接（幂等：已打开直接返回 kOk）。
    virtual core::Status open() = 0;

    // 作用：关闭连接并释放文件描述符（幂等）。
    virtual void close() = 0;

    // 作用：查询当前链路是否处于打开状态。
    virtual bool isOpen() const = 0;

    // 作用：循环写完 data[0..len-1] 全部字节，处理非阻塞 EAGAIN/EINTR；失败返 kTimeout / kIoError。
    virtual core::Status writeAll(const uint8_t* data, std::size_t len) = 0;

    // 作用：在 timeout_ms 内阻塞等待并读取最多 max_len 字节至 buf，实际读到的字节数写入 out_read。
    virtual core::Status readSome(uint8_t* buf, std::size_t max_len, int timeout_ms,
                                  std::size_t& out_read) = 0;
};

}  // namespace luma::hal
