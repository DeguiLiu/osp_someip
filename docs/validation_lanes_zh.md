# 验证通道与环境前置

结论：主机、组播、aarch64（qemu）与 vsomeip 双向互操作四条通道已执行并记录结果；
仅剩板级冒烟需要硬件，`tools/verify_env.sh` 会报出缺什么。

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

验收：118 个用例 / 3846 条断言全过；sanitizer 无报告；零分配进程返回 0。

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

订阅续订实测：Python 服务端运行 15 s、C++ 客户端运行 12 s，服务端记录 3 次
`subscribe from`（初始 + TTL/2 两次续订），客户端收到 12 条事件；offer TTL 到期后客户端
按预期打印 `service ... gone`。

## 2. 组播通道（已执行）

入组不需要 root，因此用真实网卡直接验证，不依赖 lo 的 MULTICAST 标志：

```bash
cmake -S . -B build/host-debug -DSOMEIP_TEST_MCAST_IFACE=172.16.48.122
cmake --build build/host-debug -j8
ctest --test-dir build/host-debug
```

`tests/test_multicast.cpp` 在指定接口上加入 239.255.0.255，用第二个套接字以 `IP_MULTICAST_IF`
发一条 FindService，要求入组套接字收到同样的字节，并解析出 `0x1234` 的 FindService 条目。
未设置 `SOMEIP_TEST_MCAST_IFACE` 时该用例跳过，CTest 通过 `SKIP_RETURN_CODE=4` 记为跳过而不是失败。

验收：`ens192`（172.16.48.122）上通过，24 条断言。qemu-user 不实现组播回环，
aarch64 通道中该用例为跳过状态，不能当作板上验证。

## 3. vsomeip 互操作通道（已执行）

本机账号不在 docker 组，因此没有用容器。替代做法是把两套栈放进各自的网络命名空间，用 veth
对连，效果等同 Docker 的 host 网络之外的双机部署：两边各有地址，各自绑定 30490 不冲突。
用户命名空间里已经具备建 veth、配地址、开组播的权限，不需要 root。

准备 vsomeip 3.4.10（Boost 1.74 同样从 Ubuntu 交叉包解包，不需要安装到系统）：

```bash
git clone --depth 1 --branch 3.4.10 https://ghfast.top/https://github.com/COVESA/vsomeip.git
cmake -S vsomeip -B vsomeip-build -DCMAKE_BUILD_TYPE=Release \
    -DBOOST_ROOT=$HOME/opt/boost/root/usr -DBoost_NO_SYSTEM_PATHS=ON
cmake --build vsomeip-build -j8
# peer 程序来自 Rust 原型的 tests/data/vsomeip-peer
cmake -S simple_someip/tests/data/vsomeip-peer -B peer-build \
    -Dvsomeip3_DIR=$(pwd)/vsomeip-build
cmake --build peer-build -j8
```

运行（脚本内部完成 netns、veth、配置与命令序列）：

```bash
export VSOMEIP_PEER_BIN=$PWD/peer-build/peer
export VSOMEIP_LIB_DIR=$PWD/vsomeip-build
tools/vsomeip_interop.sh server 12   # 本仓库服务端 ← vsomeip 客户端
tools/vsomeip_interop.sh client 10   # 本仓库客户端 → vsomeip 服务端
```

实测结果（2026-11-20，vsomeip 3.4.10）：

| 方向 | 观察到的消息 |
|---|---|
| 服务端 | vsomeip 收到 `AVAILABLE`、`SUBSCRIPTION ... status=accepted`、6 条 `EVENT ... 0x8001`、`RESPONSE ... payload=deadbeef`；本端 `1 subscriber(s)` 与 `request 0x0001 from 10.0.0.2` |
| 客户端 | 本端打印 `service 0x1234/0x0001 available`、`subscribe ack ttl=10`、`response 0x00010001`、`event 0x8001`；vsomeip 日志出现 `REQUEST ... payload=01020304` |

过程中修掉的两个真问题：

1. 客户端原先只开一个套接字（本地端口），收不到发往 30490 的组播 Offer；现在 SD 与服务各用一个
   套接字，`client_demo --multicast` 才会入组。服务端同理需要 `IP_MULTICAST_IF` 指定出口。
2. 同一来源的会话号会回退：vsomeip 对 FindService 的应答重新从 1 编号，与周期 Offer 的计数交错。
   重启判定因此改为“会话号回到 1 且距上一条报文至少 2 s”，避免把普通应答当成对端重启。

仍未覆盖：Docker 镜像方式、带 TLS 的 vsomeip、以及跨真实交换机的组播。

## 4. ARM 目标通道（已执行，qemu）

本机没有现成的 aarch64 工具链，也没有 root。做法是从 Ubuntu 镜像下载交叉包并解包到用户目录，
再用 `qemu-aarch64` 运行：

```bash
mkdir -p ~/opt/cross-aarch64/pkgs && cd ~/opt/cross-aarch64/pkgs
apt-get download gcc-11-aarch64-linux-gnu g++-11-aarch64-linux-gnu cpp-11-aarch64-linux-gnu \
    binutils-aarch64-linux-gnu libgcc-11-dev-arm64-cross libstdc++-11-dev-arm64-cross \
    libc6-dev-arm64-cross linux-libc-dev-arm64-cross libc6-arm64-cross \
    libstdc++6-arm64-cross libgcc-s1-arm64-cross
for d in *.deb; do dpkg-deb -x "$d" ../root; done

export AARCH64_TOOLCHAIN_ROOT=$HOME/opt/cross-aarch64/root
export LD_LIBRARY_PATH=$AARCH64_TOOLCHAIN_ROOT/usr/lib/x86_64-linux-gnu:$LD_LIBRARY_PATH

cmake -S . -B build/aarch64-tests \
    -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/aarch64-linux-gnu.cmake \
    -DCMAKE_BUILD_TYPE=Release -DSOMEIP_BUILD_TESTS=ON
cmake --build build/aarch64-tests -j8
ctest --test-dir build/aarch64-tests
```

`cmake/toolchains/aarch64-linux-gnu.cmake` 把 `CMAKE_CROSSCOMPILING_EMULATOR` 设为
`qemu-aarch64 -L <root>/usr/aarch64-linux-gnu`，CTest 因此可以直接运行目标二进制。
`LD_LIBRARY_PATH` 是解包后的 binutils 依赖，只在构建时需要。

结果：117 个用例通过，1 个跳过（组播），`someip_loopback_bench` 为
p50 11 µs、p99 24 µs、约 6.6 万次请求/秒。这是 qemu 用户态模拟，不是板上数据。

板上冒烟仍未执行：需要一块 aarch64 板卡，运行 `someip_server_demo --iface <板卡 IP>`，
主机跑 `tools/sd_probe.py --server <板卡 IP>`，要求发现、订阅、RPC、事件四项全通。

## 5. 通道状态总表

| 通道 | 状态 | 阻塞原因 |
|---|---|---|
| 主机单测 / 无异常 / sanitizer / 零分配 | 完成 | — |
| 双进程 C++ demo | 完成 | — |
| Python 探针双向互通 | 完成 | — |
| 组播（真实网卡） | 完成 | `ens192`，24 条断言 |
| aarch64 交叉构建 + qemu | 完成 | 113 过 / 1 跳过（组播），基准见 `benchmark_report_zh.md` |
| vsomeip 互操作（netns + veth） | 完成 | 双向通过，见第 3 节 |
| vsomeip 容器方式 | 未执行 | 用户不在 docker 组，已被 netns 方式替代 |
| ARM 板卡冒烟 | 待执行 | 没有目标板 |
