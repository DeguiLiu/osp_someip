# 验证通道与环境前置

结论：主机通道已全绿；vsomeip 互操作与 ARM 目标通道的操作步骤已固定，但当前机器缺少
Docker 守护进程与交叉工具链，`tools/verify_env.sh` 会直接报出缺口。

## 1. 主机通道（已执行）

```bash
cmake -S . -B build -DOSP_GITHUB_MIRROR=https://ghfast.top/
cmake --build build -j8
ctest --test-dir build --output-on-failure

cmake -S . -B build-noexc -DSOMEIP_NO_EXCEPTIONS=ON
cmake --build build-noexc -j8 && ctest --test-dir build-noexc

cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer"
cmake --build build-asan -j8
setarch $(uname -m) -R env ASAN_OPTIONS=detect_leaks=1 ./build-asan/tests/someip_tests

./build/tests/someip_no_alloc          # exit 0 表示零分配
```

验收：101 个用例 / 3699 条断言全过；sanitizer 无报告；零分配进程返回 0。

跨实现互通（已验证，双向）：

```bash
# C++ 服务端 ← Python 客户端
./build-demo/examples/someip_server_demo --iface 127.0.0.1 --seconds 6 &
python3 tools/sd_probe.py --server 127.0.0.1 --service 1234 --method 0001

# C++ 客户端 → Python 服务端
python3 tools/sd_probe.py --mode server --service 1234 --seconds 6 &
./build-demo/examples/someip_client_demo --server 127.0.0.1 --local 127.0.0.1 \
    --port 30510 --service 1234 --method 0001 --seconds 3
```

## 2. vsomeip 互操作通道（当前阻塞）

当前诊断（2026-11-20）：`dockerd` 正在运行、`docker.service` 为 active，但
`/var/run/docker.sock` 权限是 `root:docker 0660`，当前用户不在 `docker` 组，且无免密 sudo。
解除方式：

```bash
sudo usermod -aG docker "$USER"   # 之后重新登录
```

前置：

```bash
sudo ip link set lo multicast on
sudo ip route replace 239.255.0.255/32 dev lo
sudo ip addr replace 127.0.0.2/8 dev lo
export VSOMEIP_PEER_DIR=/path/to/simple_someip/tests/data/vsomeip-peer
docker info   # 必须成功
```

步骤：

```bash
docker build --network=host -t someip-peer "$VSOMEIP_PEER_DIR"
docker run --rm --network=host -i someip-peer
# stdin 命令示例：offer 1234 0001 1 0001 8001 - 0001
```

对本仓库的两个方向分别验证：

1. C++ 服务端 + vsomeip 客户端：`someip_server_demo` 运行后，peer 发 `require`、
   `subscribe`、`call`，期望收到 offer、Ack、响应与事件。
2. C++ 客户端 + vsomeip 服务端：peer 发 `offer`，`someip_client_demo --method 0001`
   期望打印 available、ack、response 与 event。

验收：双向四条消息（Offer/Subscribe/Ack/Request-Response）与事件载荷逐字节一致，
失败时用 `tcpdump -i any udp port 30490 or port 30509` 抓包定位。

## 3. ARM 目标通道（当前阻塞）

前置：`arm-linux-gnueabihf-g++` 或 `aarch64-linux-gnu-g++`，以及可运行的目标板。

当前诊断：本机只有 `arm-none-eabi-g++`（裸机 newlib）。用它对 `server.hpp` 做
`-fsyntax-only` 会停在 newosp `platform.hpp` 的 `std::this_thread`，说明需要带线程/系统调用的
Linux ARM sysroot 或 RT-Thread 专用工具链；`qemu-aarch64` 虽在，但没有对应的 aarch64 编译器。

`cmake/toolchains/arm-linux-gnueabihf.cmake` 示例：

```cmake
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR arm)
set(CMAKE_C_COMPILER arm-linux-gnueabihf-gcc)
set(CMAKE_CXX_COMPILER arm-linux-gnueabihf-g++)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
```

构建与度量：

```bash
cmake -S . -B build-arm -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/arm-linux-gnueabihf.cmake \
    -DSOMEIP_BUILD_TESTS=OFF -DSOMEIP_BUILD_EXAMPLES=ON
cmake --build build-arm -j8
arm-linux-gnueabihf-size build-arm/examples/someip_server_demo
```

板上冒烟：运行 `someip_server_demo --iface <板卡 IP> --seconds 10`，主机运行
`tools/sd_probe.py --server <板卡 IP>`，要求发现、订阅、RPC、事件四项全通。

验收：`size` 的 text+data 记录在案；吞吐与时延用 `--seconds 30` 的稳定计数估算，
数据写入 `docs/benchmark_report_zh.md`（待建）。

## 4. 通道状态总表

| 通道 | 状态 | 阻塞原因 |
|---|---|---|
| 主机单测 / 无异常 / sanitizer / 零分配 | 完成 | — |
| 双进程 C++ demo | 完成 | — |
| Python 探针双向互通 | 完成 | — |
| vsomeip 容器互操作 | 待执行 | 用户不在 docker 组（socket `root:docker 0660`） |
| ARM 交叉构建与板上冒烟 | 待执行 | 仅 `arm-none-eabi`，无 Linux ARM sysroot 或 RT-Thread SDK |
