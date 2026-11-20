# API 参考

本文按头文件说明公开接口。所有接口都在 `namespace osp::someip` 下，除特别标注外满足：
`noexcept`、不使用异常与 RTTI、不分配堆内存、容量由模板参数在编译期确定。

## 1. 约定

| 项 | 规则 |
|---|---|
| 错误 | 返回 `osp::expected<T, Error>`；`Error` 是值类型（`code` / `value` / `available`） |
| 视图 | `*View` 类型只在调用方缓冲有效期内可用，不拥有数据 |
| 值类型 | `Header` / `Message<P>` / `Entry` / `Options` 可直接编码，长度字段由构造参数推出 |
| 时间 | 所有超时、TTL、重播都以调用方传入的 `now_ms` 为准，库内不读时钟 |
| 并发 | 单个 `Server` / `Client` 归属一个线程；跨线程请自行加锁或分片 |

常用常量：`kHeaderSize`（16）、`kSdPort`（30490）、`kSdMulticastAddress`（239.255.0.255）、
`sd::kEntrySize`（16）、`sd::kMaxTtl`（0x00FFFFFF）。

## 2. 线格式与编解码

### 2.1 Header 与 HeaderView

| 接口 | 说明 |
|---|---|
| `Header::New(id, request_id, proto_ver, iface_ver, type, code, payload_len)` | 由载荷长度推出 32 位 Length |
| `Header::NewSd(request_id, sd_header_size)` | SD 消息（消息 ID 0xFFFF8100） |
| `Header::NewEvent(service_id, event_id, request_id, iface_ver, payload_len)` | 通知消息 |
| `Header::FromFields(...)` | 按原始字段构造，用于转发 |
| `HeaderView::Parse(buf)` → `expected<ParsedHeader, Error>` | 校验协议版本、消息类型、返回码；`rest` 为剩余字节 |
| `HeaderView::Id/ Length/ RequestId/ PayloadSize/ Type/ Code/ IsSd()` | 只读访问 |
| `HeaderView::ToHeader()` | 复制成值类型 |

### 2.2 Message 与 MessageView

| 接口 | 说明 |
|---|---|
| `Message<P>::Message(Header, P)` | 值类型消息，`P` 为 `OpaquePayload` 或 `OwnedPayload<N>` |
| `MessageView::Parse(buf)` | 解析一条完整消息（含长度校验），SD 消息额外校验载荷长度 |
| `MessageView::Decode(buf)` → `DecodedMessage{view, rest}` | 从数据报前端解析一条，剩余字节交回 |
| `MessageView::Head() / Body() / IsSd() / WithBody(span)` | 视图访问与载荷替换 |
| `MessageWalker` | 逐条遍历数据报；TP 分段跳过并计数；畸形处产出 `WalkItem::Kind::kMalformed` |

载荷：`OpaquePayload(span)` 借用调用方缓冲；`OwnedPayload<N>` 在对象内持有最多 N 字节。
`PayloadCodec<T>` 是自定义载荷的 CRTP 骨架（实现 `DoEncodedSize()` / `DoEncodeTo()`）。

### 2.3 基础值类型

| 类型 | 关键接口 |
|---|---|
| `MessageId` | `FromServiceAndMethod()`、`ServiceId()`、`MethodId()`、`IsEvent()`、`IsSd()` |
| `MessageTypeField` | `New(type, tp)`、`Type()`、`IsTp()` |
| `ReturnCode` | `Ok()`、`NotOk()`、`UnknownService()` 等命名码；`Generic(v)`、`Interface(v)`、`Kind()` |
| `ServiceInstance` | `New(service, instance, major)`、`IsWildcard()` |
| `Ipv4Endpoint` | `{address, port}`，可直接比较 |
| `Error` | `Incomplete()`、`InvalidLength()`、`InvalidTpSegment()` 等工厂方法 |

## 3. Service Discovery

| 接口 | 说明 |
|---|---|
| `sd::Entry::Find/Offer/StopOffer/Subscribe/Ack/Nack(...)` | 构造 16 字节 entry 值类型 |
| `sd::Options::IpV4Endpoint/Configuration/LoadBalancing(...)` | 构造 option |
| `sd::Header(flags, entries, options)` | 编码完整 SD 载荷 |
| `sd::SdHeaderView::Parse(payload)` | 一次性校验两个分区；`NextEntry()` / `NextOption()` / `OptionAt()` 遍历 |
| `sd::EntryView` / `sd::OptionView` | 零拷贝视图，`ToEntry()` / `ToOptions()` 还原值类型 |
| `sd::SdSessionCounter` | `New()`、`Resume(last, reboot)`、`Next()`；计数 1..0xFFFF，首次回绕后 reboot 标志变成 Continuous |

数据报构建器（写入调用方缓冲，返回字节数）：

```cpp
BuildOfferServiceDatagram(out, offer_request, session);
BuildStopOfferServiceDatagram(out, offer_request, session);
BuildFindServiceDatagram(out, service_id, session);
BuildSubscribeEventGroupDatagram(out, subscribe_request, session);
BuildSubscribeAckDatagram(out, ack_request, session);
```

结构体字段见 `sd_codec.hpp`；`SubscribeEventGroupRequest` 需要 `local_ip` 与 `local_rx_port`，
接收方据此回事件。

## 4. 服务端

```cpp
template <typename Sink, std::uint32_t MaxOffers = 8U, std::uint32_t MaxSubscribers = 16U,
          std::uint32_t MaxDatagram = 1400U, typename RequestHandler = NoRequestHandler,
          typename E2E = NoE2E, typename Observer = NoServiceObserver,
          std::uint32_t MaxRemoteServices = 8U, typename Tp = NoTpReassembly>
class Server;
```

| 方法 | 说明 |
|---|---|
| `Server(Sink& sink, Ipv4Endpoint sd_multicast = SdMulticastEndpoint())` | `Sink` 形如 `void(span<const uint8_t>, const Ipv4Endpoint&)`，必须活过 Server |
| `Offer(service, endpoint, ttl_s, now_ms)` | 登记并立即公告一次；失败表示槽位已满 |
| `StopOffer(service)` | 发 StopOffer 并移除订阅 |
| `Publish(service, event_group_id, event_id, payload, now_ms)` | 向该组所有已订阅端点发通知 |
| `HandleDatagram(bytes, source, now_ms)` | SD、请求、TP 分段都在这里分流 |
| `Tick(now_ms)` | TTL 到期、TTL/2 重播、TP 超时 |
| `OfferedCount()` / `SubscriberCount()` / `IsSubscribed(...)` | 状态查询 |
| `Handler()` / `E2EHandler()` / `ObserverHandler()` / `TpHandler()` | 取回编译期注入的策略对象 |

请求处理：实现 `RequestReply OnRequest(const IncomingRequest&)`，返回
`{handled, is_error, code, payload}`；`handled=false` 表示不回复，`RequestNoReturn` 自动只分发。

远端观察：实现 `OnServiceAvailable(ServiceInstance)` 与 `OnServiceGone(ServiceInstance)`，
即可得知其他节点 offer 的出现与消失（本节点自身公告会被忽略）。

## 5. 客户端

```cpp
template <typename Sink, typename Handler, std::uint32_t MaxServices = 16U,
          std::uint32_t MaxPending = 4U, std::uint32_t MaxDatagram = 1400U,
          typename E2E = NoE2E, std::uint32_t MaxSubscriptions = 8U,
          std::uint32_t MaxPeers = 8U, typename Tp = NoTpReassembly>
class Client;
```

`Handler` 需要实现：`OnResponse(request_id, MessageView)`、`OnEvent(MessageView)`、
`OnTimeout(request_id)`、`OnSubscribeAck(service, instance, group, granted_ttl)`、
`OnServiceAvailable/OnServiceGone(ServiceInstance)`、`OnSenderRebooted(Ipv4Endpoint)`。

| 方法 | 说明 |
|---|---|
| `Client(Sink&, Handler&, local_endpoint, sd_destination = SdMulticastEndpoint())` | 本端地址用于订阅选项 |
| `StartFind(service_id, now_ms)` | 立即发 Find，随后按 100 ms 起倍增重发三次 |
| `HandleDatagram(bytes, source, now_ms)` | SD、响应、通知、TP 分段 |
| `Tick(now_ms)` | find 重发、请求超时、订阅半 TTL 续订、服务过期 |
| `Request(service, method_id, payload, timeout_ms, now_ms)` | 需要服务已在缓存中；成功仅表示已发出 |
| `Subscribe(service, event_group_id, ttl_s, now_ms)` | 建档并发送；Ack/Nack 回到 `OnSubscribeAck` |
| `FindCached(service)` / `FindCachedByService(id)` | 发现缓存查询，返回 `RemoteService*` |
| `ServiceCount()` / `PendingCount()` / `SubscriptionCount()` | 状态查询 |

重启判定：同一来源的会话号回到 1 且距上一条报文至少 `kRebootQuietMs`（2000 ms）时，
清除该来源的服务与订阅并回调 `OnSenderRebooted`。

## 6. TP 与 E2E

| 类型 | 说明 |
|---|---|
| `TpHeader::Parse(payload)` / `TpHeader::Write(out, offset, more)` | 四字节分段头，偏移以 16 字节为单位 |
| `TpReassembly<MaxPayload = 4096, MaxSessions = 1, TimeoutMs = 1000>` | `Feed(segment, now_ms, out)` 完成时返回 `MessageView`；`Tick()` 清理超时；容量为 `MaxSessions * (MaxPayload + 16)` |
| `NoTpReassembly` | 默认策略，不占内存 |
| `NoE2E` | 默认策略，`Check` 不适用、`Protect` 原样返回 |
| `E2EProfile1<DataId>` | 前置 2 字节头：计数器 + CRC-8/SAE-J1850；`Protect` 需要缓冲区预留两字节 |
| `E2EProfile4<DataId, Instance, MaxDeltaCounter>` | 前置 12 字节头：长度 + 计数器 + 数据 ID + CRC-32/AUTOSAR；预留 12 字节 |
| `E2EProfile7<DataId, MaxDeltaCounter>` | 前置 20 字节头：CRC-64/XZ + 长度 + 计数器 + 数据 ID；预留 20 字节 |
| `Crc8SaeJ1850` | 可单独使用的 CRC 计算器 |

把策略接进 Server/Client 只需换模板参数：

```cpp
using Profile = osp::someip::E2EProfile1<0x1234U>;
osp::someip::Server<Sink, 8U, 16U, 1400U, Handler, Profile,
                    osp::someip::NoServiceObserver, 8U,
                    osp::someip::TpReassembly<8192U, 2U, 1000U>> server(sink);
```

## 7. 网络接入

`udp_link.hpp` 只提供适配器，不管理套接字生命周期：

| 接口 | 说明 |
|---|---|
| `UdpSender(socket)` | 作为 Sink 使用，调用 `SendTo`，忽略失败 |
| `EndpointOf(const osp::SocketAddress&)` | `RecvFrom` 的源地址转 `Ipv4Endpoint` |
| `JoinSdMulticast(socket, interface_address)` | 在指定接口加入 239.255.0.255 |
| `SetSdMulticastInterface(socket, interface_address)` | 指定组播出口（`IP_MULTICAST_IF`） |
| `SetSdMulticastLoop(socket, enable)` | 组播回环开关 |

典型组合：SD 用一个套接字（绑定 30490 并入组），服务流量用另一个套接字；SD 与服务共用
一个套接字也可以，但组播接收必须绑定 30490。

## 8. 最小示例

服务端（省略错误处理）：

```cpp
#include "osp/someip/server.hpp"
#include "osp/someip/udp_link.hpp"

struct Sink final {
  osp::UdpSocket* sd;
  osp::UdpSocket* service;
  void operator()(osp::span<const std::uint8_t> datagram,
                  const osp::someip::Ipv4Endpoint& to) const noexcept {
    const auto address = osp::SocketAddress::FromIpv4Bytes(to.address, to.port);
    osp::UdpSocket* socket = (osp::someip::kSdPort == to.port) ? sd : service;
    if (address.has_value()) {
      (void)socket->SendTo(datagram.data(), datagram.size(), address.value());
    }
  }
};

struct Echo final {
  osp::someip::RequestReply OnRequest(const osp::someip::IncomingRequest& request) noexcept {
    return osp::someip::RequestReply{true, false, osp::someip::ReturnCode::Ok(), request.payload};
  }
};

void RunServer(osp::UdpSocket& sd_socket, osp::UdpSocket& service_socket,
               const std::array<std::uint8_t, 4U>& interface_address, std::uint32_t now_ms) {
  Sink sink{&sd_socket, &service_socket};
  osp::someip::Server<Sink, 8U, 16U, 1400U, Echo> server(sink);
  const osp::someip::ServiceInstance service =
      osp::someip::ServiceInstance::New(0x1234U, 0x0001U, 1U);
  (void)server.Offer(service, osp::someip::Ipv4Endpoint{interface_address, 30509U},
                     30U, now_ms);
  const std::uint8_t payload[4] = {0x01U, 0x02U, 0x03U, 0x04U};
  server.Publish(service, 0x0001U, 0x8001U, osp::make_span(payload), now_ms);
}
```

客户端：

```cpp
#include "osp/someip/client.hpp"
#include "osp/someip/udp_link.hpp"

struct Handler final {
  void OnResponse(std::uint32_t, osp::someip::MessageView) noexcept {}
  void OnEvent(osp::someip::MessageView) noexcept {}
  void OnTimeout(std::uint32_t) noexcept {}
  void OnSubscribeAck(std::uint16_t, std::uint16_t, std::uint16_t, std::uint32_t) noexcept {}
  void OnServiceAvailable(osp::someip::ServiceInstance) noexcept {}
  void OnServiceGone(osp::someip::ServiceInstance) noexcept {}
  void OnSenderRebooted(const osp::someip::Ipv4Endpoint&) noexcept {}
};

void RunClient(osp::UdpSocket& socket, const osp::someip::Ipv4Endpoint& local,
               std::uint32_t now_ms) {
  Handler handler;
  osp::someip::UdpSender sink(socket);
  osp::someip::Client<osp::someip::UdpSender, Handler, 4U, 4U> client(sink, handler, local);
  (void)client.StartFind(0x1234U, now_ms);
  const auto* remote = client.FindCachedByService(0x1234U);
  if (nullptr != remote) {
    (void)client.Subscribe(remote->service, 0x0001U, 10U, now_ms);
    const std::uint8_t payload[4] = {0x01U, 0x02U, 0x03U, 0x04U};
    (void)client.Request(remote->service, 0x0001U, osp::make_span(payload), 2000U, now_ms);
  }
  client.Tick(now_ms);
}
```

两段代码都用 `g++ -std=c++17 -Iinclude -I../newosp/include` 编译通过，示例见 `examples/`。
