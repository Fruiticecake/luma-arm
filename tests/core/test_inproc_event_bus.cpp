#include "test_framework.hpp"
#include "luma/core/inproc_event_bus.hpp"

#include <atomic>
#include <chrono>
#include <memory>
#include <thread>
#include <vector>

using namespace luma::core;

LUMA_TEST(T06_BasicSubscribePublishAndRaiiMove) {
    InProcEventBus bus;
    int received_seq = 0;
    int call_count = 0;

    {
        auto sub = bus.subscribeTyped<HeartbeatMsg>(
            TopicId::kSysHeartbeat, [&](const HeartbeatMsg& msg) {
                received_seq = static_cast<int>(msg.seq);
                ++call_count;
            });

        LUMA_EXPECT_TRUE(sub.valid());
        LUMA_EXPECT_EQ(bus.subscriberCount(TopicId::kSysHeartbeat), std::size_t{1});

        HeartbeatMsg hb{};
        hb.seq = 42;
        const std::size_t n = bus.publishTyped(TopicId::kSysHeartbeat, hb);
        LUMA_EXPECT_EQ(n, std::size_t{1});
        LUMA_EXPECT_EQ(received_seq, 42);
        LUMA_EXPECT_EQ(call_count, 1);

        // Move constructor transfers ownership
        Subscription moved_sub(std::move(sub));
        LUMA_EXPECT_FALSE(sub.valid());
        LUMA_EXPECT_TRUE(moved_sub.valid());
        LUMA_EXPECT_EQ(bus.subscriberCount(TopicId::kSysHeartbeat), std::size_t{1});

        // Move assignment releases previous subscription first
        int second_calls = 0;
        Subscription sub2 = bus.subscribeTyped<HeartbeatMsg>(
            TopicId::kSysHeartbeat, [&](const HeartbeatMsg&) { ++second_calls; });
        LUMA_EXPECT_EQ(bus.subscriberCount(TopicId::kSysHeartbeat), std::size_t{2});

        sub2 = std::move(moved_sub);  // old sub2 must unsubscribe automatically!
        LUMA_EXPECT_EQ(bus.subscriberCount(TopicId::kSysHeartbeat), std::size_t{1});

        hb.seq = 99;
        bus.publishTyped(TopicId::kSysHeartbeat, hb);
        LUMA_EXPECT_EQ(received_seq, 99);
        LUMA_EXPECT_EQ(second_calls, 0);
    }

    // Out of scope -> RAII auto-unsubscribed
    LUMA_EXPECT_EQ(bus.subscriberCount(TopicId::kSysHeartbeat), std::size_t{0});
}

LUMA_TEST(T07_CallbackReentrancyNoDeadlock) {
    InProcEventBus bus;
    Subscription self_sub;
    Subscription dynamic_sub;
    int self_calls = 0;
    int nested_calls = 0;
    int dynamic_calls = 0;

    auto nested_sub = bus.subscribeTyped<SafetyEventMsg>(
        TopicId::kSafetyEvent, [&](const SafetyEventMsg&) { ++nested_calls; });

    self_sub = bus.subscribeTyped<HeartbeatMsg>(
        TopicId::kSysHeartbeat, [&](const HeartbeatMsg&) {
            ++self_calls;
            // Reentrant 1: unsubscribe itself inside callback
            self_sub.reset();
            // Reentrant 2: subscribe a new handler inside callback
            dynamic_sub = bus.subscribeTyped<HeartbeatMsg>(
                TopicId::kSysHeartbeat, [&](const HeartbeatMsg&) { ++dynamic_calls; });
            // Reentrant 3: nested publish inside callback
            SafetyEventMsg ev{};
            bus.publishTyped(TopicId::kSafetyEvent, ev);
        });

    HeartbeatMsg hb{};
    bus.publishTyped(TopicId::kSysHeartbeat, hb);
    LUMA_EXPECT_EQ(self_calls, 1);
    LUMA_EXPECT_EQ(nested_calls, 1);
    LUMA_EXPECT_EQ(dynamic_calls, 0);

    // Second publish should only hit dynamic_sub, not self_sub
    bus.publishTyped(TopicId::kSysHeartbeat, hb);
    LUMA_EXPECT_EQ(self_calls, 1);
    LUMA_EXPECT_EQ(dynamic_calls, 1);
}

LUMA_TEST(T08_InvertedLifetimeNoUseAfterFree) {
    Subscription dangling_sub;
    {
        auto bus = std::make_unique<InProcEventBus>();
        dangling_sub = bus->subscribeTyped<HeartbeatMsg>(
            TopicId::kSysHeartbeat, [](const HeartbeatMsg&) {});
        LUMA_EXPECT_TRUE(dangling_sub.valid());
    }
    // Bus is now destroyed! valid() must be false, reset() must be safe under ASan.
    LUMA_EXPECT_FALSE(dangling_sub.valid());
    dangling_sub.reset();
    dangling_sub.reset();  // idempotent
}

LUMA_TEST(T09_MultiThreadedConcurrencyStress) {
    InProcEventBus bus;
    std::atomic<bool> stop{false};
    std::atomic<uint64_t> delivered_total{0};

    auto pub_worker = [&] {
        HeartbeatMsg hb{};
        while (!stop.load(std::memory_order_acquire)) {
            ++hb.seq;
            delivered_total.fetch_add(bus.publishTyped(TopicId::kSysHeartbeat, hb),
                                      std::memory_order_relaxed);
        }
    };

    auto sub_worker = [&] {
        while (!stop.load(std::memory_order_acquire)) {
            auto s = bus.subscribeTyped<HeartbeatMsg>(
                TopicId::kSysHeartbeat, [](const HeartbeatMsg& msg) { (void)msg.seq; });
            std::this_thread::yield();
            s.reset();
        }
    };

    std::thread p1(pub_worker);
    std::thread p2(pub_worker);
    std::thread s1(sub_worker);
    std::thread s2(sub_worker);

    std::this_thread::sleep_for(std::chrono::milliseconds(400));
    stop.store(true, std::memory_order_release);

    p1.join();
    p2.join();
    s1.join();
    s2.join();

    LUMA_EXPECT_EQ(bus.subscriberCount(TopicId::kSysHeartbeat), std::size_t{0});
    LUMA_EXPECT_TRUE(delivered_total.load() > 0);
}

LUMA_TEST_MAIN()
