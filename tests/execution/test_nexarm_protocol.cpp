#include "luma/execution/nexarm_driver.hpp"
#include "luma/execution/nexarm_protocol.hpp"
#include "luma/hal/transport.hpp"
#include "test_framework.hpp"

#include <cstdint>
#include <vector>

using namespace luma::core;
using namespace luma::execution;
using namespace luma::execution::proto;

namespace {

// 离线 Mock 传输层：记录发出字节并按需吐出预置回包，验证驱动层单出口安全铁律
class MockTransport final : public luma::hal::ITransport {
public:
    Status open() override {
        open_ = true;
        return Status::Ok();
    }
    void close() override { open_ = false; }
    bool isOpen() const override { return open_; }

    Status writeAll(const uint8_t* data, std::size_t len) override {
        tx_log.insert(tx_log.end(), data, data + len);
        return Status::Ok();
    }

    Status readSome(uint8_t* buf, std::size_t max_len, int, std::size_t& out_read) override {
        if (rx_offset >= rx_queue.size()) {
            out_read = 0;
            return Status::Error(ErrorCode::kTimeout, "mock empty");
        }
        const std::size_t avail = rx_queue.size() - rx_offset;
        const std::size_t take = (avail < max_len) ? avail : max_len;
        for (std::size_t i = 0; i < take; ++i) {
            buf[i] = rx_queue[rx_offset + i];
        }
        rx_offset += take;
        out_read = take;
        return Status::Ok();
    }

    bool open_ = false;
    std::vector<uint8_t> tx_log;
    std::vector<uint8_t> rx_queue;
    std::size_t rx_offset = 0;
};

}  // namespace

LUMA_TEST(P01_ChecksumIncludesSystemIdAndEncodeFrame) {
    // 查固件版本无载荷：ID=0xFF, LEN=2, CMD=0x01
    // sum = 0xFF + 0x02 + 0x01 = 0x102 -> ~sum & 0xFF = 0xFD
    const uint8_t cs = computeChecksum(0xFF, 2, 0x01, nullptr, 0);
    LUMA_EXPECT_EQ(cs, uint8_t{0xFD});

    const auto raw = encodeFrame(kSystemId, kCmdFirmwareVersion);
    LUMA_EXPECT_EQ(raw.size(), std::size_t{6});
    if (raw.size() == 6) {
        LUMA_EXPECT_EQ(raw[0], uint8_t{0xFF});
        LUMA_EXPECT_EQ(raw[1], uint8_t{0xFF});
        LUMA_EXPECT_EQ(raw[2], uint8_t{0xFF});  // 第 3 个 0xFF 是合法 System ID！
        LUMA_EXPECT_EQ(raw[3], uint8_t{0x02});
        LUMA_EXPECT_EQ(raw[4], uint8_t{0x01});
        LUMA_EXPECT_EQ(raw[5], uint8_t{0xFD});
    }

    // 超大载荷拒绝打包
    std::vector<uint8_t> oversized(kMaxPayloadSize + 1, 0x11);
    LUMA_EXPECT_TRUE(encodeFrame(kSystemId, kCmdCoordinateSet, oversized).empty());
}

LUMA_TEST(P02_StreamParserFragmentedAndStickyAndDirtyBytes) {
    const std::vector<uint8_t> p1 = {1, 0, 2, 1, 0, 2};  // 固件版本回包
    const std::vector<uint8_t> p2 = {0x10, 0x2E};        // 供电电压 11792mV (0x2E10 LE)
    const auto f1 = encodeFrame(kSystemId, kCmdFirmwareVersion, p1);
    const auto f2 = encodeFrame(kAt32ReplyId, kCmdBatteryVoltage, p2);
    LUMA_EXPECT_FALSE(f1.empty());
    LUMA_EXPECT_FALSE(f2.empty());

    // 构造恶劣字节流：[前导脏字节] + [合法帧 1] + [1 帧校验破坏的坏帧] + [合法帧 2]
    std::vector<uint8_t> stream = {0x00, 0xAA, 0xFF, 0x12};
    stream.insert(stream.end(), f1.begin(), f1.end());

    auto bad = f1;
    if (!bad.empty()) {
        bad.back() ^= 0x01U;  // 翻转校验和最低位
    }
    stream.insert(stream.end(), bad.begin(), bad.end());
    stream.insert(stream.end(), f2.begin(), f2.end());

    // 极端半包测试：每次只喂 1 个字节！
    FrameParser parser;
    std::vector<Frame> parsed;
    for (uint8_t byte : stream) {
        parser.feed(&byte, 1, parsed);
    }

    LUMA_EXPECT_EQ(parser.checksumErrors(), uint64_t{1});
    LUMA_EXPECT_EQ(parsed.size(), std::size_t{2});
    if (parsed.size() == 2) {
        LUMA_EXPECT_EQ(parsed[0].id, kSystemId);
        LUMA_EXPECT_EQ(parsed[0].cmd, uint8_t{kCmdFirmwareVersion});
        LUMA_EXPECT_EQ(parsed[0].payload.size(), p1.size());

        LUMA_EXPECT_EQ(parsed[1].id, kAt32ReplyId);
        LUMA_EXPECT_EQ(parsed[1].cmd, uint8_t{kCmdBatteryVoltage});
        uint16_t mv = 0;
        LUMA_EXPECT_TRUE(decodeBatteryVoltageMv(parsed[1].payload, mv).ok());
        LUMA_EXPECT_EQ(mv, uint16_t{0x2E10});
    }
}

LUMA_TEST(P03_DriverSingleExitBlocksUnsafePoseZeroBytesSent) {
    MockTransport mock;
    NexArmDriver driver(mock);
    LUMA_EXPECT_TRUE(driver.connect().ok());

    // 1. 注入撞墙位姿 (Y = -130 -> > +28°)：驱动必须拦截，mock.tx_log 保持 0 字节！
    ArmPose unsafe{};
    unsafe.x_mm = 200.0f;
    unsafe.y_mm = -130.0f;
    unsafe.z_mm = 180.0f;
    const auto st_bad = driver.moveToPose(unsafe, 1000);
    LUMA_EXPECT_EQ(st_bad.code, ErrorCode::kSafetyViolation);
    LUMA_EXPECT_EQ(mock.tx_log.size(), std::size_t{0});

    // 2. 合法位姿必须通过安检并向底层写出完整 20B 帧（6B 帧壳 + 14B 载荷）
    ArmPose safe{};
    safe.x_mm = 200.0f;
    safe.y_mm = 0.0f;
    safe.z_mm = 180.0f;
    const auto st_ok = driver.moveToPose(safe, 1000);
    LUMA_EXPECT_TRUE(st_ok.ok());
    LUMA_EXPECT_EQ(mock.tx_log.size(), std::size_t{20});
}

LUMA_TEST_MAIN()
