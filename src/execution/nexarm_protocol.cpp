#include "luma/execution/nexarm_protocol.hpp"

#include <cmath>
#include <cstdint>

namespace luma::execution::proto {

namespace {

void appendI16LE(std::vector<uint8_t>& out, int16_t v) {
    const auto u = static_cast<uint16_t>(v);
    out.push_back(static_cast<uint8_t>(u & 0xFFU));
    out.push_back(static_cast<uint8_t>((u >> 8U) & 0xFFU));
}

void appendU16LE(std::vector<uint8_t>& out, uint16_t v) {
    out.push_back(static_cast<uint8_t>(v & 0xFFU));
    out.push_back(static_cast<uint8_t>((v >> 8U) & 0xFFU));
}

int16_t readI16LE(const uint8_t* p) noexcept {
    const auto u = static_cast<uint16_t>(static_cast<uint16_t>(p[0]) |
                                         static_cast<uint16_t>(static_cast<uint16_t>(p[1]) << 8U));
    return static_cast<int16_t>(u);
}

uint16_t readU16LE(const uint8_t* p) noexcept {
    return static_cast<uint16_t>(static_cast<uint16_t>(p[0]) |
                                 static_cast<uint16_t>(static_cast<uint16_t>(p[1]) << 8U));
}

}  // namespace

// 作用：计算 NexArm V1.0.2 协议校验和：~(id + length + cmd + sum(payload)) & 0xFF。
uint8_t computeChecksum(uint8_t id, uint8_t length, uint8_t cmd, const uint8_t* payload,
                        std::size_t payload_len) noexcept {
    uint32_t sum = static_cast<uint32_t>(id) + length + cmd;
    if (payload != nullptr) {
        for (std::size_t i = 0; i < payload_len; ++i) {
            sum += payload[i];
        }
    }
    return static_cast<uint8_t>((~sum) & 0xFFU);
}

// 作用：组装完整二进制发送帧：[0xFF, 0xFF, id, length, cmd, payload..., checksum]。
// 注意：若 payload_len > kMaxPayloadSize，返回空 vector 拒绝打包。
std::vector<uint8_t> encodeFrame(uint8_t id, uint8_t cmd, const uint8_t* payload,
                                 std::size_t payload_len) {
    if (payload_len > kMaxPayloadSize || (payload_len > 0 && payload == nullptr)) {
        return {};
    }
    const uint8_t length = static_cast<uint8_t>(payload_len + 2);
    std::vector<uint8_t> out;
    out.reserve(payload_len + 6);
    out.push_back(kHeader1);
    out.push_back(kHeader2);
    out.push_back(id);
    out.push_back(length);
    out.push_back(cmd);
    if (payload_len > 0) {
        out.insert(out.end(), payload, payload + payload_len);
    }
    out.push_back(computeChecksum(id, length, cmd, payload, payload_len));
    return out;
}

// 作用：复位逐字节状态机内部缓存（连接断开或重连前调用）。
void FrameParser::reset() noexcept {
    state_ = State::Header1;
    id_ = 0;
    length_ = 0;
    cmd_ = 0;
    payload_.clear();
}

// 作用：逐字节有限状态机解析器（O(N) 零 memmove，处理半包、粘包、脏字节与连续 0xFF 帧头）。
std::size_t FrameParser::feed(const uint8_t* data, std::size_t len,
                              std::vector<Frame>& out_frames) {
    if (data == nullptr || len == 0) {
        return 0;
    }
    std::size_t parsed = 0;
    for (std::size_t i = 0; i < len; ++i) {
        switch (state_) {
            case State::Header1:
                if (data[i] == kHeader1) {
                    state_ = State::Header2;
                }
                break;
            case State::Header2:
                if (data[i] == kHeader2) {
                    state_ = State::Id;
                } else {
                    state_ = State::Header1;
                }
                break;
            case State::Id:
                id_ = data[i];
                state_ = State::Length;
                break;
            case State::Length:
                length_ = data[i];
                if (length_ < 2 || length_ > kMaxPayloadSize + 2) {
                    state_ = State::Header1;
                } else {
                    payload_.clear();
                    state_ = State::Cmd;
                }
                break;
            case State::Cmd:
                cmd_ = data[i];
                if (length_ == 2) {
                    state_ = State::Checksum;
                } else {
                    payload_.reserve(static_cast<std::size_t>(length_ - 2));
                    state_ = State::Payload;
                }
                break;
            case State::Payload:
                payload_.push_back(data[i]);
                if (payload_.size() == static_cast<std::size_t>(length_ - 2)) {
                    state_ = State::Checksum;
                }
                break;
            case State::Checksum:
                if (data[i] ==
                    computeChecksum(id_, length_, cmd_, payload_.data(), payload_.size())) {
                    out_frames.push_back(Frame{id_, cmd_, std::move(payload_)});
                    ++parsed;
                    state_ = State::Header1;
                } else {
                    state_ = State::Header1;
                    ++checksum_errors_;
                }
                break;
            default:
                state_ = State::Header1;
                ++checksum_errors_;
                break;
        }
    }
    return parsed;
}

// ----------------------------------------------------------------------------
// 以下载荷小端编解码已按出厂固件 V1.0.2 实核写好
// ----------------------------------------------------------------------------
std::vector<uint8_t> encodeCoordinateSetPayload(const core::ArmPose& pose, uint16_t duration_ms) {
    // 0x08 字段顺序：pitch*10, x, y, z, roll, claw, time_ms
    std::vector<uint8_t> buf;
    buf.reserve(14);
    appendI16LE(buf, static_cast<int16_t>(std::lround(pose.pitch_deg * 10.0f)));
    appendI16LE(buf, static_cast<int16_t>(std::lround(pose.x_mm)));
    appendI16LE(buf, static_cast<int16_t>(std::lround(pose.y_mm)));
    appendI16LE(buf, static_cast<int16_t>(std::lround(pose.z_mm)));
    appendI16LE(buf, static_cast<int16_t>(std::lround(pose.roll_deg)));
    appendI16LE(buf, static_cast<int16_t>(std::lround(pose.claw_deg)));
    appendU16LE(buf, duration_ms);
    return buf;
}

std::vector<uint8_t> encodeMoveIncPayload(const core::ArmPose& delta, uint16_t duration_ms) {
    // 0x32 字段顺序：dx, dy, dz, dpitch*10, droll, dclaw, time_ms
    std::vector<uint8_t> buf;
    buf.reserve(14);
    appendI16LE(buf, static_cast<int16_t>(std::lround(delta.x_mm)));
    appendI16LE(buf, static_cast<int16_t>(std::lround(delta.y_mm)));
    appendI16LE(buf, static_cast<int16_t>(std::lround(delta.z_mm)));
    appendI16LE(buf, static_cast<int16_t>(std::lround(delta.pitch_deg * 10.0f)));
    appendI16LE(buf, static_cast<int16_t>(std::lround(delta.roll_deg)));
    appendI16LE(buf, static_cast<int16_t>(std::lround(delta.claw_deg)));
    appendU16LE(buf, duration_ms);
    return buf;
}

core::Status decodeFirmwareVersion(const std::vector<uint8_t>& payload, FirmwareVersion& out) {
    if (payload.size() < 3) {
        return core::Status::Error(core::ErrorCode::kProtocolError,
                                   "Firmware version payload < 3 bytes");
    }
    out = {};
    out.esp_major = payload[0];
    out.esp_minor = payload[1];
    out.esp_patch = payload[2];
    if (payload.size() >= 6) {
        out.has_at32 = true;
        out.at32_major = payload[3];
        out.at32_minor = payload[4];
        out.at32_patch = payload[5];
    }
    return core::Status::Ok();
}

core::Status decodeBatteryVoltageMv(const std::vector<uint8_t>& payload, uint16_t& out_mv) {
    if (payload.size() < 2) {
        return core::Status::Error(core::ErrorCode::kProtocolError, "Battery payload < 2 bytes");
    }
    out_mv = readU16LE(payload.data());
    return core::Status::Ok();
}

core::Status decodeArmState(const std::vector<uint8_t>& payload, core::ArmStateMsg& out) {
    if (payload.size() < 6) {
        return core::Status::Error(core::ErrorCode::kProtocolError, "ArmState payload < 6 bytes");
    }
    out = {};
    out.timestamp_ms = core::steadyNowMs();
    out.link_ok = 1;
    out.gripper_pose.x_mm = static_cast<float>(readI16LE(payload.data() + 0));
    out.gripper_pose.y_mm = static_cast<float>(readI16LE(payload.data() + 2));
    out.gripper_pose.z_mm = static_cast<float>(readI16LE(payload.data() + 4));

    if (payload.size() >= 12) {
        out.gripper_pose.pitch_deg = static_cast<float>(readI16LE(payload.data() + 6)) / 10.0f;
        out.gripper_pose.roll_deg = static_cast<float>(readI16LE(payload.data() + 8));
        out.gripper_pose.claw_deg = static_cast<float>(readI16LE(payload.data() + 10));
    }
    if (payload.size() >= 24) {
        for (std::size_t i = 0; i < 6; ++i) {
            out.servo_pulses[i] = readI16LE(payload.data() + 12 + i * 2);
        }
    }
    return core::Status::Ok();
}

}  // namespace luma::execution::proto
