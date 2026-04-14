AGENTS.md - D-RoCM 项目开发指南 & 规范手册
致 AI 助手/开发者：你是 D-RoCM 项目的资深架构师。在生成代码前，请务必严格遵守本手册定义的架构设计与编码风格。

1. 项目概述 (Project Mission)
D-RoCM 是一个基于 C++20 和 gRPC 的高性能机器人通讯中间件。

核心理念：异步化（Coroutines）、低延迟（Lock-free）、可观测（Observability）。

架构关键：利用 C++20 协程封装 gRPC 的异步 CompletionQueue，实现 co_await 风格的通讯接口。

2. 代码风格规范 (Code Style)
本项遵循 Google C++ Style Guide 的变体，并针对现代 C++ 进行了增强：

命名约定：

类名/结构体：PascalCase (如 CoroAdapter)。

函数名：camelCase (如 postRequest)。

变量名：snake_case (如 node_id)。

成员变量：snake_case_ 以底杠结尾 (如 is_running_)。

核心标准：严格使用 C++20。禁止使用 std::thread，优先使用 std::jthread；禁止手动管理内存，严格遵循 RAII。

头文件：使用 #pragma once。包含顺序：标准库 > 第三方库 (gRPC/Protobuf) > 项目头文件。

3. 开发规范 (Development Rules)
3.1 异步处理 (The Coroutine Rule)
所有网络 IO 操作严禁阻塞线程。

必须使用封装好的 CoroHandler 或 awaitable 对象。

示例：auto status = co_await stub.GetStatus(context, request);

禁止在协程函数中使用 std::mutex，优先使用原子变量 (std::atomic) 或协程友好的同步原语。

3.2 内存与性能
零拷贝：对于频繁传输的 RobotStatus 消息，优先使用 std::move 或 std::string_view。

Arena 分配：在处理高频 Protobuf 序列化时，必须体现 google::protobuf::Arena 的使用，以减少堆分配。

3.3 错误处理
禁止使用异常 (exceptions)，除非是在无法避免的构造函数中。

使用 std::expected (或类似的 Result 类型) 或返回 grpc::Status 来处理业务逻辑错误。

4. 关键技术点实现指引
4.1 协议定义 (Protobuf)
所有新协议必须在 proto/ 目录下定义，且必须包含详尽的注释说明每个字段的物理意义（如：单位、坐标系）。

4.2 注册中心逻辑
Node 启动时必须先向 Registry 注册。

心跳间隔默认为 1s，连续 3 次丢失心跳应判定节点离线。

5. 测试要求 (Testing Standards)
单元测试 (Unit Test)：每个新功能模块必须配套 GTest 测试用例。

并发测试：涉及多线程或协程的代码，必须通过 ThreadSanitizer 检查。

基准测试：修改通讯核心代码后，必须运行 benchmark/ 下的脚本，确保 P99 延迟无显著退化。

6. AI 助手指令补充 (Prompt For AI)
当你在本项目中生成代码时，请遵循以下逻辑流程：

Check Types：是否使用了 Protobuf 定义的类型？

Check Coro：异步函数是否标记为 task<> 或返回协程句柄？是否使用了 co_await？

Check Resource：是否存在未管理的资源？（必须使用智能指针）。

Check Observability：是否在关键路径上加入了性能埋点代码？

7. 注意事项 (Strict Prohibitions)
禁止重复造轮子：通讯层只允许使用 gRPC，严禁私自引入其他网络库。

禁止硬编码：所有超时时间、端口、重试次数必须从配置文件或常量定义类中读取。

禁止阻塞主循环：Master 节点的事件循环中严禁执行耗时超过 1ms 的计算密集型任务。

8. 线程安全红线 (Thread Safety Red Lines)
[红线] 严禁在未持有互斥锁的情况下访问 NodeTable 的迭代器。
所有的健康检查剔除操作必须保证原子性。在第一阶段修复 P0 时，请顺便审计一遍 NodeTable 的线程安全实现，确保没有竞态条件。

[红线] 资源释放必须遵循"金字塔"原则：
1. 最先释放：gRPC Server 句柄（立即停止接收新请求）
2. 中间释放：所有 std::jthread 线程（触发 stop_request 并 join）
3. 最后释放：数据容器（NodeTable）和基础组件（Logger）

[红线] RegistryService 必须显式调用 Shutdown() 方法，不能完全依赖析构函数。

最后修改日期：2026-04-14
版本：v1.1.0
