# osp-someip

面向嵌入式 Linux 与 RT-Thread 的 SOME/IP 协议栈，C++17 头文件库，基于 newosp 框架。
当前完成 M0 与 M1：SOME/IP 线格式、消息遍历、Service Discovery 编解码（含零拷贝解析）
与三种数据报构建器，全部通过字节级 golden 测试；服务端与客户端为下一阶段。

## 结论先行

| 项 | 状态 |
|---|---|
| 语言与标准 | C++17，`-fno-exceptions -fno-rtti` 可构建 |
| 内存 | 零堆分配，定容视图与调用方缓冲区 |
| 多态 | CRTP 静态多态，无 vtable、无工厂、无动态池 |
| 线格式 | Header / Message / Opaque payload 已实现（M0） |
| Service Discovery | entry / option / header 编解码、数据报构建器、零拷贝解析（M1） |
| 测试 | 74 个用例、1155 条断言，普通构建与无异常构建均通过 |

## 目录结构

```text
include/osp/someip/
  header.hpp          SOME/IP 16 字节头（值类型 + 零拷贝视图）
  message.hpp         完整消息、解码视图、数据报遍历器
  payload.hpp         载荷编解码骨架与不透明载荷
  wire.hpp            定界大端写入器与编码器 CRTP 骨架
  error.hpp           值类型错误码
  message_id.hpp      服务 ID / 方法 ID
  message_type.hpp    消息类型与 TP 标志
  return_code.hpp     返回码（命名码、通用区间、接口区间）
  sd/                 SD 类型、entry/option 编解码与视图、header、会话计数
  sd_codec.hpp        Offer / StopOffer / Subscribe / Ack 数据报构建器
tests/                Catch2 测试，含手工推导的线格式 golden
docs/                 设计文档与参考实现清单
```

## 构建与测试

```bash
cmake -S . -B build -DOSP_GITHUB_MIRROR=https://ghfast.top/
cmake --build build -j8
ctest --test-dir build --output-on-failure
```

无异常、无 RTTI 构建：

```bash
cmake -S . -B build-noexc -DSOMEIP_NO_EXCEPTIONS=ON
cmake --build build-noexc -j8
ctest --test-dir build-noexc --output-on-failure
```

newosp 头文件默认取 `../newosp/include`，可用 `-DOSP_INCLUDE_DIR=<path>` 覆盖。

## 设计要点

1. 编码器是 CRTP 骨架：派生类实现 `DoEncodedSize()` 与 `DoEncodeTo(osp::span<uint8_t>)`，
   基类只提供唯一的边界检查入口，因此不存在第二套长度校验。
2. 解码返回 `osp::expected<T, Error>`，错误是值类型，不抛异常、不带字符串、不分配。
3. `HeaderView` / `MessageView` 是 caller-owned 缓冲上的视图，解析零拷贝；`Header` / `Message<P>`
   是可编码的值类型，长度字段在构造时由载荷长度推出。
4. 数据报遍历器跳过 SOME/IP-TP 分段并计数，遇到畸形消息时把剩余字节原样交出。

详见 [docs/design_zh.md](docs/design_zh.md)，参考实现清单见
[docs/reference_projects_zh.md](docs/reference_projects_zh.md)。

## 路线图

```mermaid
flowchart LR
  M0["M0 线格式<br/>header/message/golden"] --> M1["M1 SD 编解码<br/>entry/option/datagram"]
  M1 --> M2["M2 服务端<br/>offer/event/subscribe"]
  M2 --> M3["M3 客户端<br/>find/subscribe/rpc"]
  M3 --> M4["M4 目标验证<br/>ARM 与互操作"]
```

## 许可证

待定。参考实现仅作行为与规范对照，不复制其代码，详见参考实现清单。
