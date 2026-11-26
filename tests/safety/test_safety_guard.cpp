#include "luma/safety/safety_guard.hpp"
#include "test_framework.hpp"

#include <limits>

using namespace luma::core;
using namespace luma::safety;

LUMA_TEST(S01_ValidCenterAndNegativeYawPass) {
    SafetyGuard guard;

    ArmPose center{};
    center.x_mm = 200.0f;
    center.y_mm = 0.0f;
    center.z_mm = 180.0f;
    center.pitch_deg = -45.0f;
    center.roll_deg = 0.0f;
    center.claw_deg = -20.0f;
    LUMA_EXPECT_TRUE(guard.validatePose(center, 1000).ok());

    // Y > 0 侧对应 ID1 负角度（安全空旷侧），例如 (200, +200) -> -45° 必须放行
    ArmPose open_side = center;
    open_side.y_mm = 200.0f;
    LUMA_EXPECT_TRUE(guard.validatePose(open_side, 800).ok());

    // Y < 0 侧在 +25° 以内（未触碰 +28° 缓冲红线）：tan(25°) * 200 ≈ 93.2mm -> y = -90mm 安全
    ArmPose safe_neg_y = center;
    safe_neg_y.y_mm = -90.0f;
    LUMA_EXPECT_TRUE(guard.validatePose(safe_neg_y, 800).ok());
}

LUMA_TEST(S02_RejectBaseYawOver28DegWallRedline) {
    SafetyGuard guard;

    // (X=200, Y=-120) -> atan2(+120, 200) ≈ +30.96° > +28° 红线，必须强制拦截！
    ArmPose hit_wall{};
    hit_wall.x_mm = 200.0f;
    hit_wall.y_mm = -120.0f;
    hit_wall.z_mm = 180.0f;
    hit_wall.pitch_deg = -30.0f;

    const auto st = guard.validatePose(hit_wall, 1000);
    LUMA_EXPECT_FALSE(st.ok());
    LUMA_EXPECT_EQ(st.code, ErrorCode::kSafetyViolation);
}

LUMA_TEST(S03_RejectTableFloorAndNanAndFastSpeed) {
    SafetyGuard guard;

    ArmPose base{};
    base.x_mm = 200.0f;
    base.y_mm = 0.0f;
    base.z_mm = 180.0f;

    // 1. 砸桌红线：Z < 60mm 拦截
    ArmPose hit_table = base;
    hit_table.z_mm = 55.0f;
    LUMA_EXPECT_EQ(guard.validatePose(hit_table, 1000).code, ErrorCode::kSafetyViolation);

    // 2. NaN / Inf 污染拦截
    ArmPose nan_pose = base;
    nan_pose.x_mm = std::numeric_limits<float>::quiet_NaN();
    LUMA_EXPECT_EQ(guard.validatePose(nan_pose, 1000).code, ErrorCode::kSafetyViolation);

    ArmPose inf_pose = base;
    inf_pose.z_mm = std::numeric_limits<float>::infinity();
    LUMA_EXPECT_EQ(guard.validatePose(inf_pose, 1000).code, ErrorCode::kSafetyViolation);

    // 3. 限速防猛甩：duration_ms < 200ms 拦截
    LUMA_EXPECT_EQ(guard.validatePose(base, 50).code, ErrorCode::kSafetyViolation);
}

LUMA_TEST(S04_SingleServoId1PulseRedlineAndBounds) {
    SafetyGuard guard;

    // ID 1 脉冲 <= 2366 放行；> 2366（如 2389 = +30°）强制拦截！
    LUMA_EXPECT_TRUE(guard.validateSingleServo(1, 2048, 500).ok());
    LUMA_EXPECT_TRUE(guard.validateSingleServo(1, 2366, 500).ok());
    LUMA_EXPECT_EQ(guard.validateSingleServo(1, 2367, 500).code, ErrorCode::kSafetyViolation);
    LUMA_EXPECT_EQ(guard.validateSingleServo(1, 2389, 500).code, ErrorCode::kSafetyViolation);

    // 其他舵机（如 ID 2）在 [0, 4095] 内允许超过 2366，但越界仍拦
    LUMA_EXPECT_TRUE(guard.validateSingleServo(2, 3000, 500).ok());
    LUMA_EXPECT_EQ(guard.validateSingleServo(2, 4096, 500).code, ErrorCode::kSafetyViolation);
    LUMA_EXPECT_EQ(guard.validateSingleServo(2, -1, 500).code, ErrorCode::kSafetyViolation);

    // 非法舵机编号 [0, 7] 返回 kInvalidArg
    LUMA_EXPECT_EQ(guard.validateSingleServo(0, 2048, 500).code, ErrorCode::kInvalidArg);
    LUMA_EXPECT_EQ(guard.validateSingleServo(7, 2048, 500).code, ErrorCode::kInvalidArg);

    // 单舵机同样受限速保护
    LUMA_EXPECT_EQ(guard.validateSingleServo(1, 2048, 100).code, ErrorCode::kSafetyViolation);
}

LUMA_TEST_MAIN()
