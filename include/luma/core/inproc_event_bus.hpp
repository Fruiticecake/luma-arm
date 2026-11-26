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

// 每条订阅的全局唯一编号（0 表示空/无效句柄）
using SubscriptionId = uint64_t;

// 底层字节流回调函数类型：等价于 C 语言里的「函数指针 + void* 上下文」打包版，
// 可直接接收 C++ Lambda 表达式作为消息处理回调。
using RawHandler = std::function<void(TopicId topic, const uint8_t* data, std::size_t len)>;

// 内部实现细节命名空间（对外隐藏，类似于 C 语言头文件里只声明 struct Impl; 而不暴露字段）
namespace detail {
struct EventBusImpl;
}  // namespace detail

// ============================================================================
// Subscription：Move-Only RAII 订阅句柄（类似智能指针，专门管理“一条订阅关系”的寿命）
//
// 为什么需要这个类？
//   在 C 语言里注册回调后，经常忘记在模块销毁时调 unsubscribe，导致总线上留着野指针回调崩溃。
//   用 RAII 封装后：只要业务对象把 Subscription 当作成员变量存着，业务对象一销毁，
//   ~Subscription() 会自动退订，彻底杜绝悬垂回调；同时禁止拷贝，防止多个副本重复退订。
// ============================================================================
class Subscription {
public:
    // 默认构造：创建一个“空句柄”（id_ == 0），方便先声明成员变量，稍后再接管真正的订阅。
    Subscription() = default;

    // 析构函数：离开作用域时自动调用 reset() 完成退订（RAII 核心机制）。
    ~Subscription();

    // 显式禁止拷贝构造与拷贝赋值：一条订阅的“退订权”全系统只能有一个主人，不能复制两份。
    Subscription(const Subscription&) = delete;
    Subscription& operator=(const Subscription&) = delete;

    // 移动构造函数：把 other 手里的订阅所有权“整包转交”给新对象，并把 other 置空（id_ = 0）。
    // 典型场景：auto sub = bus.subscribe(...); 或 std::move(sub) 存入容器。
    Subscription(Subscription&& other) noexcept;

    // 移动赋值运算符：先退订自己原本持有的旧订阅（防泄漏），再接管 other 的订阅并把 other 置空。
    // 典型场景：sub = bus.subscribe(...); 覆盖旧订阅。
    Subscription& operator=(Subscription&& other) noexcept;

    // 作用：判断当前句柄是否仍绑定着一条有效订阅（id_ 非 0 且底层总线对象尚未销毁）。
    bool valid() const noexcept;

    // 作用：主动退订并把自己置为空句柄；要求幂等（连调两次或在总线已销毁后调用都绝对安全）。
    void reset() noexcept;

private:
    // friend（友元）：只允许 InProcEventBus 内部调用下面这个私有构造函数来签发有效句柄，
    // 外部业务代码无法凭空伪造一个假 SubscriptionId。
    friend class InProcEventBus;
    Subscription(std::weak_ptr<detail::EventBusImpl> impl, TopicId topic, SubscriptionId id) noexcept;

    // 关键设计：只持 weak_ptr（弱引用）而不持 shared_ptr 或裸指针！
    // 作用：不阻止总线正常析构；即使总线比句柄先销毁，reset() 里先 lock() 判空即可安全跳过，零 UAF。
    std::weak_ptr<detail::EventBusImpl> impl_;
    TopicId topic_ = TopicId::kPerceptionSceneState;
    SubscriptionId id_ = 0;
};

// ============================================================================
// InProcEventBus：线程安全、防回调重入死锁的进程内发布/订阅事件总线
//
// 整体结构说明：
//   总线本身只持有一个 shared_ptr<detail::EventBusImpl>（Pimpl 共享内核模式）：
//   - 真正的数据（互斥锁、自增 ID、按 Topic 分组的订阅表）全部放在堆上的 EventBusImpl 里；
//   - 所有签发出去的 Subscription 都通过 weak_ptr 观察这个内核，从而优雅解决“总线先析构”问题。
// ============================================================================
class InProcEventBus {
public:
    // 构造函数：在堆上创建内部共享状态体 impl_。
    InProcEventBus();

    // 析构函数：释放 impl_ 引用（若无正在运行的锁/锁外引用，内部状态体自动回收）。
    ~InProcEventBus();

    // 总线对象不可拷贝，保证全局路由状态唯一。
    InProcEventBus(const InProcEventBus&) = delete;
    InProcEventBus& operator=(const InProcEventBus&) = delete;

    // 作用：按字节流方式订阅指定 topic，返回 RAII 订阅句柄。
    // 注意：若传入空 handler，直接返回空句柄 Subscription{}，不注册脏条目。
    Subscription subscribe(TopicId topic, RawHandler handler);

    // 作用：强类型模板封装（已写好），让业务直接按结构体类型 T（如 HeartbeatMsg）订阅。
    // 原理：内部自动校验收到的字节长度 len == sizeof(T)，再用 std::memcpy 安全还原出结构体传给回调。
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

    // 作用：向指定 topic 同步发布一条字节流消息，返回实际触发执行的订阅回调次数。
    // 核心防死锁机制（CS-002）：持锁仅拷贝该 topic 的订阅列表快照 -> 立刻解锁 -> 锁外逐项判活并调回调。
    std::size_t publish(TopicId topic, const uint8_t* data, std::size_t len);

    // 作用：强类型模板封装（已写好），把任意 POD 结构体 T 直接当字节块交给 publish() 广播出去。
    template <typename T>
    std::size_t publishTyped(TopicId topic, const T& msg) {
        static_assert(std::is_trivially_copyable_v<T>, "publishTyped requires trivially_copyable T");
        return publish(topic, reinterpret_cast<const uint8_t*>(&msg), sizeof(T));
    }

    // 作用：线程安全地查询某个 topic 当前仍活跃的订阅者数量（用于单测断言与运行时监控）。
    std::size_t subscriberCount(TopicId topic) const;

private:
    std::shared_ptr<detail::EventBusImpl> impl_;
};

}  // namespace luma::core
