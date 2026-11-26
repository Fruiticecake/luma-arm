#include <iostream>
#include <memory>
#include <string>

#include "luma/execution/nexarm_driver.hpp"
#include "luma/hal/posix_serial_transport.hpp"
#include "luma/hal/tcp_transport.hpp"
#include "luma/safety/safety_guard.hpp"

// 零运动只读实机握手工具：仅查询固件版本、供电电压与当前 6 轴位姿，绝不下发任何运动指令
int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "Usage:\n"
                  << "  " << argv[0] << " --serial <device> [baud=1000000]\n"
                  << "  " << argv[0] << " --tcp <host> [port=8080]\n";
        return 2;
    }

    std::unique_ptr<luma::hal::ITransport> transport;
    const std::string mode = argv[1];
    if (mode == "--serial") {
        const uint32_t baud =
            (argc >= 4) ? static_cast<uint32_t>(std::stoul(argv[3])) : 1000000U;
        transport = std::make_unique<luma::hal::PosixSerialTransport>(argv[2], baud);
    } else if (mode == "--tcp") {
        const uint16_t port = (argc >= 4) ? static_cast<uint16_t>(std::stoul(argv[3])) : 8080U;
        transport = std::make_unique<luma::hal::TcpTransport>(argv[2], port);
    } else {
        std::cerr << "Unknown mode: " << mode << "\n";
        return 2;
    }

    luma::execution::NexArmDriver driver(*transport);
    if (auto st = driver.connect(); !st.ok()) {
        std::cerr << "[FAIL] connect: " << st.message << "\n";
        return 1;
    }

    luma::execution::proto::FirmwareVersion ver{};
    if (auto st = driver.getFirmwareVersion(ver, 1000); st.ok()) {
        std::cout << "[OK] Firmware: ESP32=" << static_cast<int>(ver.esp_major) << "."
                  << static_cast<int>(ver.esp_minor) << "." << static_cast<int>(ver.esp_patch);
        if (ver.has_at32) {
            std::cout << ", AT32=" << static_cast<int>(ver.at32_major) << "."
                      << static_cast<int>(ver.at32_minor) << "."
                      << static_cast<int>(ver.at32_patch);
        }
        std::cout << "\n";
    } else {
        std::cerr << "[WARN] getFirmwareVersion: " << st.message << "\n";
    }

    uint16_t mv = 0;
    if (auto st = driver.getBatteryVoltageMv(mv, 1000); st.ok()) {
        std::cout << "[OK] Supply Voltage: " << mv << " mV (" << (static_cast<double>(mv) / 1000.0)
                  << " V)\n";
    } else {
        std::cerr << "[WARN] getBatteryVoltageMv: " << st.message << "\n";
    }

    luma::core::ArmStateMsg state{};
    if (auto st = driver.getCurrentState(state, 1000); st.ok()) {
        const float yaw = luma::safety::SafetyGuard::computeBaseYawDeg(state.gripper_pose.x_mm,
                                                                       state.gripper_pose.y_mm);
        std::cout << "[OK] Pose: X=" << state.gripper_pose.x_mm
                  << " Y=" << state.gripper_pose.y_mm << " Z=" << state.gripper_pose.z_mm
                  << " Pitch=" << state.gripper_pose.pitch_deg
                  << " Roll=" << state.gripper_pose.roll_deg
                  << " Claw=" << state.gripper_pose.claw_deg << " (BaseYaw=" << yaw << " deg)\n";
        std::cout << "[OK] Servos(1..6):";
        for (int i = 0; i < 6; ++i) {
            std::cout << " ID" << (i + 1) << "=" << state.servo_pulses[i];
        }
        std::cout << "\n";
    } else {
        std::cerr << "[WARN] getCurrentState: " << st.message << "\n";
    }

    driver.disconnect();
    return 0;
}
