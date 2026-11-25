# Luma C++17 实时系统编码原则与工程标准

> **维护原则**：本文档不是从网上抄来的教条清单，而是伴随 `luma-arm` 编码、Code Review 与实机排障**持续生长的活规范**。每遇到一条真实工程坑点或性能优化，统一按 `CS-NNN` 编号沉淀，标明**正反例、底层原因与权衡代价**。

---

## 规范总览索引

| 编号 | 类别 | 核心原则一句话 | 首次沉淀场景 |
|---|---|---|---|
| `CS-001` | 实时性与并发 | **持锁临界区内严禁触发堆内存分配（`malloc`/`reserve` 锁外预留）** | `HistoryRingBuffer::snapshot()` Review |
| `CS-002` | 并发防死锁 | **严禁在持锁状态下调用外部用户回调（快照拷贝后立刻解锁派发）** | `InProcEventBus::publish()` 设计 |
| `CS-003` | 资源与生命周期 | **跨对象引用防悬垂：`Subscription` 等句柄强制 Move-Only RAII + `weak_ptr`** | `InProcEventBus` 生命周期倒置防 UAF |
| `CS-004` | 时间与可靠性 | **所有超时、心跳与 TTL 判定强制使用单调时钟 `steady_clock`，禁用挂钟** | `types.hpp` (`steadyNowMs`) 防 NTP 跳变 |
| `CS-005` | 契约与编译门禁 | **跨进程 DTO 强制 `static_assert(trivially_copyable)`；全量开启 ASan/TSan 零警告** | `types.hpp` 与 CI 三矩阵构建 |

---

## 详细条款

### `CS-001`｜持锁临界区内严禁触发堆内存分配（缩小锁粒度 + 锁外预分配）

- **背景与痛点**：在 50Hz 控臂线程高频写入、辅助线程按需读取的场景下，如果在持有 `std::mutex` 的临界区内调用可能触发 `malloc` / `realloc` 的操作（如 `std::vector::reserve(count_)`、`std::string` 拼接、格式化日志落盘），一旦撞上 glibc arena 堆锁竞争或内核缺页中断，持锁时间会出现几十微秒到毫秒级毛刺，直接拖慢实时写入线程。
- **反例（锁内隐式堆分配）**：
  ```cpp
  void snapshot(std::vector<T>& out) const {
      std::lock_guard<std::mutex> lock(mutex_);
      out.clear();
      out.reserve(count_);  // ❌ 持锁期间可能触发 malloc/realloc
      ...
  }
  ```
- **正例（编译期上界锁外预留，锁内纯内存拷贝）**：
  ```cpp
  void snapshot(std::vector<T>& out) const {
      out.clear();
      out.reserve(Capacity);  // ✅ 锁外一次性预留编译期上界，保证后续 push_back 零分配

      std::lock_guard<std::mutex> lock(mutex_);
      const std::size_t start = (count_ == Capacity) ? head_ : 0;
      for (std::size_t i = 0; i < count_; ++i) {
          out.push_back(buf_[(start + i) % Capacity]);
      }
  }
  ```
- **权衡代价与适用边界**：当容器尚未存满（`count_ < Capacity`）时会多预留一小块闲置容量；仅适用于 `Capacity` 有明确小上界（如黑匣子 500 帧 ≈ 40KB）的定长缓冲区。若 `Capacity` 极大（如 GB 级），应改为调用方复用预分配缓冲或分块拷贝。

---

### `CS-002`｜严禁在持锁状态下调用外部回调（快照解锁派发）

- **背景与痛点**：在事件总线/观察者模式中，若 `publish()` 抱着 `mutex_` 直接遍历并调用订阅者回调，一旦回调代码内部反手调用 `unsubscribe()`、`subscribe()` 或嵌套 `publish()`，轻则 `std::mutex` 同线程重入直接死锁，重则修改容器导致迭代器失效崩溃。
- **标准写法**：利用 C++ RAII 独立花括号作用域 `{ std::lock_guard lock(mutex_); snapshot = list; }` 仅持锁拷贝轻量 `shared_ptr` 快照，出括号立即解锁，在锁外检查原子存活位后再调用回调。
- **权衡代价**：每次派发多了一次极短的 `vector<shared_ptr>` 拷贝开销，换取 100% 免疫回调重入死锁与迭代器失效。

---

### `CS-003`｜资源句柄强制 Move-Only RAII，反向弱引用防生命周期倒置

- **标准规则**：所有独占资源句柄（订阅句柄 `Subscription`、文件描述符、串口句柄、共享内存映射）必须显式 `= delete` 拷贝构造与拷贝赋值，仅开放移动语义；移动赋值运算符必须满足「防自赋值 → 先 `reset()` 释放旧资源 → 转移所有权 → 源句柄置空」。
- **生命周期倒置防护**：若子句柄可能比宿主对象（如 `InProcEventBus`）晚析构，子句柄绝不持裸指针，必须持 `std::weak_ptr<Impl>`，析构/退订时先 `.lock()` 判活，杜绝 Use-After-Free。

---

### `CS-004`｜全系统禁用挂钟（`system_clock`）做超时与心跳判定

- **标准规则**：心跳看门狗（`500ms` 熔断）、命令有效期 TTL、耗时打点一律基于 `std::chrono::steady_clock`（封装为 `luma::core::SteadyMs`），防止设备开机 Wi-Fi 联网 NTP 校时回拨/前跳触发假熔断急停。挂钟仅允许用于日志人类可读展示。

---

### `CS-005`｜跨进程契约编译期自检 + Sanitizer 零警告门禁

- **标准规则**：
  1. 跨进程共享的 POD DTO 必须附带 `static_assert(std::is_trivially_copyable_v<T>)` 及关键尺寸上限断言，把协议破坏拦截在编译期；
  2. 所有代码默认开启 `-Wall -Wextra -Wpedantic -Wshadow -Wconversion` 零警告；
  3. 每次合并前必须同时通过 **ASan + UBSan** 与 **TSan** 并发检测（见 [.github/workflows/ci.yml](../.github/workflows/ci.yml)）。
