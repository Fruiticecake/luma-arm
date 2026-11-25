#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <memory>
#include <type_traits>
#include <utility>

#include "luma/core/types.hpp"

namespace luma::core {

using SubscriptionId = uint64_t;
using RawHandler = std::function<void(TopicId topic, const uint8_t* data, std::size_t len)>;

namespace detail {
struct EventBusImpl;
}  // namespace detail

// ============================================================================
// Move-Only RAII 订阅句柄：析构自动退订，杜绝悬垂回调与重复退订
// ============================================================================
class Subscription {
public:
    Subscription() = default;
    ~Subscription();

    Subscription(const Subscription&) = delete;
    Subscription& operator=(const Subscription&) = delete;

    Subscription(Subscription&& other) noexcept;
    Subscription& operator=(Subscription&& other) noexcept;

    bool valid() const noexcept;
    void reset() noexcept;  // 幂等：多次调用或总线已析构时均安全无副作用

private:
    friend class InProcEventBus;
    Subscription(std::weak_ptr<detail::EventBusImpl> impl, TopicId topic, SubscriptionId id) noexcept;

    std::weak_ptr<detail::EventBusImpl> impl_;
    TopicId topic_ = TopicId::kPerceptionSceneState;
    SubscriptionId id_ = 0;
};

// ============================================================================
// 线程安全、防重入死锁的进程内事件总线
// ============================================================================
class InProcEventBus {
public:
    InProcEventBus();
    ~InProcEventBus();

    InProcEventBus(const InProcEventBus&) = delete;
    InProcEventBus& operator=(const InProcEventBus&) = delete;

    Subscription subscribe(TopicId topic, RawHandler handler);

    template <typename T>
    Subscription subscribeTyped(TopicId topic, std::function<void(const T&)> handler) {
        static_assert(std::is_trivially_copyable_v<T>, "subscribeTyped requires trivially_copyable T");
        if (!handler) {
            return {};
        }
        return subscribe(topic, [fn = std::move(handler)](TopicId, const uint8_t* data, std::size_t len) {
            if (data == nullptr || len != sizeof(T)) {
                return;
            }
            T msg{};
            std::memcpy(&msg, data, sizeof(T));
            fn(msg);
        });
    }

    // 同步快照派发：持锁拷贝订阅项快照 -> 立即解锁 -> 逐项检查 alive 后调用
    // 返回实际执行的回调数量
    std::size_t publish(TopicId topic, const uint8_t* data, std::size_t len);

    template <typename T>
    std::size_t publishTyped(TopicId topic, const T& msg) {
        static_assert(std::is_trivially_copyable_v<T>, "publishTyped requires trivially_copyable T");
        return publish(topic, reinterpret_cast<const uint8_t*>(&msg), sizeof(T));
    }

    std::size_t subscriberCount(TopicId topic) const;

private:
    std::shared_ptr<detail::EventBusImpl> impl_;
};

}  // namespace luma::core
