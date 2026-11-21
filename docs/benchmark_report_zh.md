# 回环 RPC 性能基线

结论：单进程、双真实 UDP 套接字、8 字节回显载荷，3000 次往返在 x86 上中位数 5–6 µs、
p99 9–24 µs、约 12 万–16 万次请求/秒；同一份源码交叉编译到 aarch64 后在 qemu 下中位数
12–14 µs、p99 27–49 µs、约 5.4 万–6.6 万次请求/秒。吞吐随机器负载波动，负载高时比空闲时
低约 20%。两组都是回环上限参考，不是目标板数据。

## 1. 环境与方法

| 项 | 值 |
|---|---|
| 内核 | Linux 6.5.0-45-generic x86_64 |
| CPU | 100 逻辑核 |
| 构建 | Release，`-fno-exceptions -fno-rtti` |
| 拓扑 | 同一进程内 server/client 各持真实 UDP 套接字（31700/31701/31702） |
| 流程 | find → offer → 100 次预热 → 3000 次请求/响应，忙轮询收发 |
| 载荷 | 8 字节（服务端原样回显） |
| 测量日期 | 2026-11-20（机器空闲）、2026-11-21（loadavg≈37 / 100 核） |

复现：

```bash
cmake -S . -B build-bench -DCMAKE_BUILD_TYPE=Release \
    -DSOMEIP_BUILD_EXAMPLES=ON -DSOMEIP_BUILD_TESTS=OFF -DSOMEIP_NO_EXCEPTIONS=ON
cmake --build build-bench -j8
./build-bench/examples/someip_loopback_bench
```

## 2. 结果（x86）

2026-11-20，机器空闲，连续三轮：

| 轮次 | avg | p50 | p99 | max | 吞吐 |
|---|---|---|---|---|---|
| 1 | 5.5 µs | 5 µs | 11 µs | 69 µs | 160,162 req/s |
| 2 | 5.7 µs | 5 µs | 12 µs | 236 µs | 157,439 req/s |
| 3 | 5.4 µs | 5 µs | 9 µs | 50 µs | 165,837 req/s |

2026-11-21 复测，同机同构型，后台有长期 qemu 负载（loadavg≈37 / 100 核），连续五轮：

| 轮次 | avg | p50 | p99 | max | 吞吐 |
|---|---|---|---|---|---|
| 1 | 6.6 µs | 6 µs | 13 µs | 99 µs | 131,452 req/s |
| 2 | 6.8 µs | 6 µs | 24 µs | 78 µs | 130,062 req/s |
| 3 | 6.8 µs | 6 µs | 11 µs | 601 µs | 129,143 req/s |
| 4 | 6.2 µs | 6 µs | 10 µs | 66 µs | 144,148 req/s |
| 5 | 7.0 µs | 6 µs | 12 µs | 51 µs | 124,735 req/s |

两次差异全部来自负载：中位数只差 1 µs，吞吐差约 20%。另外试过用 `taskset` 绑单核，
结果更差（p50 仍 6 µs，但 max 8–16 ms），因为忙轮询的两端要抢同一个核；这个基准不要绑核跑。

一次更早的 Debug 构建为 avg 8.5 µs / p50 7 µs / p99 18 µs / 105,623 req/s，
可用作优化档位的对照。

## 3. aarch64（qemu 用户态）

| 项 | 值 |
|---|---|
| 工具链 | gcc 11.4.0（Ubuntu jammy 交叉包，解包到用户目录） |
| 模拟器 | qemu-aarch64 6.2.0，`-L <root>/usr/aarch64-linux-gnu` |
| 构建 | Release，与主机同一份源码 |
| 主机 CPU | Xeon Gold 6348 @ 2.6 GHz（模拟执行，不是 ARM 板卡） |

| 轮次 | avg | p50 | p99 | max | 吞吐 |
|---|---|---|---|---|---|
| 1 | 17.0 µs | 12 µs | 49 µs | 626 µs | 53,813 req/s |
| 2 | 13.5 µs | 11 µs | 30 µs | 229 µs | 65,700 req/s |
| 3 | 16.1 µs | 13 µs | 39 µs | 298 µs | 57,096 req/s |

2026-11-21 复测（同上述负载条件）：avg 15.0–16.4 µs、p50 14 µs、p99 27–34 µs、
max 247–314 µs、55,090–60,281 req/s，与上表同量级。

qemu 用户态把每条访存与系统调用都翻译一遍，p99 的抖动主要来自模拟器本身，不能据此推断
目标板的主频、缓存或内核调度表现。这里只说明同一份代码在 aarch64 上可构建、可运行、
量级与 x86 同阶。

## 4. 解释与边界

1. 数字包含完整路径：SOME/IP 头与载荷编码、`sendto`、`recvfrom`、SD/RPC 解析、处理器回显、
   响应编码；只省掉了跨主机网络与中断。
2. 忙轮询消除了调度抖动，因此 p99 接近 p50；真实事件循环（含 `poll`/`epoll` 唤醒）会更差。
3. 单进程共享 CPU 缓存，不能外推为跨进程或多核数据。
4. 目标板（ARM/RT-Thread）与 vsomeip 对端数据仍待环境验证，见
   [validation_lanes_zh.md](validation_lanes_zh.md)。qemu 数据只代表指令集可执行。

## 5. 内存与体积

`sizeof` 在 x86 与 aarch64 上一致（同属 LP64，且容器全是定容数组）：

| 类型 | 字节 |
|---|---|
| `Server<Sink>`（8 offer / 16 订阅 / 1400 字节数据报缓冲 / 8 个远端服务） | 2168 |
| `Server` 加上 `TpReassembly<4096, 1>` | 6304 |
| `Client<Sink, Handler>`（默认容量） | 2704 |
| `TpReassembly<4096, 1>` | 4140 |
| `E2EProfile1<DataId>` | 3 |

TP 重组的内存按 `MaxSessions * (MaxPayload + 16)` 计算，不启用 `TpReassembly` 时不占空间。

aarch64 示例程序（动态链接，含 `printf`/日志等演示代码；Release 加 `-fno-exceptions -fno-rtti`，
2026-11-21 实测）：

| 程序 | text | data | bss |
|---|---|---|---|
| `someip_server_demo`（含 TP 重组） | 22,602 | 897 | 7 |
| `someip_client_demo`（含 TP 重组） | 24,137 | 889 | 7 |
| `someip_loopback_bench` | 27,642 | 840 | 32 |

构型会影响这组数字：早前一次未加 `-fno-exceptions -fno-rtti` 的测量分别是
23,428 / 24,549 / 28,123 字节 text，引用时要带上构型。

测量方式：`aarch64-linux-gnu-size`（交叉包内 binutils）。库本身是头文件，
这组数字主要是示例与 newosp 日志的固定开销，不代表集成到目标固件后的增量。

## 6. 后续

1. 在 ARM 目标板重复同一基准，记录主频与 `size` 结果。
2. Docker/vsomeip 可用后，补一条跨实现 RPC 时延对比。
