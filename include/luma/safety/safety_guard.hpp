#pragma once

#include <cstdint>

#include "luma/core/types.hpp"

namespace luma::safety {

// ============================================================================
// 物理防撞安全边界配置（全参数可配置，不写死魔法数字，后续可扩展 3D 视觉禁区）
// 实机标定基准：底座 ID1 中位 2048 (0°)，撞墙红线 +30° = 2389（对应 Y < 0 侧），
//              软件围栏默认留 2° 安全缓冲 -> +28°（脉冲 2366）。
// ============================================================================
struct SafetyLimits {
    // 底座 ID1 偏航限位（脉冲与角度双重约束）
    int16_t id1_pulse_min = 683;    // 约 -120°
    int16_t id1_pulse_max = 2366;   // +28°（物理红线 +30°=2389，留 2° 缓冲）
    float id1_max_deg = 28.0f;      // 最大允许正向角（deg），对应 Y < 0 侧
    float id1_min_deg = -115.0f;    // 最大允许负向角（deg），对应 Y > 0 侧

    // 笛卡尔工作包络（单位 mm / deg）
    float x_min_mm = 80.0f;
    float x_max_mm = 400.0f;
    float y_min_mm = -260.0f;       // 负侧还受 atan2(-Y, X) <= id1_max_deg 动态约束
    float y_max_mm = 400.0f;
    float z_min_mm = 60.0f;         // 桌面防撞底线（保护末端暖光灯不砸桌）
    float z_max_mm = 480.0f;

    float pitch_min_deg = -95.0f;
    float pitch_max_deg = 5.0f;
    float roll_min_deg = -60.0f;
    float roll_max_deg = 60.0f;
    float claw_min_deg = -60.0f;
    float claw_max_deg = 42.0f;

    // 限速底线：单次运动时长不得低于 200ms，防止非法短时间高速猛甩
    uint16_t min_duration_ms = 200;
};

// ============================================================================
// SafetyGuard：全系统唯一硬件物理安全安检门（零硬件依赖，100% 可离线单测）
//
// 架构角色：无论指令来自状态机、视觉伺服还是大模型推理，在落到串口/TCP 之前，
//          必须无条件通过 SafetyGuard 拦截校验。
// ============================================================================
class SafetyGuard {
public:
    explicit SafetyGuard(SafetyLimits limits = {}) noexcept : limits_(limits) {}

    // 作用：获取当前生效的安全边界配置（只读）。
    const SafetyLimits& limits() const noexcept { return limits_; }

    // 作用：校验绝对笛卡尔目标位姿与运动时长是否安全。
    // 返回：合法返回 Status::Ok()；违规返回 ErrorCode::kSafetyViolation 并附明具体超限原因。
    core::Status validatePose(const core::ArmPose& pose, uint16_t duration_ms) const;

    // 作用：校验单舵机目标脉冲是否安全（重点拦截 ID1 超出 [id1_pulse_min, id1_pulse_max]）。
    // 返回：servo_id 不在 [1,6] 返 kInvalidArg；越界返 kSafetyViolation；合法返 kOk。
    core::Status validateSingleServo(uint8_t servo_id, int16_t pulse, uint16_t duration_ms) const;

    // 作用：根据 (X, Y) 坐标计算底座 ID1 偏航角（单位：度）。
    // 实机标定方向：Y < 0 为正向 (+deg)，公式 atan2(-y_mm, x_mm) * 180 / pi。
    static float computeBaseYawDeg(float x_mm, float y_mm) noexcept;

private:
    SafetyLimits limits_;
};

}  // namespace luma::safety
