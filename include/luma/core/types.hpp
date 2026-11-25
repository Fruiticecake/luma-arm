#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <type_traits>
#include <utility>

namespace luma::core {

// ----------------------------------------------------------------------------
// 1. 统一错误码、结果状态与单调时钟
// ----------------------------------------------------------------------------
enum class ErrorCode : uint8_t {
    kOk = 0,
    kInvalidArg = 1,
    kSafetyViolation = 2,
    kIoError = 3,
    kTimeout = 4,
    kChecksumError = 5,
    kProtocolError = 6,
    kNotConnected = 7,
    kQueueFull = 8,
    kStaleFrame = 9,       // 共享内存帧被覆写或读取期间发生撕裂
    kAlreadyExists = 10,
    kNotFound = 11,
    kBusy = 12,
    kNotSupported = 13,
    kHardwareFault = 14,
    kCancelled = 15,
};

struct Status {
    ErrorCode code = ErrorCode::kOk;
    std::string message;

    bool ok() const noexcept { return code == ErrorCode::kOk; }
    static Status Ok() { return {ErrorCode::kOk, ""}; }
    static Status Error(ErrorCode c, std::string msg = {}) { return {c, std::move(msg)}; }
};

using SteadyMs = int64_t;
SteadyMs steadyNowMs() noexcept;          // 基于 std::chrono::steady_clock，抗 NTP 校时跳变
const char* errorCodeName(ErrorCode c) noexcept;

// ----------------------------------------------------------------------------
// 2. 全系统公共标识枚举
// ----------------------------------------------------------------------------
enum class ProcessId : uint8_t {
    kUnknown = 0,
    kVisiond = 1,
    kBraind = 2,
    kMotiond = 3,
    kInProcAll = 4,
};

enum class ProcessState : uint8_t {
    kInit = 0,
    kRunning = 1,
    kDegraded = 2,
    kStopping = 3,
    kFault = 4,
};

enum class TcpType : uint8_t {
    kGripper = 0,          // 末端平行夹爪中心 (NexArm 固件默认坐标系)
    kLamp = 1,             // V1 暖光伴读灯头出光点
    kWristCamera = 2,      // link4 腕部相机光心 (+64.56, -19.41, +36.50 mm)
};

enum class PixelFormat : uint8_t {
    kNv12 = 1,
    kRgb888 = 2,
    kBgr888 = 3,
    kJpeg = 4,
};

enum class TopicId : uint8_t {
    kPerceptionSceneState = 1,   // visiond -> braind  (5-10Hz,  Drop-Oldest)
    kPerceptionBookOffset = 2,   // visiond -> motiond (20Hz,    Drop-Oldest 视觉伺服快环)
    kPerceptionHeadPose = 3,     // visiond -> motiond (20Hz,    Drop-Oldest 快环防撞客观事实)
    kMotionSkillCmd = 10,        // braind  -> motiond (按需,     QueueFull 拒绝覆盖)
    kMotionArmState = 11,        // motiond -> braind  (10-20Hz, Drop-Oldest 实时状态反馈)
    kMotionCmdAck = 12,          // motiond -> braind  (事件触发, QueueFull 拒绝覆盖)
    kSafetyEvent = 20,           // motiond -> braind  (事件触发, QueueFull 拒绝覆盖)
    kSysHeartbeat = 30,          // braind/visiond -> motiond (10Hz, Drop-Oldest)
};

constexpr bool isDropOldestTopic(TopicId t) noexcept {
    return t == TopicId::kPerceptionSceneState ||
           t == TopicId::kPerceptionBookOffset ||
           t == TopicId::kPerceptionHeadPose ||
           t == TopicId::kMotionArmState ||
           t == TopicId::kSysHeartbeat;
}

// ----------------------------------------------------------------------------
// 3. 全系统冻结跨进程 POD DTO（全部强校验 trivially_copyable）
// ----------------------------------------------------------------------------
struct FrameRef {
    uint32_t slot_id = 0;        // ShmFramePool 槽位索引 [0, slot_count - 1]
    uint32_t seq = 0;            // 写入完成时的偶数版本号
    SteadyMs timestamp_ms = 0;   // 采集时刻单调毫秒时间戳
};
static_assert(sizeof(FrameRef) == 16 && std::is_trivially_copyable_v<FrameRef>);

struct Point3f {
    float x_mm = 0.0f;
    float y_mm = 0.0f;
    float z_mm = 0.0f;
    uint8_t valid = 0;
    uint8_t reserved[3] = {0};
};
static_assert(sizeof(Point3f) == 16 && std::is_trivially_copyable_v<Point3f>);

// 统一位姿表示（基座坐标系，单位 mm / deg，与 NexArm 固件单位一致）
struct ArmPose {
    float x_mm = 200.0f;
    float y_mm = 0.0f;
    float z_mm = 200.0f;
    float pitch_deg = 0.0f;      // -90°=垂直向下, 0°=水平前指
    float roll_deg = 0.0f;
    float claw_deg = 0.0f;       // -50°=常用张开, 0°=闭合
};
static_assert(sizeof(ArmPose) == 24 && std::is_trivially_copyable_v<ArmPose>);

// Topic: kMotionArmState (motiond -> braind)
struct ArmStateMsg {
    SteadyMs timestamp_ms = 0;
    ArmPose gripper_pose;        // 实测夹爪 TCP 位姿
    ArmPose lamp_pose;           // 换算后的灯头 TCP 位姿
    int16_t servo_pulses[6] = {2048, 2048, 2048, 2048, 2048, 2048};
    int16_t servo_temp_c[6] = {0, 0, 0, 0, 0, 0};
    uint16_t supply_mv = 0;      // 供电电压 mV（<10500 触发低电告警）
    uint8_t is_moving = 0;
    uint8_t link_ok = 0;
    uint8_t light_percent = 0;   // 当前暖光灯 PWM 百分比 [0..100]
    uint8_t privacy_closed = 0;  // SG90 隐私遮蔽盖状态（1=已遮蔽）
    uint8_t reserved[2] = {0};
};
static_assert(std::is_trivially_copyable_v<ArmStateMsg>);

// Topic: kPerceptionSceneState (visiond -> braind，仅包含客观感知事实，零安全策略)
struct SceneStateMsg {
    SteadyMs timestamp_ms = 0;
    FrameRef keyframe;           // 指向 ShmFramePool 的零拷贝最新帧
    float ambient_lux = -1.0f;   // 实测照度（<0 表示未就绪）
    Point3f person_head_3d;      // 基座坐标系下人头 3D 坐标
    Point3f book_center_3d;      // 基座坐标系下书本中心 3D 坐标
    Point3f phone_center_3d;     // 基座坐标系下手机 3D 坐标
    float neck_tilt_deg = 0.0f;  // 场景 3 颈肩前倾客观测量角（deg）
    float doa_angle_deg = 0.0f;  // 声源方位角（基座坐标系）
    uint8_t has_person = 0;
    uint8_t has_book = 0;
    uint8_t has_phone = 0;
    uint8_t wake_triggered = 0;
    uint8_t reading_debounced = 0;      // 3s 时间窗防抖后的人+书状态
    uint8_t posture_bad_debounced = 0;  // 60s 时间窗防抖后的不良坐姿状态
    uint8_t camera_id = 0;       // 0=全局相机, 1=腕部 USB 相机
    uint8_t reserved = 0;
};
static_assert(std::is_trivially_copyable_v<SceneStateMsg>);

// Topic: kPerceptionBookOffset (visiond -> motiond，20Hz 视觉伺服快环直通)
struct BookOffsetMsg {
    SteadyMs timestamp_ms = 0;
    uint32_t seq = 0;
    float dx_norm = 0.0f;        // 书本中心相对图像光心归一化偏差 [-1.0, +1.0]
    float dy_norm = 0.0f;
    float area_ratio = 0.0f;
    Point3f book_center_3d;
    float confidence = 0.0f;
    uint8_t valid = 0;
    uint8_t camera_id = 1;
    uint8_t reserved[2] = {0};
};
static_assert(std::is_trivially_copyable_v<BookOffsetMsg>);

// Topic: kPerceptionHeadPose (visiond -> motiond，20Hz 头部防撞客观事实)
struct HeadPoseMsg {
    SteadyMs timestamp_ms = 0;
    uint32_t seq = 0;
    Point3f head_center_3d;
    float head_radius_mm = 110.0f;
    float gaze_pitch_deg = 0.0f;
    float confidence = 0.0f;
    uint8_t valid = 0;
    uint8_t reserved[3] = {0};
};
static_assert(std::is_trivially_copyable_v<HeadPoseMsg>);

// Topic: kMotionSkillCmd (braind -> motiond，高层技能执行指令)
enum class SkillAction : uint8_t {
    kStop = 0,
    kHome = 1,
    kMovePose = 2,
    kLookAtYaw = 3,
    kTrackBookStart = 4,
    kTrackBookStop = 5,
    kSetLightPwm = 6,
    kSetPrivacyCover = 7,
    kGentleNudge = 8,
    kSpeakText = 9,
    kEmergencyTorqueOff = 10,
};

struct SkillCmdMsg {
    uint32_t cmd_seq = 0;        // 严格单调递增命令号（去重与 Ack 匹配）
    SteadyMs issued_ms = 0;      // 发令时刻（steady_clock）
    uint32_t ttl_ms = 2000;      // 命令有效期，超时 motiond 拒执行并回 Expired
    SkillAction action = SkillAction::kStop;
    TcpType tcp_type = TcpType::kLamp;
    uint8_t light_percent = 0;   // [0..100]
    uint8_t privacy_close = 0;   // 1=合盖遮蔽, 0=开盖
    ArmPose target_pose;
    uint16_t duration_ms = 1000;
    uint16_t light_fade_ms = 800;
    float target_yaw_deg = 0.0f; // 供 kLookAtYaw 使用（仍受 ID1<=+28° 约束）
    uint8_t nudge_level = 1;
    uint8_t reserved[3] = {0};
    char tts_text[128] = {0};    // UTF-8 以 '\0' 结尾
};
static_assert(sizeof(SkillCmdMsg) <= 256 && std::is_trivially_copyable_v<SkillCmdMsg>);

// Topic: kMotionCmdAck (motiond -> braind)
enum class CmdAckState : uint8_t {
    kAccepted = 0,
    kCompleted = 1,
    kRejected = 2,
    kCancelled = 3,
    kExpired = 4,
};

enum class SafetyViolationCode : uint8_t {
    kNone = 0,
    kBaseYawLimit = 1,           // ID1 > +28° (脉冲 > 2366) 或 Y < -X*tan(28°)
    kWorkspaceX = 2,
    kWorkspaceY = 3,
    kWorkspaceZFloor = 4,        // Z < 60mm 桌面防撞底线
    kWorkspaceZCeil = 5,
    kOrientationLimit = 6,
    kHeadExclusionZone = 7,      // 侵入头部防撞球
    kGlareConeViolation = 8,     // 灯头直射人眼防眩光禁区
    kSpeedOrDurationTooFast = 9,
    kHeartbeatTimeout = 10,      // braind/visiond >500ms 心跳超时
    kLowVoltage = 11,
    kServoOverTemp = 12,
    kLinkLost = 13,
    kEmergencyStop = 14,
    kIkUnreachable = 15,
};

struct SkillCmdAckMsg {
    uint32_t cmd_seq = 0;
    SteadyMs timestamp_ms = 0;
    ErrorCode result = ErrorCode::kOk;
    SafetyViolationCode safety_code = SafetyViolationCode::kNone;
    CmdAckState state = CmdAckState::kAccepted;
    uint8_t reserved = 0;
};
static_assert(std::is_trivially_copyable_v<SkillCmdAckMsg>);

// Topic: kSafetyEvent (motiond -> braind)
enum class SafetyLayer : uint8_t {
    kCognitionHarness = 1,
    kSkillsPrecheck = 2,
    kExecutionFinalGate = 3,
    kWatchdog = 4,
};

struct SafetyEventMsg {
    SteadyMs timestamp_ms = 0;
    SafetyViolationCode code = SafetyViolationCode::kNone;
    uint8_t severity = 1;        // 1=Warn(单次拒令), 2=Critical(熔断停机), 3=Emergency(卸力)
    SafetyLayer source_layer = SafetyLayer::kExecutionFinalGate;
    uint8_t blackbox_dumped = 0;
    uint32_t related_cmd_seq = 0;
    float measured_value = 0.0f;
    float limit_value = 0.0f;
    char detail[96] = {0};
};
static_assert(std::is_trivially_copyable_v<SafetyEventMsg>);

// Topic: kSysHeartbeat (braind / visiond -> motiond，10Hz 保活)
struct HeartbeatMsg {
    ProcessId process_id = ProcessId::kUnknown;
    ProcessState state = ProcessState::kInit;
    uint16_t reserved = 0;
    uint32_t seq = 0;
    SteadyMs timestamp_ms = 0;
};
static_assert(std::is_trivially_copyable_v<HeartbeatMsg>);

}  // namespace luma::core
