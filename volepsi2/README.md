# volepsi2：OKVS + PSI 复现实现

`volepsi2` 是本仓库中用于毕业设计答辩和最终展示的主目录。它不是 `volepsi` 的简单封装，而是围绕论文核心路径重新组织的一套独立实现，并额外提供了本地 Web UI。

## 这个目录里有什么

- OKVS `Encode` / `Decode` 实现。
- clustered OKVS。
- 基于 GF(2^128) 与 silent VOLE 的半诚实 PSI fast instantiation。
- 本地 benchmark 工具。
- 两个清晰的内部回归测试目标。
- 本地 Web UI demo。

## 构建方式

从仓库根目录执行。

### 前置条件

`volepsi2` 现在支持两种依赖来源，优先级如下：

1. 先尝试 `find_package(libOTe)`，使用系统中或你指定前缀中的已安装 `libOTe`。
2. 如果没找到，则自动回退到仓库内 vendored 的 `thirdparty/libOTe` / `thirdparty/coproto` / `thirdparty/macoro` / `thirdparty/function2`。

如果走 vendored 路径，目前仍需要系统可发现的 `libsodium` 开发文件。

如果 vendored 依赖不在默认位置，可用 `-DVOLEPSI2_THIRDPARTY_DIR=/path/to/thirdparty` 指向替代目录。

### 方式一：使用外部已安装的 libOTe

如果你已经有可用的 `libOTe` 安装前缀，配置时传入 `CMAKE_PREFIX_PATH` 即可：

```bash
cmake -S volepsi2 -B volepsi2/build \
  -DCMAKE_PREFIX_PATH=/path/to/libote/prefix
```

也可以直接指定 `libOTe_DIR`：

```bash
cmake -S volepsi2 -B volepsi2/build \
  -DlibOTe_DIR=/path/to/lib/cmake/libOTe
```

### 方式二：使用仓库内 vendored fallback

如果没有外部安装，直接配置即可；CMake 会先尝试 `find_package(libOTe)`，失败后自动使用 `volepsi2/thirdparty`：

```bash
cmake -S volepsi2 -B volepsi2/build
```

如果你想强制忽略外部安装、始终走 vendored 路径：

```bash
cmake -S volepsi2 -B volepsi2/build \
  -DVOLEPSI2_USE_VENDORED_LIBOTE=ON
```

如果你想在当前机器上重新开启更激进的本地 CPU 调优，可额外传入：

```bash
cmake -S volepsi2 -B volepsi2/build \
  -DVOLEPSI2_USE_NATIVE_ARCH=ON
```

默认关闭 `-march=native`，便于在不同答辩机器之间迁移构建。

当前 GF(2^128) 与行哈希 fast path 依赖 x86 AES / PCLMUL 指令，`VOLEPSI2_ENABLE_X86_CRYPTO_FLAGS` 默认开启并添加 `-maes -mpclmul`。只有在工具链已经提供等价编译选项，或准备移植这条 fast path 时，才应关闭该选项。

### 编译

```bash
cmake --build volepsi2/build --target core_test psi_test volepsi2_bench volepsi2_demo volepsi2_cli
```

## 生成的主要目标

- `core_test`: 内部后端回归测试，集中覆盖 GF(2^128) 基本性质、OKVS encode/decode，以及 clustered OKVS round-trip。
- `psi_test`: 半诚实 PSI 端到端回归测试；该路径与 demo 使用的协议流程保持一致。
- `volepsi2_bench`: 本地 benchmark，可测 OKVS 与 PSI。
- `volepsi2_demo`: 最终展示用的 Web UI 服务端。
- `volepsi2_cli`: 小型命令行示例，便于快速验证基础流程。

## 测试

```bash
ctest --test-dir volepsi2/build --output-on-failure
```

当前测试策略不是堆很多分散的小目标，而是保留两类真正有用的验证：

1. `core_test` 负责后端基础正确性。
2. `psi_test` 负责和 Web UI 一致的协议路径。

如果需要额外做一层随机化压力验证：

```bash
./volepsi2/build/volepsi2_bench stress -t 20 -nt 4 -bs 2048
```

该模式会在 `n = 2^10`、`2^12`、`2^14` 上重复检查 OKVS round-trip 和 PSI correctness，最新记录见 [../EVALUATION.md](../EVALUATION.md)。

## 启动 Web UI

```bash
./volepsi2/build/volepsi2_demo --port 8090
```

浏览器打开 `http://127.0.0.1:8090`。

Web UI 支持：

- 一键运行半诚实 PSI demo。
- 实时显示 `Hash Mapping`、`OKVS Encoding`、`VOLE Generation`、`Correction Transfer`、`Intersection Calculation` 五个阶段。
- 以图表形式展示每个阶段的耗时和网络流量。
- 展示交集样本、发送方/接收方样本、clustered OKVS 开关状态和 VOLE 后端信息。

说明：demo 采用真实的本地 socket 流程。silent VOLE 阶段仍通过 `coproto::LocalAsyncSocket` 产生本地通信；校正向量和发送方标签也通过本地 socket 传输，因此界面中的流量统计来自真实测量而不是拍脑袋估算。

当前 demo 限制 synthetic 或上传后的单侧集合规模不超过 `2^20`，请求体不超过 `64 MiB`。自定义数据上传后会生成一个 15 分钟有效的一次性本地 session，随后通过 `/api/run` 的 SSE 流执行协议。

## Benchmark 用法

### OKVS

```bash
./volepsi2/build/volepsi2_bench okvs -nn 12 -t 3 -nt 1
./volepsi2/build/volepsi2_bench okvs -nn 12 -t 3 -nt 4
./volepsi2/build/volepsi2_bench okvs -nn 12 -t 3 -nt 4 -bs 2048
```

### PSI

```bash
./volepsi2/build/volepsi2_bench psi -nn 12 -t 3 -nt 1
./volepsi2/build/volepsi2_bench psi -nn 12 -t 3 -nt 4
./volepsi2/build/volepsi2_bench psi -nn 12 -t 3 -nt 4 -bs 2048
```

已记录的结果和与 `volepsi` 的比较见 [../EVALUATION.md](../EVALUATION.md)。

## 与论文的一致性

- 稀疏列长度默认取 `ceil(1.23 * n)`，可通过 `OkvsConfig::explicitSparseSize` 覆盖。
- 密集列数量默认等于 `securityParameter`，即论文中的 `λ`。
- 稀疏三角化与 gap 回填复用了 Paxos 风格的 `FC^{-1}` 计算和 GF(2^128) 稠密回代，不显式构造大矩阵。
- PSI 对应论文 Figure 4 的 fast instantiation：`B = F = GF(2^128)`。

## 当前边界

`volepsi2` 当前没有声称完成以下内容：

- subfield-VOLE 低通信量变体。
- circuit PSI / DOKVS / malicious PSI。
- 在性能上追平 `volepsi` 上游实现。

所以它的定位很明确：这是一个为了毕业设计而组织的、可讲解、可运行、可演示的复现实现。
