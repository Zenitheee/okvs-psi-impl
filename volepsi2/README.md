# volepsi2：OKVS + PSI 复现

本目录提供论文《Blazing fast PSI from improved OKVS and subfield VOLE》的独立 C++ 复现，目前包含：

- 第 2 章 OKVS 构造：使用 GF(2^128) 密集列、Paxos 风格三角化与回代生成向量 `P`；
- clustering 版 OKVS：按 bin 分桶并支持多线程编码/解码；
- 第 4 节半诚实 PSI 快速实例：基于 `Encode`/`Decode`、GF(2^128) 版 fast instantiation 和本地 VOLE 相关性模拟完成从 `P` 到最终交集输出的流程。

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
- `psi_test`：半诚实 PSI 回归测试，覆盖空集、全交、部分交、非均衡规模和 clustered 多线程路径。

## 测试

```bash
cmake --build build --target volepsi2_tests
cmake --build build --target psi_test
ctest --test-dir build
```

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
- `SemiHonestPsi` 直接在 `volepsi2` 内部模拟 VOLE 相关性 `(A, B, C, Δ)`，用于复现协议数据流与正确性，而不是替代 `volepsi` 中基于 libOTe 的真实网络/VOLE 实现；
- 接收方输出是交集元素在接收方输入中的索引，便于和 `volepsi/tests/RsPsi_Tests.cpp` 的风格对齐。

## 后续工作

- 与原 `volepsi` 实现进行性能对比与联调；
- 为 PSI 添加结构化 benchmark 和 demo 界面；
- 继续推进 subfield-VOLE、DOKVS / circuit-PSI 等后续目标。
