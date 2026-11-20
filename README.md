# osp-someip

[English](README.en.md) | 中文

SOME/IP 协议栈，C++17 头文件库，面向嵌入式 Linux 与 RT-Thread，运行底座是
[newosp](https://github.com/DeguiLiu/newosp)。包含线格式、Service Discovery、服务端和客户端，
不包含 socket 与线程：收发和时钟由调用方注入，可以接进已有的事件循环或 RTOS 任务。

目前完成 M0–M3，并补上 SOME/IP-TP 重组与 E2E Profile 1。真机与第三方互操作的情况见
“状态与限制”。

## 能力

- 线格式：16 字节 SOMF 头、消息编解码、数据报遍历。
- SOME/IP-TP：分段头编解码与定容重组，按（消息 ID，请求 ID）区分会话，按超时丢弃；
  不启用时不占内存。
- Service Discovery：entry / option / header 编解码与零拷贝视图；Find、Offer、StopOffer、
  Subscribe、Ack 数据报构建；会话计数。
- 服务端：offer 槽表、find 应答（支持通配）、订阅管理、事件发布、TTL 到期与 TTL/2 重播、
  远端 offer 观察者。
- 客户端：发现缓存、find 倍增重发、请求匹配与超时、订阅半 TTL 续订、服务可用性回调、
  对端重启检测。
- RPC：`RequestHandler` 在编译期绑定，同步分发请求，自动回 Response 或 Error，支持
  fire-and-forget。
- E2E：策略模板，发送前 Protect、接收时 Check，校验失败丢弃；默认 `NoE2E`，不产生开销。
  内置 Profile 1（CRC-8 SAE J1850 + 4 位计数器 + 数据 ID）。
- 内存：容器容量由模板参数给定，解析结果是调用方缓冲区上的视图，全链路不分配堆内存。
  默认服务端实例约 2.1 KB、客户端约 2.7 KB，TP 重组按容量另算。
- 构建：`-fno-exceptions -fno-rtti` 下可用。

## 快速开始

```bash
cmake -S . -B build -DOSP_GITHUB_MIRROR=https://ghfast.top/
cmake --build build -j8
ctest --test-dir build --output-on-failure
```

也可以用预设：`host-debug`、`host-release`、`noexc`、`asan`、`demo`。

```bash
cmake --preset host-debug
cmake --build --preset host-debug -j8
ctest --preset host-debug
```

主机上 116 个用例、3831 条断言通过；`noexc` 与 ASan + UBSan 两组构建另行跑通。
aarch64 交叉构建后在 qemu 下 113 过、1 跳过（组播用例需要真机网卡）。
x86 回环 3000 次 RPC 往返，p50 5 µs、p99 9–12 µs，约 16 万次请求/秒；
aarch64（qemu）同一基准 p50 11 µs、p99 24 µs，约 6.6 万次请求/秒。
数据来自 `docs/benchmark_report_zh.md`。

## 接入方式

服务端把发送通道作为模板参数，请求处理函数在编译期绑定：

```cpp
#include "osp/someip/server.hpp"

struct UdpSink {
  void operator()(osp::span<const std::uint8_t> datagram,
                  const osp::someip::Ipv4Endpoint& to) noexcept {
    // sendto(to.address, to.port, datagram)
  }
};

struct Echo {
  osp::someip::RequestReply OnRequest(const osp::someip::IncomingRequest& request) noexcept {
    return osp::someip::RequestReply{true, false, osp::someip::ReturnCode::Ok(), request.payload};
  }
};

UdpSink sink;
osp::someip::Server<UdpSink, 8U, 16U, 1400U, Echo> server(sink);

server.Offer(service, local_endpoint, 3U, now_ms);   // 开始 offer
server.HandleDatagram(datagram, source, now_ms);     // 收到一个数据报
server.Tick(now_ms);                                 // 推进重播、超时
```

客户端用法对称，构造时传入发送通道和回调，`Tick()` 负责 find 重发、请求超时与订阅续订。
`HandleDatagram()` 不保存传入视图，回调返回后数据即可复用。

可运行的示例（服务端绑定 SD 30490 与服务端口，加入组播，回显请求并每秒发布一条事件；
客户端发现、订阅、发一次 RPC、收事件）：

```bash
cmake -S . -B build-demo -DSOMEIP_BUILD_EXAMPLES=ON
cmake --build build-demo -j8
./build-demo/examples/someip_server_demo --iface 127.0.0.1 --service 1234 --seconds 6 &
./build-demo/examples/someip_client_demo --server 127.0.0.1 --local 127.0.0.1 \
    --port 30510 --service 1234 --method 0001 --seconds 4
```

客户端加 `--multicast` 时改用组播 SD：它在 30490 另开一个套接字入组，服务端口仍是 `--port`。
不加时 SD 与业务共用一个套接字，适合同一台机器上跑两端。

`tools/sd_probe.py` 是只用 Python 标准库的第二套实现，可以当服务端或客户端，用来核对字节级别
的编解码是否一致：

```bash
python3 tools/sd_probe.py --server 127.0.0.1 --service 1234 --method 0001
python3 tools/sd_probe.py --mode server --service 1234 --seconds 6
```

## 构建选项

| 选项 | 默认 | 说明 |
|---|---|---|
| `OSP_INCLUDE_DIR` | `../newosp/include` | newosp 头文件目录 |
| `SOMEIP_FETCH_NEWOSP` | OFF | 同级目录没有 newosp 时从 GitHub 拉取 |
| `SOMEIP_BUILD_TESTS` | ON | 编译 Catch2 测试 |
| `SOMEIP_BUILD_EXAMPLES` | OFF | 编译示例与基准 |
| `SOMEIP_NO_EXCEPTIONS` | OFF | 加 `-fno-exceptions -fno-rtti` |
| `OSP_GITHUB_MIRROR` | 空 | 拉取依赖时的镜像前缀 |

安装与消费：

```bash
cmake -S . -B build -DSOMEIP_BUILD_TESTS=OFF -DCMAKE_INSTALL_PREFIX=/opt/osp-someip
cmake --build build -j8 && cmake --install build
```

```cmake
find_package(osp_someip CONFIG REQUIRED)
target_link_libraries(app PRIVATE osp::osp_someip)
```

newosp 头文件不随本包安装，消费端需要自己提供 include 路径，或在 `find_package` 前设置
`OSP_INCLUDE_DIR`。

## 目录

```text
include/osp/someip/
  header.hpp       SOMF 头，值类型与视图
  message.hpp      消息编解码、数据报遍历
  payload.hpp      载荷骨架、OpaquePayload
  owned_payload.hpp  定容持有载荷
  wire.hpp         大端写入器与编码器 CRTP 骨架
  sd/              SD 类型、entry、option、header、会话计数
  sd_codec.hpp     SD 数据报构建器
  server.hpp       服务端
  client.hpp       客户端
  udp_link.hpp     newosp UDP 套接字适配
  e2e.hpp          E2E 策略钩子
  e2e_profile1.hpp E2E Profile 1
  tp.hpp           SOME/IP-TP 分段与重组
examples/          服务端、客户端、回环基准
tests/             Catch2 测试，含手写线格式 golden
docs/              设计、验证、基准、静态分析文档
```

## 状态与限制

- ARM：已用 aarch64 交叉工具链构建并通过 qemu 运行，无目标板实测数据。
- 组播：已在真实网卡（`ens192`）上验证入组与接收，未与外部节点做过跨越交换机的对测。
- vsomeip 3.4.10 双向互操作已通过（用户命名空间加 veth 对连，不用 Docker）：
  服务端侧收到订阅、事件与 RPC 应答，客户端侧完成发现、订阅、RPC 与事件。
  复现见 `tools/vsomeip_interop.sh` 与 `docs/validation_lanes_zh.md`。
- SOME/IP-TP 按序重组，乱序分段直接丢弃；E2E 只带 Profile 1，没有 Profile 2/4/5/7。
- 参考实现只用于核对行为，不复制其代码。

## 文档

| 文档 | 内容 |
|---|---|
| [docs/design_zh.md](docs/design_zh.md) | 架构、模块划分、关键取舍 |
| [docs/validation_lanes_zh.md](docs/validation_lanes_zh.md) | 验证通道与环境前置 |
| [docs/benchmark_report_zh.md](docs/benchmark_report_zh.md) | 回环性能基线 |
| [docs/static_analysis_zh.md](docs/static_analysis_zh.md) | cpplint / clang-tidy 结论与偏差 |
| [docs/comparison_with_rust_zh.md](docs/comparison_with_rust_zh.md) | 与 Rust 原型 simple_someip 的对照 |
| [docs/reference_projects_zh.md](docs/reference_projects_zh.md) | 参考实现与互操作对端清单 |

环境自检：`./tools/verify_env.sh`。

## 许可证

MIT，见 [LICENSE](LICENSE)。
