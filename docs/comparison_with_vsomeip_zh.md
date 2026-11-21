# 与 vsomeip 的对照

结论：两者不在同一层。vsomeip 3.4.10 是一套带路由管理、配置体系与线程池的运行时框架，以共享库加
插件的形式交付，运行期依赖 Boost.Thread，一个应用进程稳态 7 个线程、VmData 50 MB；osp-someip 是一个
协议核心库，30 个头文件在编译期展开进调用方，不含 socket、不含线程、全链路零堆分配，示例进程 1 个线程、
VmData 264 kB、代码段 26 KB。线格式与 SD 语义两边一致，已用 vsomeip 3.4.10 做过双向互操作实测
（发现、订阅、事件、RPC 全通）。选型判据是三条：目标机能不能给它一个堆、能不能接受多线程与 JSON
配置体系、协议要不要嵌进已有事件循环或 RTOS 任务。

## 1. 测量口径

| 项 | 取值 |
|---|---|
| 日期与机器 | 2026-11-21，同一台 x86-64 主机（Linux 6.5，gcc 11） |
| 对端 | vsomeip 3.4.10，Release，Boost 1.74；`peer` 程序取自 Rust 原型 `tests/data/vsomeip-peer`，自身即 routing manager |
| 本方 | `examples/server_demo.cpp`，Release，`-fno-exceptions -fno-rtti` |
| 负载 | 两侧都是「offer 一个服务 0x1234/0x0001，一个事件组，SD 周期运行」，进入稳态后取样 |
| 内存与线程 | `/proc/<pid>/status` 的 `Threads`、`VmSize`、`VmRSS`、`VmData` |
| 代码规模 | `find … \( -name '*.cpp' -o -name '*.hpp' \) \| xargs cat \| wc -l` |
| 代码段 | `size(1)` 的 text 列 |

## 2. 数据对照

| 项 | vsomeip 3.4.10 | osp-someip |
|---|---|---|
| 交付形态 | 共享库加插件：`libvsomeip3.so` + `libvsomeip3-cfg.so` | 头文件库，无 `.so`、无插件 |
| 代码段 | 3,341,398 字节（`libvsomeip3.so` 2,838,186 + `libvsomeip3-cfg.so` 503,212），另加应用侧 `peer` 51,489 | `someip_server_demo` 26,028 字节（x86-64）/ 23,892 字节（aarch64），协议代码全部在内 |
| 协议实现规模 | `implementation/` 72,940 行、336 个 `.cpp`/`.hpp` | `include/` 5,411 行、30 个头文件；另有 `tests/` 5,492 行 |
| 运行期依赖 | `libvsomeip3.so.3`、`libvsomeip3-cfg.so`、`libboost_thread.so.1.74.0`、`libstdc++` | 无；newosp 头文件在编译期展开，不进链接闭包 |
| 线程 | 1 个进程 7 个线程（IO、定时器、路由分发等） | 1 个进程 1 个线程；库本身 0 线程 |
| 内存（稳态） | VmSize 321,636 kB、VmRSS 7,200 kB、VmData 50,108 kB | VmSize 6,084 kB、VmRSS 2,400 kB、VmData 264 kB |
| 堆分配 | `std::shared_ptr<message>`、`std::vector` 缓冲（TP 重组缓冲也是 `vector` 加 `shared_ptr`） | 全链路零分配，由 `tests/no_alloc_witness.cpp`（ctest 项 `someip_no_alloc`）守护 |
| 单实例内存上界 | 运行期动态决定 | 编译期定容：服务端实例 2.1 KB、客户端 2.7 KB，TP 重组按容量另算 |
| 配置方式 | JSON 文件加 `VSOMEIP_CONFIGURATION`、`VSOMEIP_APPLICATION_NAME`，运行时校验 | 模板参数加显式调用，无配置文件、无解析代码 |
| 传输 | UDP 与 TCP（`tcp_client/server_endpoint_impl`），另有本地 IPC 端点（UDS 或本地 TCP） | 仅 UDP |
| IP 版本 | IPv4 与 IPv6 | IPv4 收发；IPv6 endpoint / multicast option 可编解码，无收发路径 |
| E2E profile | 1、4、7、custom | 1、2、4、5、7 |
| SOME/IP-TP | 有，实现在 `endpoints/`，重组缓冲在堆上 | 有，实现在协议核心，按 16 字节单元记录到达情况，定容、支持乱序、重叠分段丢弃 |
| payload 序列化 | 有 serializer / deserializer 与消息对象体系 | 只搬字节，类型编解码归调用方 |
| OS 假设 | Linux/POSIX 加 Boost.Asio | 无 OS 假设；RT-Thread 侧已完成编译期可移植与 qemu 自检 |

### 2.1 vsomeip 有、本库没有

| 能力 | 说明 |
|---|---|
| routing manager 与本地 IPC | 多个应用进程共用一条路由，应用与路由之间走本地 socket；本库一个实例就是调用方进程里的一个对象 |
| JSON 配置体系与校验 | 服务、端口、SD 参数、可靠性都写在配置里；本库全部参数在编译期或调用点给出 |
| 插件机制 | configuration、tracing 等以插件形式加载；本库用模板参数注入 |
| 日志系统 | 分级日志与 sink；本库不打印，状态只经回调交给调用方 |
| 接口状态跟踪 | `netlink_connector` 监听网卡 up/down 与地址变化；本库由调用方负责 |
| 安全策略 | `security/` 下的凭证与策略校验；本库无进程模型，不涉及 |
| TCP 端点与 IPv6 收发 | 见第 4 节与「状态与限制」 |

### 2.2 本库有、vsomeip 3.4.10 没有

| 能力 | 说明 |
|---|---|
| E2E Profile 2 与 5 | vsomeip 3.4.10 只有 1、4、7 与 custom |
| 零堆分配保证 | 有可执行见证进程作为门禁，不是「通常不分配」 |
| 无线程、无 socket 所有权 | 可直接嵌进单线程事件循环、RTOS 任务或测试夹具 |
| 编译期内存上界 | 容器容量是模板参数，实例大小可静态计算 |
| `-fno-exceptions -fno-rtti` 全量构建 | 无异常、无 RTTI、无 vtable 与类型擦除，便于静态审查 |

## 3. 架构差异

1. **所有权边界**：vsomeip 拥有 socket、线程、定时器与消息缓冲，应用通过 `runtime::get_application()`
   拿到对象后由框架驱动；本库把这三样都留给调用方，核心只有 `HandleDatagram(datagram, source, now_ms)`、
   `Tick(now_ms)`、`Request/Publish/Offer`，发送经模板参数 `Sink` 出去，时间由调用方给。
2. **进程模型**：vsomeip 的一个应用连到 routing manager，跨进程消息经本地端点转发，因此有本地 IPC、
   应用注册与凭证校验这一整套；本库没有进程间概念，服务端与客户端就是同一进程内的两个对象，
   `examples/` 里的双进程 demo 只是两个独立实例通过 UDP 对连。
3. **内存模型**：vsomeip 的消息是 `std::shared_ptr<message>`，收发缓冲与 TP 重组缓冲都在堆上，
   容量由配置与对端行为决定；本库解析结果是调用方缓冲区上的 `osp::span` 视图，容器是定容数组，
   编码写进调用方给的缓冲，载荷可借用（`OpaquePayload`）或定容持有（`OwnedPayload<Capacity>`）。
4. **扩展方式**：vsomeip 用虚接口与插件（`plugin/`），运行期解析；本库用模板参数与 CRTP，
   编译期绑定，`Sink`、`RequestHandler`、`E2E`、`Observer` 都是类型参数。
5. **可移植性目标**：vsomeip 面向 Linux/POSIX，Boost.Asio 是硬依赖；本库不引入 OS 假设，
   RT-Thread 侧已验证 24 个核心头文件对桩 `rtthread.h` 编译通过、lwIP 2.2.0 后端类型检查零告警、
   qemu-vexpress-a9 上 15 项自检全过（运行时收发仍未打通，卡点见 `docs/validation_lanes_zh.md` 5.2）。

## 4. 为什么 osp-someip 不需要 TCP

不是「来不及做」，是范围取舍，理由按权重排列：

1. **大载荷已由 SOME/IP-TP 覆盖**。UDP 上的分段与重组已经实现：按 16 字节单元记录到达情况、
   支持乱序、重叠分段丢弃、按（消息 ID，请求 ID）区分会话、超时释放。事件与有界 RPC 这两个
   主要场景不需要 TCP 的字节流语义。
2. **TCP 的内存代价与本库的预算直接冲突**。每条连接要有 PCB、发送与接收缓冲、重传队列和拥塞窗口
   状态；lwIP 上一个 TCP PCB 加缓冲是数 KB 量级，而本库一个服务端实例是 2.1 KB、客户端 2.7 KB，
   并且要求编译期定容、运行期零分配。TCP 缓冲会随对端行为增长，给不出静态上界。
3. **TCP 的生命周期必须持有 socket 与时钟**。connect、握手、ACK、重传定时、半开连接检测都要主动
   发起 I/O 并挂定时器，而本库刻意不拥有 socket、线程和时钟。把 TCP 放进来，就得把连接状态机
   一起放进来，那条「协议核心不碰 OS」的边界就没了。
4. **实时性与可分析性**。TCP 的重传与拥塞控制让时延没有上界，缓冲占用也无法静态分析。安全相关或
   硬实时的常见做法是 UDP 加 E2E 保护加应用层超时，本库的 E2E Profile 1/2/4/5/7 与请求超时
   正是这条路线上的零件。
5. **目标平台的 TCP 栈尚未验证**。RT-Thread 侧 lwIP 运行时还没打通，在没有运行时验证的平台上再叠
   一个 TCP 状态机，只会放大未验证面。
6. **线格式层面并没有「不认识」TCP**。SD endpoint option 的 protocol 字节有枚举
   （`TransportProtocol::kTcp = 0x06`）与合法性校验；2026-11-21 起还会被显式过滤（见 5.4）。
   缺的是端点机制，不是解码能力。

仍然需要 TCP 的场景：单条消息超过 TP 重组容量、对端只提供 reliable 端点、跨公网传输、需要面向连接
的背压。这类场景建议直接用 vsomeip，或在调用方侧另建通道，本库不打算覆盖。

## 5. 与 vsomeip 的兼容性

### 5.1 已验证互通

方式：用户命名空间加 veth 对连（不用 Docker，本机账号不在 docker 组），两侧各有地址与端口空间，
SD 走 239.255.0.255:30490，TTL 3 s，cyclic offer 1000 ms。脚本：`tools/vsomeip_interop.sh`。

| 方向 | 覆盖步骤 | 结果 |
|---|---|---|
| 本方作服务端，vsomeip 作客户端 | 对端 require 服务、收到本方 offer、订阅事件组、连续收到 notify、发起 RPC 并收到应答 | 通过（2026-11-21 复跑） |
| 本方作客户端，vsomeip 作服务端 | 发现对端 offer、上报 available、RPC 请求与 4 字节应答、订阅并收到事件、对端停止后上报 gone | 通过（2026-11-21 复跑） |

字节级另有两条独立核对：E2E Profile 4 与 7 的 CRC 与 vsomeip 的 `crc.cpp`、`profile_04.cpp`、
`profile_07.cpp` 编进同一进程比对，差异为 0；SD 与消息线格式另有只用 Python 标准库的第二实现
`tools/sd_probe.py` 双向对跑。细节见 `docs/validation_lanes_zh.md`。

### 5.2 明确不兼容

- **TCP 端点**：对端只提供 reliable 端点时无法通信，本库不发也不收 TCP。
- **IPv6 传输**：对端走 IPv6 时不通；本库只能编解码 IPv6 option，没有收发路径。
- **多应用共享路由**：vsomeip 侧一个 routing manager 带多个应用的结构与本库无关，互通只发生在网络上。

### 5.3 未覆盖

- E2E 保护过线对 vsomeip（只做 CRC 逐字节核对，没有跑带 E2E 的完整链路）。
- SOME/IP-TP 过线对 vsomeip（只与 Python 探针对过分段与重组）。
- 多实例、多事件组、长时间运行的稳定性与内存趋势。
- 跨交换机组播、TLS（vsomeip 3.4.10 与本库都不含 TLS 实现）。

### 5.4 2026-11-21 修复：端点选项按 L4 协议过滤

修复前三处代码只看 option 类型、不看 protocol 字节：客户端的发现缓存、服务端的远端 offer 观察者、
以及订阅目标解析。如果对端 offer 或 subscribe 的第一个 endpoint option 是 TCP，本库会把 UDP 数据报
发往对端的 TCP 端口——报文送不到，还可能打到无关的监听者。

修复后统一走 `SdHeaderView::UdpEndpointOption(entry, option)`：遍历该 entry 两个 option run 引用的
全部选项，只接受「已知类型 + IPv4 endpoint + protocol = 0x11」的选项，一个都没有时回退到 SD 报文的
源地址。对外可见的行为变化只有一条：只声明 TCP 端点的服务仍会触发 `OnServiceAvailable`，但不会把
数据报发往 TCP 端口；这类服务对本栈实际不可用，需要在对端配置里给出 `unreliable`（UDP）端口。

覆盖用例：`sd parse - endpoint options are filtered by transport protocol`、
`client - a TCP-only offer is not used as a destination`、
`client - a UDP endpoint wins over a preceding TCP endpoint`、
`server - a TCP-only subscriber endpoint falls back to the SD source`。

## 6. 选型建议

| 场景 | 建议 |
|---|---|
| 车载 Linux / IVI，需要开箱即用的路由、配置与诊断生态 | vsomeip |
| 需要 TCP、IPv6 或多应用共享一条路由 | vsomeip |
| MCU / RTOS、无堆或堆受限、需要静态审查与确定性内存 | osp-someip |
| 要把协议嵌进已有事件循环或 RTOS 任务，不接受框架自带线程 | osp-someip |
| 需要 E2E Profile 2 或 5 | osp-someip |
| 一端是既有 vsomeip 工程、另一端是受限设备 | 两者共存，网络上互通（5.1 已实测，前提是对端配 UDP 端点） |

## 7. 复现命令

```bash
# 互操作（需先构建 vsomeip 3.4.10、peer 程序与本仓库 demo）
cmake --preset demo && cmake --build build/demo -j8
VSOMEIP_PEER_BIN=/path/to/peer VSOMEIP_LIB_DIR=/path/to/vsomeip-build \
    SOMEIP_DEMO_DIR=build/demo/examples tools/vsomeip_interop.sh server 14
VSOMEIP_PEER_BIN=/path/to/peer VSOMEIP_LIB_DIR=/path/to/vsomeip-build \
    SOMEIP_DEMO_DIR=build/demo/examples tools/vsomeip_interop.sh client 14

# 线程与内存（两侧都进入稳态后取样）
grep -E "^Threads|^VmRSS|^VmData|^VmSize" /proc/<pid>/status

# 代码段
size build/demo/examples/someip_server_demo
size /path/to/vsomeip-build/libvsomeip3.so.3.4.10 /path/to/vsomeip-build/libvsomeip3-cfg.so.3.4.10

# 代码规模
find ~/workspace/vsomeip-src/implementation \( -name '*.cpp' -o -name '*.hpp' \) | xargs cat | wc -l
find include \( -name '*.hpp' \) | xargs cat | wc -l
```
