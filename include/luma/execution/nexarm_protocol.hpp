#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "luma/core/types.hpp"

namespace luma::execution::proto {

// ============================================================================
// NexArm 出厂固件 V1.0.2 权威通信协议（已对照固件源码与官方 SDK 实核勘误）
// ⚠️ 严禁参照旧 PDF：PDF 漏算了 ID 字段，会导致校验和差 1！
// 帧格式：[0xFF] [0xFF] [ID] [LEN] [CMD] [PAYLOAD (LEN-2 字节)] [CHECKSUM]
// 校验和：~(ID + LEN + CMD + sum(PAYLOAD)) & 0xFF
// ============================================================================
constexpr uint8_t kHeader1 = 0xFF;
constexpr uint8_t kHeader2 = 0xFF;
constexpr uint8_t kSystemId = 0xFF;
constexpr uint8_t kAt32ReplyId = 0x5A;
constexpr uint8_t kMaxPayloadSize = 248;

enum Command : uint8_t {
    kCmdFirmwareVersion = 0x01,  // 查询固件版本（ESP32 + AT32）
    kCmdBatteryVoltage = 0x02,   // 查询供电电压（uint16 LE, mV）
    kCmdCoordinateSet = 0x08,    // 绝对坐标运动：<hhhhhhH (pitch*10, x, y, z, roll, claw, time_ms)
    kCmdBuzzerSet = 0x09,        // 蜂鸣器
    kCmdGetCurCoords = 0x0B,     // 读取当前 6D 位姿 + 6 舵机脉冲（24B）
    kCmdArmMoveInc = 0x32,       // 相对增量运动：<hhhhhhH (dx, dy, dz, dpitch*10, droll, dclaw, time_ms)
    kCmdArmServoSingle = 0x33,   // 单舵机脉冲控制：<BhH (servo_id, pulse, time_ms)
    kCmdSetTorque = 0x3D,        // 扭矩开关：0=卸力急停, 1=上电锁止
    kCmdArmHome = 0x43,          // 机械臂回零
};

// 解析完成后的单条合法协议帧
struct Frame {
    uint8_t id = kSystemId;
    uint8_t cmd = 0;
    std::vector<uint8_t> payload;
};

// 机械臂主板双芯片固件版本（ESP32 主控 + AT32 协处理器）
struct FirmwareVersion {
    uint8_t esp_major = 0;
    uint8_t esp_minor = 0;
    uint8_t esp_patch = 0;
    bool has_at32 = false;
    uint8_t at32_major = 0;
    uint8_t at32_minor = 0;
    uint8_t at32_patch = 0;
};

// 作用：计算 NexArm V1.0.2 协议校验和：~(id + length + cmd + sum(payload)) & 0xFF。
// 注意：必须把 id 算进累加和；length 等于 payload_len + 2。
uint8_t computeChecksum(uint8_t id, uint8_t length, uint8_t cmd, const uint8_t* payload,
                        std::size_t payload_len) noexcept;

// 作用：打包完整发送字节流：[0xFF, 0xFF, id, length, cmd, payload..., checksum]。
std::vector<uint8_t> encodeFrame(uint8_t id, uint8_t cmd, const uint8_t* payload,
                                 std::size_t payload_len);

// 便捷重载：直接传 std::vector<uint8_t> 载荷。
inline std::vector<uint8_t> encodeFrame(uint8_t id, uint8_t cmd,
                                        const std::vector<uint8_t>& payload = {}) {
    return encodeFrame(id, cmd, payload.data(), payload.size());
}

// ============================================================================
// FrameParser：逐字节有限状态机解析器（方案 B，O(N) 零 memmove，通吃 USB 串口与 TCP）
//
// 状态流转：Header1 -> Header2 -> Id -> Length -> Cmd -> (Payload) -> Checksum -> Header1
// ============================================================================
class FrameParser {
public:
    // 作用：复位状态机到 Header1 并清空半包缓存（连接重建时调用）。
    void reset() noexcept;

    // 作用：喂入底层 read() 返回的任意长度碎片字节流，将所有校验通过的完整帧追加进 out_frames。
    // 返回：本次 feed() 新解析出的合法帧个数。
    std::size_t feed(const uint8_t* data, std::size_t len, std::vector<Frame>& out_frames);

    // 作用：返回累计因校验和错误而丢弃的坏帧次数（链路健康监控指标）。
    uint64_t checksumErrors() const noexcept { return checksum_errors_; }

private:
    enum class State : uint8_t {
        Header1,
        Header2,
        Id,
        Length,
        Cmd,
        Payload,
        Checksum,
    };

    State state_ = State::Header1;
    uint8_t id_ = 0;
    uint8_t length_ = 0;
    uint8_t cmd_ = 0;
    std::vector<uint8_t> payload_;
    uint64_t checksum_errors_ = 0;
};

// 载荷小端编解码辅助函数（已按出厂 V1.0.2 固件实核实现）
std::vector<uint8_t> encodeCoordinateSetPayload(const core::ArmPose& pose, uint16_t duration_ms);
std::vector<uint8_t> encodeMoveIncPayload(const core::ArmPose& delta, uint16_t duration_ms);
core::Status decodeFirmwareVersion(const std::vector<uint8_t>& payload, FirmwareVersion& out);
core::Status decodeBatteryVoltageMv(const std::vector<uint8_t>& payload, uint16_t& out_mv);
core::Status decodeArmState(const std::vector<uint8_t>& payload, core::ArmStateMsg& out);

}  // namespace luma::execution::proto
