#include "test_framework.hpp"
#include "luma/core/ring_buffer.hpp"

#include <atomic>
#include <cstdint>
#include <thread>
#include <vector>

using namespace luma::core;

LUMA_TEST(T03_BasicPushAndSnapshotOrder) {
    HistoryRingBuffer<int, 4> rb;
    LUMA_EXPECT_EQ(rb.capacity(), std::size_t{4});
    LUMA_EXPECT_EQ(rb.size(), std::size_t{0});

    rb.pushOverwrite(10);
    rb.pushOverwrite(20);
    rb.pushOverwrite(30);
    LUMA_EXPECT_EQ(rb.size(), std::size_t{3});

    std::vector<int> snap{999, 888};  // snapshot 必须先清空旧数据
    rb.snapshot(snap);
    LUMA_EXPECT_EQ(snap.size(), std::size_t{3});
    if (snap.size() == 3) {
        LUMA_EXPECT_EQ(snap[0], 10);
        LUMA_EXPECT_EQ(snap[1], 20);
        LUMA_EXPECT_EQ(snap[2], 30);
    }
}

LUMA_TEST(T03_WrapAroundOverwriteKeepsLatestInOrder) {
    constexpr std::size_t kCap = 16;
    HistoryRingBuffer<uint32_t, kCap> rb;

    const uint32_t total = static_cast<uint32_t>(kCap) + 137U;
    for (uint32_t i = 0; i < total; ++i) {
        rb.pushOverwrite(i);
    }

    LUMA_EXPECT_EQ(rb.size(), kCap);
    std::vector<uint32_t> snap;
    rb.snapshot(snap);
    LUMA_EXPECT_EQ(snap.size(), kCap);
    if (snap.size() == kCap) {
        for (std::size_t i = 0; i < kCap; ++i) {
            LUMA_EXPECT_EQ(snap[i], total - static_cast<uint32_t>(kCap) + static_cast<uint32_t>(i));
        }
    }

    rb.clear();
    LUMA_EXPECT_EQ(rb.size(), std::size_t{0});
    rb.snapshot(snap);
    LUMA_EXPECT_EQ(snap.size(), std::size_t{0});
}

LUMA_TEST(T03_ConcurrentPushAndSnapshotMonotonicity) {
    HistoryRingBuffer<uint32_t, 64> rb;
    std::atomic<bool> done{false};

    std::thread writer([&] {
        for (uint32_t i = 1; i <= 50000; ++i) {
            rb.pushOverwrite(i);
        }
        done.store(true, std::memory_order_release);
    });

    std::atomic<int> order_errors{0};
    std::thread reader([&] {
        std::vector<uint32_t> snap;
        while (!done.load(std::memory_order_acquire)) {
            rb.snapshot(snap);
            for (std::size_t i = 1; i < snap.size(); ++i) {
                if (snap[i] != snap[i - 1] + 1) {
                    order_errors.fetch_add(1, std::memory_order_relaxed);
                }
            }
        }
    });

    writer.join();
    reader.join();

    LUMA_EXPECT_EQ(order_errors.load(), 0);
    LUMA_EXPECT_EQ(rb.size(), std::size_t{64});
}

LUMA_TEST_MAIN()
