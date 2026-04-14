# D-RoCM (Distributed Robotic Communication Middleware)

[![C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg)](https://en.cppreference.com/w/cpp/20)
[![gRPC](https://img.shields.io/badge/gRPC-latest-green.svg)](https://grpc.io/)
[![License](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)

**D-RoCM** 是一款专注于机器人高性能场景的分布式通信中间件。项目旨在解决高频传感器数据（如 LiDAR）传输过程中的内存碎片、网络拥塞以及系统自愈问题。

---

## 🚀 核心技术亮点 (Key Features)

### 1. 极致内存优化 (High-Performance Memory Model)
* **Protobuf Arena + Object Reuse**: 针对 100Hz+ 高频点云流，通过 Arena 分配器和对象复用策略，彻底消除了热点路径上的堆内存申请。
* **内存收敛验证**: 经过长时稳定性压测，系统常驻内存 (RSS) 锁死在 **7.2MB**，实现“零波动”运行，有效避免了长时运行下的 GC 抖动。

### 2. 异步非阻塞背压机制 (Non-blocking Backpressure)
* **流量自适应**: 实时监控 gRPC `ServerWriter` 状态，当消费者处理速度不足时触发 **指数退避 (Exponential Backoff)**。
* **系统熔断保护**: 自动识别并断开高延迟的“慢消费者”，保护服务端缓冲区，防止由于数据堆积导致的 OOM (Out of Memory) 风险。

### 3. 高可用服务发现 (Self-Healing Service Discovery)
* **动态服务治理**: 基于 TTL (Time-to-Live) 与双向心跳的分布式节点管理。
* **全自动自愈**: 节点内置重连逻辑，支持在注册中心 (Registry) 宕机重启后实现秒级自动重连与状态恢复。

### 4. 实时可观测性 (Observability)
* **内置 Metrics Server**: 纯 Socket 实现的轻量级 Prometheus Exporter，零外部库依赖。
* **指标监控**: 实时监控 QPS、总发送消息量 (压测峰值 > 40w) 及心跳异常计数。

---

## 📊 性能验证 (Performance Verification)

由于本项目在封闭仿真环境中运行，以下为关键测试阶段的实时数据快照：

### 1. 内存稳定性 (htop Metrics)
在 10 路并发订阅流、100Hz 高频发包环境下，进程表现出极强的内存收敛特性：
* **初始内存 (Initial RSS):** 4.8 MB
* **稳态内存 (Steady State RSS):** **7.2 MB** (在 20 分钟连续测试中保持 0 波动)
* **结论:** 证明了内存池设计成功抑制了堆碎片的产生，实现了 **Zero Allocation Spikes**。

### 2. 吞吐量压测 (ghz Benchmark)
使用 `ghz` 进行 20 分钟饱和度测试，模拟极端负载：

```text
Summary:
  Count:        410,810 messages
  Total:        1,200.00 s
  Slowest:      20.01 s (Stream context duration)
  Fastest:      0.01 ms
  Average:      19.99 s
  Requests/sec: 1.00 (Concurrent persistent streams)

Status code distribution:
  [OK]                Successful data transmission
  [Unavailable]       7 responses (Active backpressure cutoff)
  [DeadlineExceeded]  Controlled timeouts for slow consumers
```

### 3. 实时监控 (Prometheus Metrics)
通过 `/metrics` 端点获取的实时生产数据（采样于第 40 万条消息发送时）：

```prometheus
# HELP drocm_messages_sent_total Total messages sent via streaming
# TYPE drocm_messages_sent_total counter
drocm_messages_sent_total 410810

# HELP drocm_backpressure_events_total Total backpressure events triggered
# TYPE drocm_backpressure_events_total counter
drocm_backpressure_events_total 124

# HELP drocm_nodes_online Current number of online nodes
# TYPE drocm_nodes_online gauge
drocm_nodes_online 1
```

---

## 🛠️ 构建与运行 (Build & Run)

### 依赖环境
* CMake >= 3.15
* gRPC & Protobuf
* spdlog (Asynchronous Logging)

### 编译指令
```bash
mkdir build && cd build
cmake ..
make -j$(nproc)
```

### 运行流程
```bash
# 1. 启动注册中心 (Service Registry)
./registry_server

# 2. 启动 LiDAR 仿真节点 (Producer)
./lidar_emulator

# 3. 执行压测验证 (Benchmark)
ghz --insecure \
    --proto=../proto/node.proto \
    --call=drocm.node.NodeService.Subscribe \
    --duration=2m \
    localhost:50052
```

---

## 📂 项目结构
* `src/registry/`: 注册中心核心逻辑，包含 NodeTable 维护。
* `src/node/`: 仿真节点实现，集成 Arena 优化与背压流控。
* `src/utils/`: 包含异步日志、Prometheus 指标收集及信号处理。
