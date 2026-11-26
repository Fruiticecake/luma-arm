#include "luma/safety/safety_guard.hpp"

#include <cmath>
#include <string>

namespace luma::safety {

namespace {
constexpr float kRadToDeg = 57.29577951308232f;
}  // namespace

// 工具函数（已写好）：根据基座坐标 (X, Y) 换算底座 ID1 偏航角（单位：度）。
// 实机标定方向：ID1 正向旋转 (+deg) 朝向 Y < 0 一侧，+30° 对应脉冲 2389。
float SafetyGuard::computeBaseYawDeg(float x_mm, float y_mm) noexcept {
    return std::atan2(-y_mm, x_mm) * kRadToDeg;
}

// 作用：绝对笛卡尔位姿安全安检门（防 NaN/Inf、防猛甩、防越界砸桌、防底座撞墙）
core::Status SafetyGuard::validatePose(const core::ArmPose& pose, uint16_t duration_ms) const {
    if (!std::isfinite(pose.x_mm) || !std::isfinite(pose.y_mm) || !std::isfinite(pose.z_mm) ||
        !std::isfinite(pose.pitch_deg) || !std::isfinite(pose.roll_deg) ||
        !std::isfinite(pose.claw_deg)) {
        return core::Status::Error(core::ErrorCode::kSafetyViolation, "NaN/Inf detected in pose");
    }
    if (duration_ms < limits_.min_duration_ms) {
        return core::Status::Error(core::ErrorCode::kSafetyViolation, "Duration too short");
    }
    if (pose.x_mm < limits_.x_min_mm || pose.x_mm > limits_.x_max_mm ||
        pose.y_mm < limits_.y_min_mm || pose.y_mm > limits_.y_max_mm ||
        pose.z_mm < limits_.z_min_mm || pose.z_mm > limits_.z_max_mm ||
        pose.pitch_deg < limits_.pitch_min_deg || pose.pitch_deg > limits_.pitch_max_deg ||
        pose.roll_deg < limits_.roll_min_deg || pose.roll_deg > limits_.roll_max_deg ||
        pose.claw_deg < limits_.claw_min_deg || pose.claw_deg > limits_.claw_max_deg) {
        return core::Status::Error(core::ErrorCode::kSafetyViolation, "Pose out of bounds");
    }
    const float yaw_deg = computeBaseYawDeg(pose.x_mm, pose.y_mm);
    if (yaw_deg < limits_.id1_min_deg || yaw_deg > limits_.id1_max_deg) {
        return core::Status::Error(core::ErrorCode::kSafetyViolation, "Yaw out of bounds");
    }
    return core::Status::Ok();
}

// 作用：单舵机脉冲安全安检门（编号校验、限速保护、通用脉冲边界与 ID1 底座专属硬锁）
core::Status SafetyGuard::validateSingleServo(uint8_t servo_id, int16_t pulse,
                                              uint16_t duration_ms) const {
    if (servo_id < 1 || servo_id > 6) {
        return core::Status::Error(core::ErrorCode::kInvalidArg, "Invalid servo ID");
    }
    if (duration_ms < limits_.min_duration_ms) {
        return core::Status::Error(core::ErrorCode::kSafetyViolation, "Duration too short");
    }
    if (pulse < 0 || pulse > 4095) {
        return core::Status::Error(core::ErrorCode::kSafetyViolation, "Pulse out of bounds");
    }
    if (servo_id == 1 && (pulse < limits_.id1_pulse_min || pulse > limits_.id1_pulse_max)) {
        return core::Status::Error(core::ErrorCode::kSafetyViolation, "Pulse out of bounds");
    }
    return core::Status::Ok();
}

}  // namespace luma::safety
