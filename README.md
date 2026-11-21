# osp-someip

[English](README.en.md) | 中文

SOME/IP 协议栈，C++17 头文件库，面向嵌入式 Linux 与 RT-Thread，运行底座是
[newosp](https://github.com/DeguiLiu/newosp)。包含线格式、Service Discovery、服务端和客户端，
不包含 socket 与线程：收发和时钟由调用方注入，可以接进已有的事件循环或 RTOS 任务。

## 为什么用它

- 能进没有堆的目标：全链路零分配（`tests/no_alloc_witness.cpp` 就是可执行见证），容器容量由
  模板参数在编译期定死，默认服务端实例 2.1 KB、客户端 2.7 KB。
- 不夹带运行时依赖：不含 socket 与线程，收发和时钟由调用方注入；发送通道与请求处理函数都是
  模板参数，没有 vtable、没有类型擦除，`-fno-exceptions -fno-rtti` 下可用，便于静态审查。
- 协议面够做一次真实集成：SD 全流程（含 TTL 到期与重播）、RPC 同步分发、事件订阅与发布、
  SOME/IP-TP 乱序重组、E2E 五个 profile，逐项见下面的「能力」。
- 字节级可对账：手写 golden、SD / TP / E2E 三类模糊扫描、一份只用 Python 标准库的第二实现
  （`tools/sd_probe.py`）、与 vsomeip 3.4.10 双向互操作、E2E 的 CRC 与 AUTOSAR C 参考逐字节核对。

不适合：需要 payload 类型序列化（本库只搬字节，类型编解码归调用方）、需要 TCP 或 IPv6、
需要开箱即用的守护进程与配置体系（那是 vsomeip 的位置）。

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
  内置 Profile 1（CRC-8 SAE J1850）、Profile 2（CRC-8/H2F + 计数器状态机）、
  Profile 4（CRC-32/AUTOSAR）、Profile 5（CRC-16/CCITT-FALSE）与 Profile 7（CRC-64/XZ）。
- 内存：容器容量由模板参数给定，解析结果是调用方缓冲区上的视图，全链路不分配堆内存；
  TP 重组按容量另算。
- 构建：`-fno-exceptions -fno-rtti` 下可用。

## 快速开始

```bash
cmake -S . -B build -DOSP_GITHUB_MIRROR=https://ghfast.top/
cmake --build build -j8
ctest --test-dir build --output-on-failure
```

也有预设 `host-debug`、`host-release`、`noexc`、`asan`、`demo`，例如
`cmake --preset host-debug && cmake --build --preset host-debug -j8 && ctest --preset host-debug`。

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

可运行的示例（服务端 offer、回显请求、每秒发一条事件；客户端发现、订阅、发一次 RPC）：

```bash
cmake -S . -B build-demo -DSOMEIP_BUILD_EXAMPLES=ON && cmake --build build-demo -j8
./build-demo/examples/someip_server_demo --iface 127.0.0.1 --service 1234 --seconds 6 &
./build-demo/examples/someip_client_demo --server 127.0.0.1 --local 127.0.0.1 \
    --port 30510 --service 1234 --method 0001 --seconds 4
```

同一份源码还编出 `someip_e2e{1,2,4,5,7}_{server,client}_demo`（按 profile 的变体）和不走套接字的
`someip_fragmented_demo`（E2E 保护加 19 段 TP 重组）。全部参数写在 `examples/server_demo.cpp` 与
`examples/client_demo.cpp` 的文件头注释里。

`tools/sd_probe.py` 是只用 Python 标准库的第二套实现，可当客户端或服务端，用来核对字节级编解码：

```bash
python3 tools/sd_probe.py --server 127.0.0.1 --service 1234 --method 0001  # 作客户端
python3 tools/sd_probe.py --mode server --service 1234 --seconds 6         # 作服务端
```

`--e2e p1|p2|p4|p5|p7`、`--tp-chunk`、`--multicast` 这些组合用法见 `docs/validation_lanes_zh.md`。

## 构建选项

| 选项 | 默认 | 说明 |
|---|---|---|
| `OSP_INCLUDE_DIR` | `../newosp/include` | newosp 头文件目录 |
| `SOMEIP_FETCH_NEWOSP` | OFF | `OSP_INCLUDE_DIR` 不存在时从 GitHub 拉取，钉 newosp v0.9.6 |
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
include/osp/someip/  30 个头文件：线格式（header / message / wire / payload）、sd/ 与 sd_codec、
                     server、client、tp、e2e 与 e2e_profile{1,2,4,5,7}、udp_link
examples/            服务端、客户端、E2E 变体、TP 分段、回环基准
tests/               Catch2 测试，含手写 golden 与 SD / TP / E2E 模糊扫描
tools/               Python 探针、vsomeip 互操作、qemu 与可移植性检查脚本
docs/                设计、接口、验证、基准、静态分析
```

逐个头文件的职责见 [docs/api_zh.md](docs/api_zh.md)。

## 状态与限制

- ARM：aarch64 交叉构建在 qemu-user 与 qemu-system（完整内核加 busybox initramfs）下都跑过，
  无目标板实测数据。
- 组播：真实网卡（`ens192`）上验证过入组与接收，未做跨交换机对测。
- vsomeip 3.4.10 双向互操作通过（用户命名空间加 veth 对连，不用 Docker），复现见
  `tools/vsomeip_interop.sh`。
- RT-Thread：只到“编译期可移植加 qemu 自检”。24 个核心头文件对桩 `rtthread.h` 编译通过，
  lwIP 后端对 lwIP 2.2.0 头文件零告警通过类型检查，qemu-vexpress-a9 上 15 项运行时自检全过。
  真实板卡未验证；lwIP 运行时收发未打通，卡点与结论见 `docs/validation_lanes_zh.md` 5.2。
- 传输只有 UDP，端点类型只有 IPv4：SD 的 IPv6 endpoint 与 IPv6 multicast option 能编解码，
  但没有 IPv6 收发路径；TCP 未实现。
- SOME/IP-TP 按 16 字节单元记录到达情况，乱序可重组，重叠分段丢弃；E2E 未实现 Profile
  3/6/11/22/44，Profile 2 的周期用法见 `docs/api_zh.md`。
- 参考实现只用于核对行为，不复制其代码。

## 文档

| 文档 | 内容 |
|---|---|
| [docs/api_zh.md](docs/api_zh.md) | 公开接口速查与最小示例 |
| [docs/design_zh.md](docs/design_zh.md) | 架构、模块划分、关键取舍 |
| [docs/validation_lanes_zh.md](docs/validation_lanes_zh.md) | 验证通道与环境前置 |
| [docs/benchmark_report_zh.md](docs/benchmark_report_zh.md) | 回环性能基线 |
| [docs/static_analysis_zh.md](docs/static_analysis_zh.md) | cpplint / clang-tidy 结论与偏差 |
| [docs/comparison_with_rust_zh.md](docs/comparison_with_rust_zh.md) | 与 Rust 原型 simple_someip 的对照 |
| [docs/reference_projects_zh.md](docs/reference_projects_zh.md) | 参考实现与互操作对端清单 |

环境自检：`./tools/verify_env.sh`。

## 许可证

MIT，见 [LICENSE](LICENSE)。
