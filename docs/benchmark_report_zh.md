# 回环 RPC 性能基线

结论：单进程、双真实 UDP 套接字、8 字节回显载荷，3000 次往返的耗时中位数 5 µs，
p99 9–12 µs，吞吐约 16 万次请求/秒。这是 x86 回环上限参考，不是目标板数据。

## 1. 环境与方法

| 项 | 值 |
|---|---|
| 内核 | Linux 6.5.0-45-generic x86_64 |
| CPU | 100 逻辑核 |
| 构建 | Release，`-fno-exceptions -fno-rtti` |
| 拓扑 | 同一进程内 server/client 各持真实 UDP 套接字（31700/31701/31702） |
| 流程 | find → offer → 100 次预热 → 3000 次请求/响应，忙轮询收发 |
| 载荷 | 8 字节（服务端原样回显） |

复现：

```bash
cmake -S . -B build-bench -DCMAKE_BUILD_TYPE=Release \
    -DSOMEIP_BUILD_EXAMPLES=ON -DSOMEIP_BUILD_TESTS=OFF -DSOMEIP_NO_EXCEPTIONS=ON
cmake --build build-bench -j8
./build-bench/examples/someip_loopback_bench
```

## 2. 结果（连续三次）

| 轮次 | avg | p50 | p99 | max | 吞吐 |
|---|---|---|---|---|---|
| 1 | 5.5 µs | 5 µs | 11 µs | 69 µs | 160,162 req/s |
| 2 | 5.7 µs | 5 µs | 12 µs | 236 µs | 157,439 req/s |
| 3 | 5.4 µs | 5 µs | 9 µs | 50 µs | 165,837 req/s |

一次更早的 Debug 构建为 avg 8.5 µs / p50 7 µs / p99 18 µs / 105,623 req/s，
可用作优化档位的对照。

## 3. 解释与边界

1. 数字包含完整路径：SOME/IP 头与载荷编码、`sendto`、`recvfrom`、SD/RPC 解析、处理器回显、
   响应编码；只省掉了跨主机网络与中断。
2. 忙轮询消除了调度抖动，因此 p99 接近 p50；真实事件循环（含 `poll`/`epoll` 唤醒）会更差。
3. 单进程共享 CPU 缓存，不能外推为跨进程或多核数据。
4. 目标板（ARM/RT-Thread）与 vsomeip 对端数据仍待环境验证，见
   [validation_lanes_zh.md](validation_lanes_zh.md)。

## 4. 后续

1. 在 ARM 目标板重复同一基准，记录主频与 `size` 结果。
2. Docker/vsomeip 可用后，补一条跨实现 RPC 时延对比。
