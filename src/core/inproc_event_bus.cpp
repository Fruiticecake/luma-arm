#include "luma/core/inproc_event_bus.hpp"

#include <algorithm>
#include <atomic>
#include <mutex>
#include <unordered_map>
#include <utility>
#include <vector>

namespace luma::core {

namespace detail {

// 单条订阅记录（由 shared_ptr 托管，保证即使在锁外派发期间被另一线程退订移除，对象内存也不会立刻释放）
struct SubscriberEntry {
    SubscriptionId id = 0;          // 该订阅的唯一编号
    std::atomic<bool> alive{true};  // 原子存活标志：退订时置 false，锁外派发前检查它以防调到刚退订的回调
    RawHandler handler;             // 用户注册的回调函数
};

// 事件总线真正的数据内核（被 InProcEventBus 用 shared_ptr 持有，被 Subscription 用 weak_ptr 观察）
struct EventBusImpl {
    mutable std::mutex mutex;    // 保护 next_id 与 topics 路由表
    SubscriptionId next_id = 1;  // 递增 ID 发号器（从 1 开始，0 留给空句柄）

    // 路由哈希表：key = TopicId 的 uint8_t 数值，value = 订阅了该 Topic 的所有条目列表
    std::unordered_map<uint8_t, std::vector<std::shared_ptr<SubscriberEntry>>> topics;

    // 作用：从指定 topic 的订阅列表中找到编号为 id 的条目，先将其 alive 标为 false，再从 vector 删除。
    // 调用时机：由 Subscription::reset() 在确认总线仍存活（weak_ptr::lock 成功）后调用。
    void unsubscribe(TopicId topic, SubscriptionId id) noexcept {
        std::lock_guard<std::mutex> lock(mutex);
        auto it = topics.find(static_cast<uint8_t>(topic));
        if (it == topics.end()) {
            return;
        }
        auto& vec = it->second;
        for (auto iter = vec.begin(); iter != vec.end(); ++iter) {
            if ((*iter)->id == id) {
                (*iter)->alive.store(false, std::memory_order_release);
                vec.erase(iter);
                break;
            }
        }
    }
};

}  // namespace detail

// 私有构造：由 InProcEventBus::subscribe() 调用，打包总线弱引用 + topic + 订阅 ID。
Subscription::Subscription(std::weak_ptr<detail::EventBusImpl> impl, TopicId topic,
                           SubscriptionId id) noexcept
    : impl_(std::move(impl)), topic_(topic), id_(id) {}

// 析构函数：离开作用域时自动调 reset() 完成退订。
Subscription::~Subscription() { reset(); }

// 移动构造：接管 other 的三项状态，并将 other.id_ 置 0 使其变为空句柄（防止重复退订）。
Subscription::Subscription(Subscription&& other) noexcept
    : impl_(std::move(other.impl_)), topic_(other.topic_), id_(other.id_) {
    other.id_ = 0;
}

// 作用：移动赋值——先退订自己原本持有的旧订阅（防订阅泄漏），再接管 other 的所有权并置空 other。
Subscription& Subscription::operator=(Subscription&& other) noexcept {
    if (this == &other) {
        return *this;
    }
    reset();
    impl_ = std::move(other.impl_);
    topic_ = other.topic_;
    id_ = other.id_;
    other.id_ = 0;
    return *this;
}

// 作用：判断当前句柄是否仍然有效（id_ != 0 且总线内核尚未销毁）。
bool Subscription::valid() const noexcept { return id_ != 0 && !impl_.expired(); }

// 作用：幂等退订——若句柄有效且总线还活着，通知内核退订；最后把自身彻底清成空句柄。
void Subscription::reset() noexcept {
    if (id_ == 0) {
        return;
    }
    if (auto sp = impl_.lock()) {
        sp->unsubscribe(topic_, id_);
    }
    id_ = 0;
    impl_.reset();
}

// 构造：在堆上初始化内部状态体。
InProcEventBus::InProcEventBus() : impl_(std::make_shared<detail::EventBusImpl>()) {}

// 析构：释放持有的 shared_ptr<EventBusImpl>。
InProcEventBus::~InProcEventBus() = default;

// 作用：向指定 topic 注册一个字节流回调，并返回自动管理寿命的 Move-Only Subscription 句柄。
Subscription InProcEventBus::subscribe(TopicId topic, RawHandler handler) {
    if (!handler) {
        return {};
    }
    auto entry = std::make_shared<detail::SubscriberEntry>();
    entry->handler = std::move(handler);

    std::lock_guard<std::mutex> lock(impl_->mutex);
    entry->id = impl_->next_id++;
    impl_->topics[static_cast<uint8_t>(topic)].push_back(entry);
    return Subscription(impl_, topic, entry->id);
}

// 作用：同步广播一条消息到 topic 的所有活跃订阅者，返回实际执行回调的个数。
// 核心机制（CS-002）：锁内只拷轻量快照 vector，出花括号立刻解锁，在锁外逐个检查 alive 再调 handler，
//                    避免回调里反手 reset()/subscribe()/publish() 时发生同线程重入死锁。
std::size_t InProcEventBus::publish(TopicId topic, const uint8_t* data, std::size_t len) {
    std::vector<std::shared_ptr<detail::SubscriberEntry>> snapshot;
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        auto it = impl_->topics.find(static_cast<uint8_t>(topic));
        if (it != impl_->topics.end()) {
            snapshot = it->second;
        }
    }

    std::size_t delivered = 0;
    for (const auto& entry : snapshot) {
        if (entry->alive.load(std::memory_order_acquire)) {
            entry->handler(topic, data, len);
            ++delivered;
        }
    }
    return delivered;
}

// 作用：线程安全地查询指定 topic 当前已注册的订阅者个数。
std::size_t InProcEventBus::subscriberCount(TopicId topic) const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    auto it = impl_->topics.find(static_cast<uint8_t>(topic));
    if (it != impl_->topics.end()) {
        return it->second.size();
    }
    return 0;
}

}  // namespace luma::core
