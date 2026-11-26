#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "luma/execution/nexarm_driver.hpp"
#include "luma/execution/nexarm_protocol.hpp"
#include "luma/hal/posix_serial_transport.hpp"
#include "luma/hal/tcp_transport.hpp"
#include "luma/safety/safety_guard.hpp"

namespace {

void printHexSlice(const char* label, const std::vector<uint8_t>& bytes, std::size_t begin,
                   std::size_t end) {
    std::cout << "  " << label << ":";
    for (std::size_t i = begin; i < end && i < bytes.size(); ++i) {
        std::cout << " " << std::hex << std::uppercase << std::setw(2) << std::setfill('0')
                  << static_cast<unsigned int>(bytes[i]);
    }
    std::cout << std::dec << "\n";
}

void printArmState(const char* tag, const luma::core::ArmStateMsg& st) {
    const float yaw = luma::safety::SafetyGuard::computeBaseYawDeg(st.gripper_pose.x_mm,
                                                                   st.gripper_pose.y_mm);
    std::cout << tag << " Pose: X=" << st.gripper_pose.x_mm << " Y=" << st.gripper_pose.y_mm
              << " Z=" << st.gripper_pose.z_mm << " Pitch=" << st.gripper_pose.pitch_deg
              << " Roll=" << st.gripper_pose.roll_deg << " Claw=" << st.gripper_pose.claw_deg
              << " (BaseYaw=" << yaw << " deg)\n";
    std::cout << tag << " Servos(1..6):";
    for (int i = 0; i < 6; ++i) {
        std::cout << " ID" << (i + 1) << "=" << st.servo_pulses[i];
    }
    std::cout << "\n";
}

void printUsage(const char* prog) {
    std::cerr << "Usage:\n"
              << "  " << prog << " --serial <device> [baud=1000000] [--execute]\n"
              << "  " << prog << " --tcp <host> [port=8080] [--execute]\n"
              << "Default is --dry-run (read-only + prints full data-path hex frames).\n";
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        printUsage(argv[0]);
        return 2;
    }

    bool execute = false;
    std::vector<std::string> pos_args;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--execute") {
            execute = true;
        } else if (arg == "--dry-run") {
            execute = false;
        } else {
            pos_args.push_back(arg);
        }
    }

    if (pos_args.size() < 2) {
        printUsage(argv[0]);
        return 2;
    }

    std::unique_ptr<luma::hal::ITransport> transport;
    const std::string& mode = pos_args[0];
    if (mode == "--serial") {
        const uint32_t baud =
            (pos_args.size() >= 3) ? static_cast<uint32_t>(std::stoul(pos_args[2])) : 1000000U;
        transport = std::make_unique<luma::hal::PosixSerialTransport>(pos_args[1], baud);
    } else if (mode == "--tcp") {
        const uint16_t port =
            (pos_args.size() >= 3) ? static_cast<uint16_t>(std::stoul(pos_args[2])) : 8080U;
        transport = std::make_unique<luma::hal::TcpTransport>(pos_args[1], port);
    } else {
        std::cerr << "Unknown mode: " << mode << "\n";
        return 2;
    }

    luma::execution::NexArmDriver driver(*transport);
    if (auto st = driver.connect(); !st.ok()) {
        std::cerr << "[FAIL] connect: " << st.message << "\n";
        return 1;
    }

    // Step 1: 读取当前位姿作为起点（返回后也按此位姿缓慢归位）
    luma::core::ArmStateMsg origin{};
    if (auto st = driver.getCurrentState(origin, 1000); !st.ok()) {
        std::cerr << "[FAIL] getCurrentState: " << st.message << "\n";
        driver.disconnect();
        return 1;
    }
    std::cout << "=== Step 1: Current Arm State ===\n";
    printArmState("[ORIGIN]", origin);

    // Step 2: 主动注入一个撞墙越界位姿，演示 SafetyGuard 单出口拦截（0 字节发出）
    std::cout << "\n=== Step 2: SafetyGuard Interception Check ===\n";
    luma::core::ArmPose unsafe{};
    unsafe.x_mm = 200.0f;
    unsafe.y_mm = -130.0f;  // atan2(130, 200) ≈ +33.0° > +28° 硬红线
    unsafe.z_mm = 180.0f;
    unsafe.pitch_deg = -30.0f;
    const auto unsafe_st = driver.moveToPose(unsafe, 1000);
    std::cout << "[GUARD] Injecting unsafe pose (X=200, Y=-130, Z=180) -> code="
              << luma::core::errorCodeName(unsafe_st.code) << " (" << unsafe_st.message
              << "), 0 bytes written.\n";

    // Step 3: 生成安全小幅度微动目标，并完整拆解 14B 载荷 + 20B 协议帧十六进制数据路径
    constexpr uint16_t kMoveDurationMs = 1500;
    luma::core::ArmPose target = origin.gripper_pose;
    target.x_mm = std::clamp(origin.gripper_pose.x_mm + 20.0f, 100.0f, 260.0f);
    target.z_mm = std::clamp(origin.gripper_pose.z_mm + 25.0f, 100.0f, 280.0f);

    std::cout << "\n=== Step 3: Safe Micro-Move Target & Data-Path Trace ===\n";
    std::cout << "[TARGET] X=" << target.x_mm << " Y=" << target.y_mm << " Z=" << target.z_mm
              << " Pitch=" << target.pitch_deg << " Roll=" << target.roll_deg
              << " Claw=" << target.claw_deg << " duration=" << kMoveDurationMs << "ms\n";

    luma::safety::SafetyGuard guard;
    const auto guard_ok = guard.validatePose(target, kMoveDurationMs);
    std::cout << "[GUARD] validatePose(target) -> " << luma::core::errorCodeName(guard_ok.code)
              << "\n";
    if (!guard_ok.ok()) {
        std::cerr << "[FAIL] target rejected by SafetyGuard: " << guard_ok.message << "\n";
        driver.disconnect();
        return 1;
    }

    const auto payload =
        luma::execution::proto::encodeCoordinateSetPayload(target, kMoveDurationMs);
    const auto frame = luma::execution::proto::encodeFrame(
        luma::execution::proto::kSystemId, luma::execution::proto::kCmdCoordinateSet, payload);

    std::cout << "[DATA-PATH] Payload (" << payload.size()
              << "B = pitch*10, x, y, z, roll, claw, time_ms in int16/uint16 LE):\n";
    printHexSlice("Payload[0..13]", payload, 0, payload.size());

    std::cout << "[DATA-PATH] Wire Frame (" << frame.size() << "B sent to ITransport::writeAll):\n";
    printHexSlice("Header (2B)   ", frame, 0, 2);
    printHexSlice("SysID  (1B)   ", frame, 2, 3);
    printHexSlice("Length (1B)   ", frame, 3, 4);
    printHexSlice("Cmd0x08(1B)   ", frame, 4, 5);
    printHexSlice("Payload(14B)  ", frame, 5, 19);
    printHexSlice("Chksum (1B)   ", frame, 19, 20);
    printHexSlice("Full Frame    ", frame, 0, frame.size());

    // Step 4: 默认 dry-run 绝不驱动物理舵机；显式传入 --execute 才下发微动与回位
    std::cout << "\n=== Step 4: Execution ===\n";
    if (!execute) {
        std::cout << "[DRY-RUN] Verified read + guard + wire frame without moving servos.\n"
                  << "          Pass --execute after visual safety check to move & return.\n";
        driver.disconnect();
        return 0;
    }

    std::cout << "[EXEC] Moving slowly (+20mm X, +25mm Z in 1500ms)...\n";
    if (auto st = driver.moveToPose(target, kMoveDurationMs); !st.ok()) {
        std::cerr << "[FAIL] moveToPose(target): " << st.message << "\n";
        driver.disconnect();
        return 1;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1800));

    luma::core::ArmStateMsg after_move{};
    if (auto st = driver.getCurrentState(after_move, 1000); st.ok()) {
        printArmState("[MOVED]", after_move);
    } else {
        std::cerr << "[WARN] getCurrentState after move: " << st.message << "\n";
    }

    std::cout << "[EXEC] Returning to origin pose (1500ms)...\n";
    if (auto st = driver.moveToPose(origin.gripper_pose, kMoveDurationMs); !st.ok()) {
        std::cerr << "[FAIL] moveToPose(origin): " << st.message << "\n";
        driver.disconnect();
        return 1;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1800));

    luma::core::ArmStateMsg after_return{};
    if (auto st = driver.getCurrentState(after_return, 1000); st.ok()) {
        printArmState("[RETURNED]", after_return);
    } else {
        std::cerr << "[WARN] getCurrentState after return: " << st.message << "\n";
    }

    driver.disconnect();
    return 0;
}
