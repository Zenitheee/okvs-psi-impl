# volepsi2：OKVS + PSI 复现

本目录提供论文《Blazing fast PSI from improved OKVS and subfield VOLE》的独立 C++ 复现，目前包含：

- 第 2 章 OKVS 构造：使用 GF(2^128) 密集列、Paxos 风格三角化与回代生成向量 `P`；
- clustering 版 OKVS：按 bin 分桶并支持多线程编码/解码；
- 第 4 节半诚实 PSI 快速实例：基于 `Encode`/`Decode`、GF(2^128) 版 fast instantiation 和真实 silent VOLE 完成从 `P` 到最终交集输出的流程。

## 构建

在仓库根目录：

```bash
cmake -S . -B build -DOKVS_ENABLE_VOLEPSI=OFF
cmake --build build
```

生成的目标：

- `volepsi2_cli`：先验证 OKVS encode/decode，再运行一个小型 PSI 示例；
- `volepsi2_tests`：`tests/okvs_roundtrip.cpp`，覆盖多组规模与长 key；
- `gf128_square_test`：GF(2^128) 平方/求逆检查；
- `psi_test`：半诚实 PSI 回归测试，覆盖空集、全交、部分交、非均衡规模、clustered 多线程路径，以及真实 VOLE 后端是否启用。
- `volepsi2_demo`：本地 Web demo，提供“一键求交”界面，并实时展示各阶段耗时与网络流量图表。

## 测试

```bash
cmake --build build --target volepsi2_tests
cmake --build build --target psi_test
ctest --test-dir build
```

## Demo 界面

构建并启动 demo：

```bash
cmake --build build --target volepsi2_demo
./build/volepsi2_demo --port 8090
```

随后在浏览器打开 `http://127.0.0.1:8090`。界面支持：

- 一键运行半诚实 PSI demo；
- 实时显示 `Hash Mapping`、`OKVS Encoding`、`VOLE Generation`、`Correction Transfer`、`Intersection Calculation` 五个阶段；
- 以图表形式展示每个阶段的耗时和网络流量；
- 展示交集样本、接收方/发送方样本，以及是否启用 clustered OKVS、真实 silent VOLE 等运行信息。

说明：demo 现在采用真实的双方本地 socket 流程。silent VOLE 阶段继续使用 `coproto::LocalAsyncSocket`，校正向量与发送方 tag 也会通过独立的本地 socket 真实发送，因此界面中的网络流量全部来自实测字节数。

## 使用方式概览

```cpp
okvs::OkvsConfig cfg;
okvs::OkvsEncoder encoder(cfg, /*seed=*/0x12345678);

std::vector<std::string> keys = { "alice", "bob" };
std::vector<okvs::GF128> values = {
    okvs::GF128(0, 1),
    okvs::GF128(0, 2)
};

auto table = encoder.encode(keys, values);
auto decoded = encoder.decode("alice", table);
```

## 与论文的一致性

- 稀疏列长度默认取 `ceil(1.23 * n)`，可通过 `OkvsConfig::explicitSparseSize` 覆盖；
- 密集列数量默认等于 `securityParameter`，即论文中的 `λ`；
- 稀疏三角化及 gap 回填流程复用了 Paxos 风格的 `FC^{-1}` 计算与 GF(2^128) 稠密回代，不再显式构造大矩阵；
- 密集列采用 GF(2^128) 并构造 Vandermonde 行，以确保 `B'` 满秩的概率达到 “压倒性”。

## PSI 说明

- 当前实现对应论文 Figure 4 的 fast instantiation：`B = F = GF(2^128)`；
- `SemiHonestPsi` 现在通过 `libOTe` 的 silent VOLE 在本地 `coproto::LocalAsyncSocket` 上生成真实相关性 `(A, B, C, Δ)`，保持当前单进程 API 的同时不再使用本地随机模拟；
- 接收方输出是交集元素在接收方输入中的索引，便于和 `volepsi/tests/RsPsi_Tests.cpp` 的风格对齐。

## 后续工作

- 与原 `volepsi` 实现进行性能对比与联调；
- 为 PSI 添加结构化 benchmark 和 demo 界面；
- 继续推进 subfield-VOLE、DOKVS / circuit-PSI 等后续目标。
