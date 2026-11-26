#include "luma/execution/nexarm_driver.hpp"

#include <cstdint>

namespace luma::execution {

NexArmDriver::NexArmDriver(hal::ITransport& transport, safety::SafetyGuard guard)
    : transport_(transport), guard_(guard) {}

core::Status NexArmDriver::connect() {
    parser_.reset();
    return transport_.open();
}

void NexArmDriver::disconnect() { transport_.close(); }

bool NexArmDriver::isConnected() const { return transport_.isOpen(); }

core::Status NexArmDriver::sendAndWaitReply(uint8_t cmd, const std::vector<uint8_t>& payload,
                                            proto::Frame& out_reply, int timeout_ms) {
    if (!transport_.isOpen()) {
        return core::Status::Error(core::ErrorCode::kNotConnected, "Transport not open");
    }

    const auto tx = proto::encodeFrame(proto::kSystemId, cmd, payload);
    if (tx.empty()) {
        return core::Status::Error(core::ErrorCode::kProtocolError,
                                   "encodeFrame returned empty buffer");
    }

    if (auto st = transport_.writeAll(tx.data(), tx.size()); !st.ok()) {
        return st;
    }

    // 严格使用单调时钟 steadyNowMs() 计算超时截止时刻（CS-004）
    const core::SteadyMs deadline_ms = core::steadyNowMs() + static_cast<core::SteadyMs>(timeout_ms);
    uint8_t buf[256];
    std::vector<proto::Frame> frames;

    while (true) {
        const core::SteadyMs now_ms = core::steadyNowMs();
        if (now_ms >= deadline_ms) {
            return core::Status::Error(core::ErrorCode::kTimeout,
                                       "Timed out waiting for NexArm reply");
        }
        const int remain_ms = static_cast<int>(deadline_ms - now_ms);

        std::size_t n = 0;
        const auto st = transport_.readSome(buf, sizeof(buf), remain_ms > 0 ? remain_ms : 1, n);
        if (!st.ok()) {
            if (st.code == core::ErrorCode::kTimeout) {
                continue;
            }
            return st;
        }
        if (n == 0) {
            continue;
        }

        frames.clear();
        parser_.feed(buf, n, frames);
        for (const auto& f : frames) {
            const bool valid_id = (f.id == proto::kSystemId || f.id == proto::kAt32ReplyId);
            if (valid_id && f.cmd == cmd) {
                out_reply = f;
                return core::Status::Ok();
            }
        }
    }
}

core::Status NexArmDriver::getFirmwareVersion(proto::FirmwareVersion& out, int timeout_ms) {
    proto::Frame reply;
    if (auto st = sendAndWaitReply(proto::kCmdFirmwareVersion, {}, reply, timeout_ms); !st.ok()) {
        return st;
    }
    return proto::decodeFirmwareVersion(reply.payload, out);
}

core::Status NexArmDriver::getBatteryVoltageMv(uint16_t& out_mv, int timeout_ms) {
    proto::Frame reply;
    if (auto st = sendAndWaitReply(proto::kCmdBatteryVoltage, {}, reply, timeout_ms); !st.ok()) {
        return st;
    }
    return proto::decodeBatteryVoltageMv(reply.payload, out_mv);
}

core::Status NexArmDriver::getCurrentState(core::ArmStateMsg& out, int timeout_ms) {
    proto::Frame reply;
    if (auto st = sendAndWaitReply(proto::kCmdGetCurCoords, {}, reply, timeout_ms); !st.ok()) {
        return st;
    }
    return proto::decodeArmState(reply.payload, out);
}

core::Status NexArmDriver::moveToPose(const core::ArmPose& target, uint16_t duration_ms) {
    // 铁律：任何运动指令发出前必须强制通过 SafetyGuard 拦截（违规 0 字节发出）
    if (auto st = guard_.validatePose(target, duration_ms); !st.ok()) {
        return st;
    }
    if (!transport_.isOpen()) {
        return core::Status::Error(core::ErrorCode::kNotConnected, "Transport not open");
    }
    const auto payload = proto::encodeCoordinateSetPayload(target, duration_ms);
    const auto tx = proto::encodeFrame(proto::kSystemId, proto::kCmdCoordinateSet, payload);
    if (tx.empty()) {
        return core::Status::Error(core::ErrorCode::kProtocolError, "encodeFrame failed");
    }
    return transport_.writeAll(tx.data(), tx.size());
}

core::Status NexArmDriver::moveSingleServo(uint8_t servo_id, int16_t pulse, uint16_t duration_ms) {
    if (auto st = guard_.validateSingleServo(servo_id, pulse, duration_ms); !st.ok()) {
        return st;
    }
    if (!transport_.isOpen()) {
        return core::Status::Error(core::ErrorCode::kNotConnected, "Transport not open");
    }
    std::vector<uint8_t> payload(5);
    payload[0] = servo_id;
    payload[1] = static_cast<uint8_t>(static_cast<uint16_t>(pulse) & 0xFFU);
    payload[2] = static_cast<uint8_t>((static_cast<uint16_t>(pulse) >> 8U) & 0xFFU);
    payload[3] = static_cast<uint8_t>(duration_ms & 0xFFU);
    payload[4] = static_cast<uint8_t>((duration_ms >> 8U) & 0xFFU);

    const auto tx = proto::encodeFrame(proto::kSystemId, proto::kCmdArmServoSingle, payload);
    if (tx.empty()) {
        return core::Status::Error(core::ErrorCode::kProtocolError, "encodeFrame failed");
    }
    return transport_.writeAll(tx.data(), tx.size());
}

core::Status NexArmDriver::setTorque(bool enable) {
    if (!transport_.isOpen()) {
        return core::Status::Error(core::ErrorCode::kNotConnected, "Transport not open");
    }
    const std::vector<uint8_t> payload = {static_cast<uint8_t>(enable ? 1 : 0)};
    const auto tx = proto::encodeFrame(proto::kSystemId, proto::kCmdSetTorque, payload);
    if (tx.empty()) {
        return core::Status::Error(core::ErrorCode::kProtocolError, "encodeFrame failed");
    }
    return transport_.writeAll(tx.data(), tx.size());
}

}  // namespace luma::execution
