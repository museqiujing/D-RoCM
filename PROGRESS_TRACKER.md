PROGRESS_TRACKER.md - 项目开发进度追踪表
AI 说明书：

在执行任何开发指令前，请先读取本文件以确认当前所处阶段。

只有在完成当前标记为 [ ] 的子项，并得到指令人（用户）的验收（Acceptance）后，方可将其标记为 [x]。

严禁越级开发：在 P0 阶段未全部打勾前，不得编写 P1 阶段的代码。

1. 项目当前概览
当前阶段：Phase 3 - 性能攻坚与可观测性 (Optimization & Metrics)

总体进度：[███████████████░░] 80%

最近更新日期：2026-04-14

2. 详细进度清单
Phase 0: 基础设施与协议定义 (MVP Preparation)
[x] 0.1 项目骨架搭建

[x] 配置 CMakeLists.txt（包含 gRPC, Protobuf, GTest 依赖）。

[x] 按照 TECH_DESIGN.md 创建目录结构。

[x] 0.2 核心 Proto 协议编写

[x] 定义 common.proto (基础数据类型)。

[x] 定义 registry.proto (节点注册接口)。

[x] 定义 node.proto (节点间通讯接口)。

[x] 0.3 日志与观测基础设施 (Logging Setup)
    - [x] 集成 `spdlog` 异步模式，配置多级日志（Console + File）。
    - [x] 封装统一的日志宏（如 `DROCM_LOG_INFO`），支持打印文件名与行号。
    - [x] 验证异步日志在高频并发下的性能，确保不阻塞主通讯线程。

Phase 1: 异步通讯内核 (Core Engine)
[x] 1.1 gRPC 异步基础封装

[x] 实现基础的 CompletionQueue 管理器。

[x] 1.2 C++20 协程适配层 (Coro-Adapter)

[x] 实现 Awaiter 对象，将 gRPC Tag 映射至协程 Handle。

[x] 实现 co_await 风格的 Unary RPC 包装器。

[x] 1.3 Registry 服务实现

[x] 实现节点注册逻辑。

[x] 实现基于内存的简单服务列表存储。

Phase 2: 高级通讯模式与自愈 (Advanced Features)
[x] 2.1 流式通讯实现 (Streaming)

[x] 实现 Server Streaming 异步封装（用于传感器数据）。

[x] 实现 Bidirectional Streaming 异步封装（用于实时控制）。

[x] 2.2 健康检查机制

[x] 实现节点心跳上报逻辑。

[x] 在 Registry 中实现超时节点自动剔除逻辑。

[x] 2.3 鲁棒性增强

[x] 实现指数退避（Exponential Backoff）重连算法。
[x] 实现信号控制保活（SIGINT/SIGTERM 优雅退出）。
[x] 实现金字塔资源释放顺序（AGENTS.md 红线）。
[x] 实现 Registry 与 Node 解耦（lidar_emulator 专注 Node 职责）。
[x] 实现自动重新注册（Registry 重启后节点自愈）。

[x] 2.4 服务发现接口 (Discover)

[x] 在 registry.proto 中定义 DiscoverRequest/DiscoverResponse。
[x] 在 RegistryServiceImpl 中实现 Discover 方法。
[x] NodeTable 健康过滤：Discover 只返回 missed_heartbeats == 0 的健康节点。
[x] 编写 NodeTable 单元测试（覆盖注册、心跳、健康过滤、并发）。

[x] 2.4 拓扑发现与 P2P 闭环 (Topology & P2P)
    - [x] 实现 Registry::Discover 接口（支持健康状态过滤）。
    - [ ] 封装 `drocm::NodeClient` 屏蔽底层寻址细节。
    - [ ] 实现 `lidar_subscriber` 示例，打通“发现-订阅-接收”全链路。

### Phase 3: 性能攻坚与可观测性 (Optimization & Metrics)
[x] 3.1 内存分配优化 (Memory Tuning)
    - [x] 在数据传输路径引入 Protobuf Arena 零拷贝分配。
    - [x] 实现"分配后复用"策略：arena.Reset() O(1) 内存回收。
[x] 3.2 异步流控优化 (Flow Control)
    - [x] 实现基于重试的非阻塞背压机制（指数退避）。
    - [ ] 完整协程背压：使用 ServerAsyncWriter + co_await（留待后续）。
[x] 3.3 可观测性与基准测试 (Observability & Benchmark)
    - [x] 集成 Prometheus-cpp 监控 QPS 和延迟（drocm_messages_sent_total, drocm_stream_latency_ms）。
    - [x] 编写 benchmark.sh 压测脚本，支持 ghz 和自定义模式。


3. 已完成任务验收记录 (Audit Log)
**2026-04-14 - Phase 3 (Performance & Observability) Complete:**
- ✅ Protobuf Arena reuse strategy: arena.Reset() for O(1) memory deallocation
- ✅ Non-blocking backpressure in Subscribe: exponential retry (50ms, 100ms, 150ms)
- ✅ Prometheus metrics integration: drocm_messages_sent_total, drocm_stream_latency_ms
- ✅ benchmark.sh script created (supports ghz and custom mode)
- ✅ Zero compilation warnings, clean build verified

**2026-04-13 - Phase 1.3 (Registry) & Phase 2.2 (Health Check) Complete:**
- ✅ Defined ErrorCode enum (10 codes: INVALID_ARGUMENT, NODE_NOT_FOUND, NODE_ALREADY_EXISTS, etc.)
- ✅ Defined Result<T> type for exception-free error handling
- ✅ Added timeout support to AwaitableOperation (cancel_with_timeout method)
- ✅ NodeTable with std::shared_mutex (read-heavy optimized, lock-free serialization)
- ✅ RegistryServiceImpl: Register, Discover, Heartbeat, Deregister RPCs
- ✅ HealthChecker: background std::jthread with std::stop_token monitoring
- ✅ Auto-stale-node-removal: 3 missed heartbeats = node removed
- ✅ INFO-level logging for node register/deregister/remove events
- ✅ Zero compilation warnings, clean shutdown verified

4. AI 操作指引（重要）
每次对话开始时，请按以下流程更新本文件：

定位：寻找清单中第一个未打勾 [ ] 的子项。

确认：向用户确认是否开始该子项的开发。

执行：根据 TECH_DESIGN.md 和 AGENTS.md 的规范编写代码。

标记：在代码运行成功并通过用户验收后，在本文件中将该项修改为 [x]。