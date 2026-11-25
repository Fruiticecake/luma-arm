#include "luma/core/inproc_event_bus.hpp"

#include <algorithm>
#include <atomic>
#include <mutex>
#include <unordered_map>
#include <utility>
#include <vector>

namespace luma::core {

namespace detail {

struct SubscriberEntry {
    SubscriptionId id = 0;
    std::atomic<bool> alive{true};
    RawHandler handler;
};

struct EventBusImpl {
    mutable std::mutex mutex;
    SubscriptionId next_id = 1;
    std::unordered_map<uint8_t, std::vector<std::shared_ptr<SubscriberEntry>>> topics;

    void unsubscribe(TopicId topic, SubscriptionId id) noexcept {
        // TODO(Stage 1B):
        // 1. 加锁 mutex
        // 2. 在 topics[static_cast<uint8_t>(topic)] 中按 id 查找条目
        // 3. 将 entry->alive.store(false, std::memory_order_release) 后从 vector 中移除
        (void)topic;
        (void)id;
    }
};

}  // namespace detail

// ============================================================================
// ★ Stage 1B 动手任务：Subscription（Move-Only RAII）与 InProcEventBus
//
// 必守不变量（Executable Spec -> tests/core/test_inproc_event_bus.cpp）：
//   1. 生命周期倒置安全：Subscription 仅持 weak_ptr<EventBusImpl>，reset() 时先 lock()；
//      即使 InProcEventBus 已先析构，reset() 也绝不触发 Use-After-Free。
//   2. 移动赋值防泄漏：operator=(Subscription&& other) 必须先处理自赋值并 reset() 自身旧订阅，
//      再接管 other 的状态并将 other.id_ 置 0（使其失效）。
//   3. 回调重入零死锁：publish() 持锁拷贝 vector<shared_ptr<SubscriberEntry>> 快照后必须立刻解锁，
//      再在锁外检查 entry->alive.load(std::memory_order_acquire) 后调用 entry->handler(...)。
//   4. 并发安全：多线程同时 publish / subscribe / reset 下 TSan 零 data race、ASan 零泄漏。
// ============================================================================

Subscription::Subscription(std::weak_ptr<detail::EventBusImpl> impl, TopicId topic,
                           SubscriptionId id) noexcept
    : impl_(std::move(impl)), topic_(topic), id_(id) {}

Subscription::~Subscription() { reset(); }

Subscription::Subscription(Subscription&& other) noexcept
    : impl_(std::move(other.impl_)), topic_(other.topic_), id_(other.id_) {
    other.id_ = 0;
}

Subscription& Subscription::operator=(Subscription&& other) noexcept {
    // TODO(Stage 1B): 防自赋值 -> reset() 释放旧订阅 -> 转移成员 -> other.id_ = 0
    (void)other;
    return *this;
}

bool Subscription::valid() const noexcept {
    // TODO(Stage 1B): id_ != 0 且 !impl_.expired()
    return false;
}

void Subscription::reset() noexcept {
    // TODO(Stage 1B): 若 id_ == 0 直接返回；否则 lock impl_ 调用 unsubscribe，并清空 id_ 与 impl_
}

InProcEventBus::InProcEventBus() : impl_(std::make_shared<detail::EventBusImpl>()) {}

InProcEventBus::~InProcEventBus() = default;

Subscription InProcEventBus::subscribe(TopicId topic, RawHandler handler) {
    // TODO(Stage 1B):
    // 1. 若 !handler 返回空 Subscription{}
    // 2. 加锁分配 id = impl_->next_id++，构造 shared_ptr<SubscriberEntry> 放入 topics
    // 3. 返回 Subscription(impl_, topic, id)
    (void)topic;
    (void)handler;
    return {};
}

std::size_t InProcEventBus::publish(TopicId topic, const uint8_t* data, std::size_t len) {
    // TODO(Stage 1B):
    // 1. 持锁拷贝该 topic 的订阅快照（注意：不要在持锁期间调 handler！）
    // 2. 解锁后遍历快照，检查 entry->alive.load(std::memory_order_acquire)
    // 3. 若存活则调用 entry->handler(topic, data, len) 并累加 delivered 计数
    (void)topic;
    (void)data;
    (void)len;
    return 0;
}

std::size_t InProcEventBus::subscriberCount(TopicId topic) const {
    // TODO(Stage 1B): 加锁查询该 topic 当前订阅数
    (void)topic;
    return 0;
}

}  // namespace luma::core
