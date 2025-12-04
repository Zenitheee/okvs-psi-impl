# volepsi2：OKVS 第 2 章复现

本目录提供论文《Blazing fast PSI from improved OKVS and subfield VOLE》第二章 OKVS 构造的独立 C++ 实现。实现目标：

- 使用 GF(2^128) 作为密集列所依赖的域，并采用 Vandermonde 结构保证满秩概率；
- 对给定的一组键值对构造线性方程组并求解，生成向量 `P`；
- 提供编码与解码接口，附带示例程序和随机用例的测试。

## 构建

在仓库根目录：

```bash
cmake -S . -B build -DOKVS_ENABLE_VOLEPSI=OFF
cmake --build build
```

生成的目标：

- `volepsi2_cli`：简单示例程序，生成随机键值对并验证编码/解码；
- `volepsi2_tests`：随机用例测试，可通过 `ctest` 运行。

## 测试

```bash
cmake --build build --target volepsi2_tests
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
- 密集列采用 GF(2^128) 并构造 Vandermonde 行，以确保 `B'` 满秩的概率达到 “压倒性”。

## 后续工作

- 精确实现论文中的三角化过程及 gap 处理；
- 与原 `volepsi` 实现进行性能对比与联调。

