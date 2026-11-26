#pragma once

#include <cstdint>
#include <vector>

#include "luma/core/types.hpp"
#include "luma/execution/nexarm_protocol.hpp"
#include "luma/hal/transport.hpp"
#include "luma/safety/safety_guard.hpp"

namespace luma::execution {

// ============================================================================
// NexArmDriver：机械臂高层驱动（全系统唯一允许写硬件链路的“单出口”）
//
// 依赖关系（严格无环 DAG）：
//   - 注入 hal::ITransport&（USB 串口 / Wi-Fi TCP / Mock 单测无感切换）
//   - 内置 safety::SafetyGuard（任何 move* 指令发出前强制拦截，绝不绕过）
// ============================================================================
class NexArmDriver {
public:
    explicit NexArmDriver(hal::ITransport& transport,
                          safety::SafetyGuard guard = safety::SafetyGuard());

    // 作用：复位协议解析器并打开底层物理连接。
    core::Status connect();

    // 作用：关闭物理连接。
    void disconnect();

    // 作用：查询底层传输是否处于打开状态。
    bool isConnected() const;

    // 作用：只读查询——获取主板双芯片固件版本（零运动）。
    core::Status getFirmwareVersion(proto::FirmwareVersion& out, int timeout_ms = 1000);

    // 作用：只读查询——获取当前供电电压 mV（零运动）。
    core::Status getBatteryVoltageMv(uint16_t& out_mv, int timeout_ms = 1000);

    // 作用：只读查询——获取当前 6D 位姿与 6 舵机实时脉冲，填充至核心 DTO ArmStateMsg（零运动）。
    core::Status getCurrentState(core::ArmStateMsg& out, int timeout_ms = 1000);

    // 作用：安全移动至绝对笛卡尔目标位姿（必须先过 SafetyGuard::validatePose，违规 0 字节发出）。
    core::Status moveToPose(const core::ArmPose& target, uint16_t duration_ms);

    // 作用：安全移动单颗舵机至目标脉冲（必须先过 SafetyGuard::validateSingleServo，违规 0 字节发出）。
    core::Status moveSingleServo(uint8_t servo_id, int16_t pulse, uint16_t duration_ms);

    // 作用：开关舵机力矩（false=紧急卸力急停，true=重新锁止；急停指令不受运动限位阻塞）。
    core::Status setTorque(bool enable);

private:
    core::Status sendAndWaitReply(uint8_t cmd, const std::vector<uint8_t>& payload,
                                  proto::Frame& out_reply, int timeout_ms);

    hal::ITransport& transport_;
    safety::SafetyGuard guard_;
    proto::FrameParser parser_;
};

}  // namespace luma::execution
