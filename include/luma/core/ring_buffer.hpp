#pragma once

#include <array>
#include <cstddef>
#include <mutex>
#include <type_traits>
#include <vector>

namespace luma::core {

// ============================================================================
// 泛型覆盖型历史环形缓冲（飞控黑匣子 & 滑动窗底座）
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
        std::lock_guard<std::mutex> lock(mutex_);
        buf_[head_] = item;
        head_ = (head_ + 1) % Capacity;
        if (count_ < Capacity) {
            ++count_;
        }
    }

    void snapshot(std::vector<T>& out) const {
        // 锁外预留：Capacity 编译期已知，先把可能触发 malloc 的扩容放到锁外，
        // 保证持锁临界区内只做纯内存拷贝（零堆分配），绝不拖慢 50Hz 实时写入线程
        out.clear();
        out.reserve(Capacity);

        std::lock_guard<std::mutex> lock(mutex_);
        const std::size_t start = (count_ == Capacity) ? head_ : 0;
        for (std::size_t i = 0; i < count_; ++i) {
            out.push_back(buf_[(start + i) % Capacity]);
        }
    }

    std::size_t size() const noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        return count_;
    }

    constexpr std::size_t capacity() const noexcept { return Capacity; }

    void clear() noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        head_ = 0;
        count_ = 0;
    }

private:
    mutable std::mutex mutex_;
    std::array<T, Capacity> buf_{};
    std::size_t head_ = 0;   // 下一次写入的槽位索引
    std::size_t count_ = 0;  // 当前有效元素个数 [0, Capacity]
};

}  // namespace luma::core
