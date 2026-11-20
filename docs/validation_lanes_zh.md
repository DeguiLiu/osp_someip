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

验收：148 个用例 / 44989 条断言全过；sanitizer 无报告；零分配进程返回 0；
`someip_headers_self_contained` 把 26 个公开头文件逐个单独编译，全部通过。

E2E Profile 4 的 CRC 另外做过一次交叉核对：用临时程序把 vsomeip 的 `crc.cpp` 与
`profile_04.cpp` 编进同一个进程，对 1000 组随机缓冲比较
`profile_04::compute_crc` 与 `Crc32Autosar` 的结果，差异为 0。该程序没有入库，也不作
测试 oracle；仓库里的期望值仍然只用标准目录的校验值 `0x1697D06A`。

Profile 7 用同样的办法核对过：把 vsomeip 的 `profile_07.cpp` 与 `protector.cpp` 编进临时程序，
对同一段载荷比较两者的 36 字节输出，逐字节一致（差异 0）。CRC-64/XZ 的校验值
`0x995DC9BBDF1939FA` 也先与标准目录核对过，然后才写进测试。

覆盖率只作趋势参考，不作为门禁：

```bash
cmake -S . -B build/coverage -DCMAKE_BUILD_TYPE=Debug -DSOMEIP_BUILD_TESTS=ON \
    -DCMAKE_CXX_FLAGS="--coverage -O0" -DCMAKE_EXE_LINKER_FLAGS="--coverage"
cmake --build build/coverage -j8 && ctest --test-dir build/coverage
gcovr -r . --filter 'include/osp/someip/' --txt-summary build/coverage
```

当前结果：行 59.0%、函数 73.0%、分支 37.5%。`server.hpp` / `client.hpp` 是模板，
每个 TU 各实例化一次、gcov 按实例计数，所以这两项偏低；单个头文件里 `header.hpp`、
`udp_link.hpp` 为 100%，`sd/option_view.hpp` 为 97%。

CI 同路径（干净目录、拉取上游 newosp 而不是用同级目录）也跑过一遍：

```bash
OSP_GITHUB_MIRROR=https://ghfast.top/ cmake -S . -B /tmp/fetch-check \
    -DSOMEIP_FETCH_NEWOSP=ON -DOSP_INCLUDE_DIR=/nonexistent
cmake --build /tmp/fetch-check -j8 && ctest --test-dir /tmp/fetch-check
```

结果 152 项全过。这条路径验证的是 `FetchContent` 拉下来的 newosp main 与本地同级目录等价。

发布状态复核：从 Gitee 重新 `git clone --depth 1` 到空目录，`cmake --preset host-debug
-DSOMEIP_FETCH_NEWOSP=ON` 后构建，152 项全过（148 个用例、44989 条断言）。仓库自包含，不依赖任何未提交文件。

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

E2E 互通（Python 探针 ←→ C++ 示例，Profile 1）：

```bash
./build-demo/examples/someip_e2e_server_demo --iface 127.0.0.1 --seconds 8 &
python3 tools/sd_probe.py --server 127.0.0.1 --service 1234 --method 0001 --e2e p1
# 探针输出：e2e check=ok counter=1，response payload=11223344

python3 tools/sd_probe.py --mode server --service 1234 --seconds 8 --e2e p1 &
./build-demo/examples/someip_e2e_client_demo --server 127.0.0.1 --local 127.0.0.1 \
    --port 30510 --service 1234 --method 0001 --seconds 5
# 客户端输出：response 0x00010001, 4 payload bytes；探针输出：e2e check=ok counter=0
```

两个方向都验证了“保护 → 校验 → 剥离”这条完整路径。注意探针的事件发布不带 E2E，
所以带 E2E 的客户端会把探针发出的事件判为校验失败并丢弃（输出 `received 0 event(s)`），
这是符合预期的行为。

SOME/IP-TP 互通（Python 探针 → C++ 服务端）：

```bash
./build-demo/examples/someip_server_demo --iface 127.0.0.1 --seconds 8 &
python3 tools/sd_probe.py --server 127.0.0.1 --service 1234 --method 0001 --tp-chunk 32
```

探针把 128 字节请求切成 4 个 32 字节分段发出，服务端重组后原样回显；
输出 `request sent as tp segments of 32`，响应载荷是 `00 01 02 … 7f` 的完整 128 字节。
这条链路说明重组实现能被一份独立的 Python 分段器驱动。

反方向同样验证过（Python 服务端 → C++ 客户端）：

```bash
python3 tools/sd_probe.py --mode server --service 1234 --seconds 8 --tp-chunk 32 &
./build-demo/examples/someip_client_demo --server 127.0.0.1 --local 127.0.0.1 \
    --port 30510 --service 1234 --method 0001 --seconds 5
```

探针把 128 字节应答拆成 4 个 TP 分段，客户端打印 `response 0x00010001, 128 payload bytes`，
服务端日志为 `request 0x0001 -> tp response of 128 bytes`。

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

同一份工具链文件也支持发行版安装的交叉编译器（CI 走这条路径）：

```bash
sudo apt install g++-aarch64-linux-gnu qemu-user
cmake -S . -B build/aarch64 \
    -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/aarch64-linux-gnu.cmake \
    -DCMAKE_BUILD_TYPE=Release -DSOMEIP_BUILD_TESTS=ON
cmake --build build/aarch64 -j8 && ctest --test-dir build/aarch64
```

两条路径都在本机各跑过一遍：133 项，1 项跳过（组播），其余全过。

### 4.1 完整内核（qemu-system）

qemu-user 只翻译指令与系统调用，调度、内核网络栈与用户态 init 都没参与。再上一层用
`qemu-system-aarch64` 启动完整 Linux：

- 内核：Alpine 3.20 `vmlinuz-virt`。它是 zimg 封装，先用脚本从文件中找到 gzip 载荷解出
  raw Image（偏移 0x38 处应为 `ARM\x64`），QEMU 才能用 `-kernel` 加载。
  这一步与 busybox 的下载已脚本化：

  ```bash
  tools/fetch_arm_vm_assets.sh build/arm-vm   # 官方 CDN 失败时自动切清华镜像
  ```

  产出 `build/arm-vm/Image` 与 `build/arm-vm/busybox.static`，都不入库。
- 用户态：busybox-static（apk 包里的 `bin/busybox.static`）加交叉工具链里的
  `ld-linux-aarch64.so.1`、`libc`、`libm`、`libgcc_s`、`libstdc++`。
- init：挂 proc/sys/dev，`ip link set lo up`，跑测试与基准，然后 `poweroff -f`。

```bash
AARCH64_TOOLCHAIN_ROOT=$HOME/opt/cross-aarch64/root \
tools/arm_vm.sh Image busybox.static \
    build/aarch64-tests/tests/someip_tests build/aarch64/examples/someip_loopback_bench

# 再把两个示例程序放进 initramfs，在 guest 内跑一次完整链路
AARCH64_TOOLCHAIN_ROOT=$HOME/opt/cross-aarch64/root \
tools/arm_vm.sh Image busybox.static \
    build/aarch64-tests/tests/someip_tests build/aarch64/examples/someip_loopback_bench \
    build/aarch64/examples/someip_server_demo build/aarch64/examples/someip_client_demo
```

实测（TCG，2 vCPU，1 GB）：

```
KERNEL: Linux 6.6.134-0-virt aarch64
test cases: 148 | 147 passed | 1 skipped        (组播用例需要真机网卡)
assertions: 44965 | 44965 passed
TESTS_RC=0
round_trips=3000 avg=91.3us p50=87us p99=186us rate=10482 req/s
BENCH_RC=0
```

比 qemu-user 慢一个数量级，符合 TCG 全系统模拟的预期。它的价值在于证明代码在完整内核、
真实 glibc 用户态与 initramfs 启动链下可运行。

带示例程序时，guest 内还会跑一次端到端链路（服务端后台启动，客户端发现、订阅、RPC、收事件）：

```
[client] service 0x1234/0x0001 available
[client] subscribe ack 0x1234/0x0001 eg=0x0001 ttl=10
[client] response 0x00010001, 4 payload bytes
[client] received 5 event(s)
DEMO_RC=0
```

结果：146 个用例通过，1 个跳过（组播），`someip_loopback_bench` 为
p50 11 µs、p99 24 µs、约 6.6 万次请求/秒。这是 qemu 用户态模拟，不是板上数据。

两个示例程序也在 qemu 下跑通了完整链路（等同板上冒烟的命令）：

```bash
qemu-aarch64 -L "$AARCH64_TOOLCHAIN_ROOT/usr/aarch64-linux-gnu" \
    build/aarch64/examples/someip_server_demo --iface 127.0.0.1 --service 1234 --seconds 8 &
qemu-aarch64 -L "$AARCH64_TOOLCHAIN_ROOT/usr/aarch64-linux-gnu" \
    build/aarch64/examples/someip_client_demo --server 127.0.0.1 --local 127.0.0.1 \
    --port 30510 --service 1234 --method 0001 --seconds 5
```

观察结果：客户端 `available` → `subscribe ack ttl=10` → `response 0x00010001` → 每秒一条
`event 0x8001`；服务端 `1 subscriber(s)`。

同一工具链再加 `-DSOMEIP_NO_EXCEPTIONS=ON` 再跑一遍：119 项全过，组播 1 项跳过，
说明无异常、无 RTTI 的目标配置在 aarch64 上同样成立。

真实板卡仍未验证：需要一块 aarch64 板子，把上面的命令换成板卡 IP 与 `tools/sd_probe.py`。

## 5. RT-Thread 编译检查（桩 SDK，已执行）

没有 RT-Thread SDK 与板卡，因此只做到编译期检查：`tools/rtthread_stub/rtthread.h` 声明
newosp 平台层用到的几个符号（`rt_tick_get`、`rt_thread_*`、`RT_TICK_PER_SECOND` 等），
然后用 `-DOSP_PLATFORM_RTTHREAD=1 -DOSP_HAS_NETWORK=0` 编译 24 个与 socket 无关的核心头文件。

```bash
tools/check_rtthread.sh g++ ../newosp/include
# 或：ctest --test-dir build/host-debug -R someip_rtthread_core_headers
```

结果：24 个头文件在 `-fno-exceptions -fno-rtti` 下全部通过。这项检查能发现核心代码
误用 POSIX 头（`<thread>`、`<chrono>`、`<sys/socket.h>`）。

lwIP 后端另外单独验证：下载 lwIP 2.2.0，用 `tools/lwip_port` 里的最小 `lwipopts.h` 与
`arch/cc.h` 做类型检查：

```bash
git clone --depth 1 --branch STABLE-2_2_0_RELEASE \
    https://ghfast.top/https://github.com/lwip-tcpip/lwip.git lwip
tools/check_lwip.sh g++ "$PWD/lwip" ../newosp/include
# 或：cmake -S . -B build/host-debug -DSOMEIP_LWIP_DIR="$PWD/lwip"
#     ctest --test-dir build/host-debug -R someip_lwip_socket_backend
```

结果：`osp/socket.hpp` 与 `udp_link.hpp` 在 `-DOSP_NET_BACKEND=1`、`-Wall -Wextra` 下零告警
编译通过。

仍**不能**证明：

- 收发、定时、中断上下文下的运行行为；
- 栈与中断占用，以及 lwIP 与 RT-Thread 的具体版本组合。

这些都需要 RT-Thread SDK 与板子，和板级实测一并列为待办。

## 6. 通道状态总表

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
| RT-Thread 核心头文件编译 | 完成（桩 SDK） | 24 个核心头文件通过 |
| lwIP socket 后端类型检查 | 完成（lwIP 2.2.0 头文件） | `socket.hpp` + `udp_link.hpp` 零告警；运行时未验证 |
