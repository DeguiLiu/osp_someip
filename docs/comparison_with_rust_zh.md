# 与 Rust 原型（simple_someip）的对照

结论：两者在线格式与 SD 语义上对齐，差异集中在运行时模型——Rust 用 async 执行器与
分配器可选，本项目用"调用方驱动 + 定容 + 无堆"。功能覆盖上，本项目已具备
SD 编解码、服务端与客户端核心；尚未覆盖 SOME/IP-TP 重组、E2E 实现、组播加入与
第三方互操作实测。

## 1. 口径

| 项 | Rust 原型 | 本项目 |
|---|---|---|
| 版本 | simple-someip 0.15（`src/` 约 39k 行） | M0–M3 核心（`include/` 约 3.5k 行） |
| 语言 | Rust 2024，`no_std` + 可选 `alloc` | C++17，header-only |
| 运行时 | tokio（std）或 embassy（bare-metal） | 调用方事件循环 + `Tick(now_ms)` |
| 分配 | `heapless` 用于 SD；`std` 特性下 `Vec`/`Arc` | 全链路无堆（见证进程守护） |
| 错误 | `Result<T, Error>`（thiserror） | `osp::expected<T, Error>`（值类型） |
| 多态 | trait + 泛型 | CRTP / 模板参数注入 |

## 2. 能力对照

| 能力 | Rust | 本项目 | 说明 |
|---|---|---|---|
| SOME/IP 头、消息、遍历 | 有 | 有 | 逐字节对齐 19 条 golden |
| SOME/IP-TP | 跳过并计数（不重组） | 跳过并计数 | 语义一致 |
| SD entry/option/header 编解码 | 有 | 有 | entry/option 均含零拷贝视图 |
| SD 数据报构建（Find/Offer/Stop/Subscribe/Ack） | 有 | 有 | 本项目另含 Find 构建器 |
| 服务端 offer / 订阅 / 事件发布 | 有（async） | 有（同步核心） | 本项目含 TTL 到期与 TTL/2 重播 |
| 服务端请求分发 | 非 SD 原始数据报回调 | `RequestHandler` 类型化分发并自动回 Response/Error | 本项目支持 fire-and-forget |
| 客户端发现 / 订阅 / RPC | 有（async，oneshot 响应） | 有（回调 + 超时） | 请求 ID 规则一致：client<<16 \| session |
| E2E 保护 | 仅 trait 钩子 | 无 | 本项目列为范围外 |
| 组播加入 | 有（socket2） | 有（`JoinSdMulticast` + `IP_ADD_MEMBERSHIP`） | 组播实测仍需真实网卡环境 |
| 互操作实测（vsomeip） | 有（Docker） | 未跑 | 本机 Docker 守护进程不可用 |
| 金丝雀/无分配见证 | 有 | 有 | 本项目用全局 `operator new` 计数 |

## 3. 结构与运行时差异

1. **执行模型**：Rust 的 client/server 是长驻 future，网络与定时器由 tokio/embassy 驱动；
   本项目把 socket 与时钟都留给调用方，核心只有 `HandleDatagram` / `Tick` / `Request`，
   因此可在单线程事件循环、RT-Thread 任务或测试中直接复用，行为确定、无隐藏线程。
2. **内存模型**：Rust 在 `std` 下用 `Vec`/`Arc`，`no_std` 下用 `heapless`；本项目所有
   容器是模板参数定容的数组，解析是 `osp::span` 视图，编码写入调用方缓冲。
3. **错误与回调**：Rust 用 `Result` 与 channel 更新流；本项目用 `osp::expected` 与编译期
   绑定的 `Sink` / `Handler`，没有类型擦除与堆分配。
4. **API 形状**：Rust 的 `Client::request` 是 `async` 且等待响应；本项目的 `Request`
   立即返回，响应/超时通过 `Handler` 回调，调用方在 `Tick` 中推进时间。

## 4. 验证对照

| 项 | Rust | 本项目 |
|---|---|---|
| 单元/集成测试 | 824 个 `#[test]` 标注 | 88 个用例、1321 条断言 |
| 线格式 golden | 19 条手写字面量 | 19 条同源字面量（逐字节移植） |
| 无分配 | `no_alloc_witness` 测试 | `someip_no_alloc` 进程，覆盖 server + client |
| 真实网络 | tokio + vsomeip 容器互操作 | 真实 UDP 环回：服务端用例 + 客户端/服务端联调用例 |
| 异常/panic 关闭 | `panic = abort`、no_std | `-fno-exceptions -fno-rtti` 全量构建 |

## 5. 已知差距与补齐顺序

1. **互操作实测**：换到 Docker 可用的机器，用 `tests/data/vsomeip-peer` 的镜像跑
   find/offer/subscribe/publish/RPC。
2. **组播实测**：加入逻辑与示例已就绪，需在真实网卡/IGMP 环境下与 vsomeip 对跑。
3. **E2E 钩子**：在 `Server`/`Client` 的收发路径预留 `E2EHandle` 策略，默认空实现。
4. **目标板实测**：ARM Linux / RT-Thread 上的内存与吞吐数据，目前只有 x86 结果。
