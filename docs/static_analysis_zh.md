# 静态分析结果

结论：cpplint 0 条；clang-tidy 的 bugprone 与 performance 检查在修复两项后只剩
“相邻参数类型相同、可能传反”的提示，属于已接受偏差；编译器在
`-Wall -Wextra -Wpedantic -Wconversion -Wshadow` 下零告警。

## 1. 工具与命令

```bash
cpplint --recursive include/osp/someip tests examples

clang-tidy tests/header_smoke.cpp -checks='-*,bugprone-*' \
    -- -std=c++17 -Iinclude -I../newosp/include
clang-tidy tests/header_smoke.cpp -checks='-*,performance-*' \
    -- -std=c++17 -Iinclude -I../newosp/include
```

`tests/header_smoke.cpp` 同时实例化伞头、`Server` 与 `Client`，因此这一条 TU 覆盖了
协议层、服务端与客户端的公开接口。

## 2. 已修复

| 检查 | 位置 | 处理 |
|---|---|---|
| `bugprone-crtp-constructor-accessibility` | `wire.hpp`、`payload.hpp` | CRTP 基类构造/拷贝/析构改为 private，并 `friend Derived` |
| `bugprone-branch-clone` | `sd/entry.hpp` | 合并 `kSubscribe` / `kSubscribeAck` 两个相同分支 |
| `performance-enum-size` | `message.hpp` | `SdRejectReason` 底层类型从 `uint16_t` 收窄为 `uint8_t` |

## 3. 接受的偏差

`bugprone-easily-swappable-parameters` 仍是唯一一类报告，当前 11 条（加入 TP 与 E2E Profile 1
后没有新增检查类别）。例如：

- `Header::New(message_id, request_id, protocol_version, interface_version, ...)`
- `Client::Publish(service, event_group_id, event_id, payload, ...)`
- `Server::Offer(service, endpoint, ttl_s, now_ms)`

理由：这些参数是协议字段，调用点均以命名变量传入；要彻底消除需要为每个字段引入
`NewType`，会让 API 与 Rust 原型明显分叉。若后续引入严格 MISRA 审计，再按字段逐个
强类型化。`performance-*` 当前 0 条。

## 4. 与编译告警的关系

三种构型（普通、`-fno-exceptions -fno-rtti`、ASan+UBSan）均以
`-Wall -Wextra -Wpedantic -Wconversion -Wshadow` 构建，当前零告警；
Release 构建曾暴露 newosp `log.hpp` 的未使用参数，已在 newosp 侧修复（提交 `996e498`）。
