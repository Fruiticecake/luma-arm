#pragma once

#include <array>
#include <cstddef>
#include <mutex>
#include <type_traits>
#include <vector>

namespace luma::core {

// ============================================================================
// ★ Stage 1A 动手任务：泛型覆盖型历史环形缓冲（飞控黑匣子 & 滑动窗底座）
//
// 契约与不变量（Executable Spec -> tests/core/test_ring_buffer.cpp）：
//   1. 零堆分配写入：内部使用定长 std::array<T, Capacity>，pushOverwrite() 必须 O(1) 且不分配堆内存。
//   2. 覆盖语义：未满时顺序追加；满时自动覆写最老一条，始终保留最新的 Capacity 条。
//   3. 时间有序快照：snapshot(out) 必须先 clear() out，再严格按「从最老到最新」顺序拷出当前全部元素。
//   4. 线程安全：支持单/多线程并发调用 pushOverwrite() / snapshot() / size() / clear()，TSan 零竞态。
// ============================================================================
template <typename T, std::size_t Capacity>
class HistoryRingBuffer {
    static_assert(Capacity > 0, "HistoryRingBuffer Capacity must be > 0");
    static_assert(std::is_trivially_copyable_v<T>, "HistoryRingBuffer requires trivially_copyable T");

public:
    void pushOverwrite(const T& item) noexcept {
        // TODO(Stage 1A):
        // 1. 加锁 mutex_
        // 2. 将 item 写入 buf_[head_]
        // 3. 更新 head_ = (head_ + 1) % Capacity
        // 4. 若 count_ < Capacity 则 ++count_
        (void)item;
    }

    void snapshot(std::vector<T>& out) const {
        // TODO(Stage 1A):
        // 1. 加锁 mutex_
        // 2. out.clear(); out.reserve(count_);
        // 3. 计算最老元素起始索引：start = (count_ == Capacity) ? head_ : 0
        // 4. 依次拷贝 count_ 条元素到 out（按从旧到新顺序）
        out.clear();
    }

    std::size_t size() const noexcept {
        // TODO(Stage 1A): 加锁返回 count_
        return 0;
    }

    constexpr std::size_t capacity() const noexcept { return Capacity; }

    void clear() noexcept {
        // TODO(Stage 1A): 加锁将 head_ 与 count_ 归零
    }

private:
    mutable std::mutex mutex_;
    std::array<T, Capacity> buf_{};
    std::size_t head_ = 0;   // 下一次写入的槽位索引
    std::size_t count_ = 0;  // 当前有效元素个数 [0, Capacity]
};

}  // namespace luma::core
