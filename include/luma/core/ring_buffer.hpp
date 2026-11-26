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
// 结构说明：
//   - template <typename T, std::size_t Capacity>：
//     编译期模板类，T 为存进去的结构体类型（必须是纯数据 POD），Capacity 为固定最大条数。
//     相当于在 C 里为不同结构体各写一套定长循环数组，C++ 模板让一份代码通用所有类型。
//   - 线程安全模型：内部用一把 std::mutex 保护 head_ / count_ / buf_，保证多线程并发读写不竞态。
// ============================================================================
template <typename T, std::size_t Capacity>
class HistoryRingBuffer {
    static_assert(Capacity > 0, "HistoryRingBuffer Capacity must be > 0");
    static_assert(std::is_trivially_copyable_v<T>, "HistoryRingBuffer requires trivially_copyable T");

public:
    // 作用：向环形缓冲写入一条最新记录（O(1) 零堆分配）。
    // 行为：未满时顺序追加；已满时直接覆盖最老的一条记录，始终保留最近 Capacity 条。
    // 场景：50Hz 控臂线程每一拍写入一条遥测位姿，绝不阻塞、不申请堆内存。
    void pushOverwrite(const T& item) noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        buf_[head_] = item;
        head_ = (head_ + 1) % Capacity;
        if (count_ < Capacity) {
            ++count_;
        }
    }

    // 作用：把缓冲内当前保存的全部历史数据，严格按「从最老到最新」的时间顺序拷贝到 out 中。
    // 优化（CS-001）：Capacity 在编译期已知，先把可能触发 malloc 的 out.reserve(Capacity) 放在锁外，
    //               保证持锁临界区内只做纯内存拷贝，不拖慢高频写入线程。
    // 场景：触发安全急停时一次性抓出最近 10 秒黑匣子记录并落盘 CSV。
    void snapshot(std::vector<T>& out) const {
        out.clear();
        out.reserve(Capacity);

        std::lock_guard<std::mutex> lock(mutex_);
        const std::size_t start = (count_ == Capacity) ? head_ : 0;
        for (std::size_t i = 0; i < count_; ++i) {
            out.push_back(buf_[(start + i) % Capacity]);
        }
    }

    // 作用：返回当前已存储的有效元素个数，范围 [0, Capacity]。
    // 说明：函数后的 const 表示逻辑只读，加锁依赖成员变量 mutex_ 前的 mutable 修饰。
    std::size_t size() const noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        return count_;
    }

    // 作用：返回环形缓冲的编译期最大容量（常量，无需加锁）。
    constexpr std::size_t capacity() const noexcept { return Capacity; }

    // 作用：清空缓冲计数与写指针（O(1) 逻辑清空，不释放底层数组内存）。
    void clear() noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        head_ = 0;
        count_ = 0;
    }

private:
    // mutable 允许在 const 只读成员函数（snapshot / size）内部对互斥锁加解锁
    mutable std::mutex mutex_;
    // 编译期定长连续数组（等价于 C 的 T buf_[Capacity]），无堆分配开销
    std::array<T, Capacity> buf_{};
    std::size_t head_ = 0;   // 下一次写入将要落入的数组下标 [0, Capacity - 1]
    std::size_t count_ = 0;  // 当前有效数据条数 [0, Capacity]
};

}  // namespace luma::core
