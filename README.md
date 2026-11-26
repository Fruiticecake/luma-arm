# Luma Embodied Arm (`luma-arm`)

> **Why "Luma"?** 源自 *Lumen*（光通量）与 *Luminance*（辉度）——一盏拥有空间感知、快慢双环决策与主动追光陪伴能力的桌面具身机械臂台灯。

基于 **RK3588 + 桌面串口机械臂 + 端云多模态感知** 的 C++17 具身智能桌面机械臂助手系统。

核心架构目标：**快慢双环解耦 + 混合零拷贝总线 + 三层物理安全兜底**，一套纯 C++17 代码同时支持 PC/WSL 单进程联调（`--mode=inproc`）与 RK3588 三进程隔离部署（`visiond` / `braind` / `motiond`）。

---

## 架构概览

```text
hal (硬件抽象) ─▶ perception (端侧NPU感知) ─▶ core (混合零拷贝总线与基础设施)
                                                   │
                 ┌─────────────────────────────────┴─────────────────────────────────┐
                 ▼                                                                   ▼
     skills + cognition (慢环编排与护栏) ──────────▶ safety (三层安全围栏) ──▶ execution (50Hz控臂/灯控)
```

- **双运行模式零代码切换**：基于 `IMessageBus` 抽象，开发期注入 `InProcMessageBus` 单进程调试，生产期切换 `UdsMessageBus` + `ShmFramePool`（POSIX `shm_open` + 无锁 Seqlock）三进程物理隔离。
- **确定性物理安全底座**：底层 `SafetyGuard` 与 `500ms` 单调时钟心跳看门狗独立于上层视觉与大模型，强制守住底座限位（`ID1 <= +28°`）、桌面高度底线（`Z >= 60mm`）与断链停机保护。
- **零三方重依赖**：`core` 仅依赖 C++17 标准库与 Linux/POSIX 系统调用，默认开启 `-Wall -Wextra -Wpedantic` 及 ASan / UBSan / TSan 门禁。

---

## 快速构建与测试（Linux / WSL）

```bash
# 1. Debug + ASan + UBSan 构建（默认推荐）
cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=Debug -DLUMA_ENABLE_ASAN=ON
cmake --build build-asan -j
ctest --test-dir build-asan --output-on-failure

# 2. ThreadSanitizer 并发竞态检测构建
cmake -S . -B build-tsan -DCMAKE_BUILD_TYPE=Debug -DLUMA_ENABLE_TSAN=ON
cmake --build build-tsan -j
ctest --test-dir build-tsan --output-on-failure
```

---

## 模块进度（滚动演进）

> 持续维护的实时 C++ 编码规范见 [docs/coding-standards.md](./docs/coding-standards.md)。

| 模块 | 职责 | 当前状态 |
|---|---|---|
| `core` | 冻结 POD DTO、覆盖型历史环形缓冲、防重入 `InProcEventBus`、异步日志、UDS 总线、SHM Seqlock 帧池 | 🚧 进行中（✅ Stage 1 全绿：`types` / `HistoryRingBuffer` / `InProcEventBus` 已过 ASan+TSan；🚧 Stage 2 待启动） |
| `safety` | 底座偏航围栏、桌面高度底线、头部防撞球、500ms 心跳熔断 | ⏳ 待开工 |
| `hal` / `execution` | 串口/TCP/Mock 传输层、NexArm V1.0.2 流式帧协议与 50Hz 伺服 | ⏳ 待开工 |
| `algo` / `perception` | 防抖滤波、手眼变换、RK3588 RKNPU 目标检测管线 | ⏳ 待开工 |
| `skills` / `cognition` / `app` | 伴读/坐姿状态机、VLM 护栏封装、双模式装配入口 | ⏳ 待开工 |

---

## License

[MIT](./LICENSE)
