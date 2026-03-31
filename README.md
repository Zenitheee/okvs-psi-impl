# Blazing Fast PSI (CCS '22) Reproduction

本仓库是对论文 *Blazing Fast PSI from Improved OKVS and Subfield VOLE* 的复现，作为武汉大学本科毕业设计项目。当前状态不是“想做什么”，而是“已经做了什么并如何复现”。

## 当前状态

- 已完成 `volepsi2` 中的 OKVS、clustered OKVS、半诚实 PSI fast instantiation 以及本地 Web UI 演示。
- `volepsi2` 已支持独立构建：优先使用已安装的 `libOTe`，找不到时回退到仓库内 vendored `thirdparty` 依赖。
- 已补充本地结构化 benchmark 结果，见 [EVALUATION.md](./EVALUATION.md)。
- 最终展示形态是本地 Web UI，不是命令行输出；除内置 synthetic demo 外，也支持粘贴或导入自定义数据集并在后端完成预处理。

## 已实现范围

`volepsi2` 当前覆盖的内容：

1. 论文 Figure 1 对应的 OKVS `Encode` / `Decode`。
2. clustered OKVS，支持按 bin 编码与多线程解码。
3. 论文 Figure 4 的半诚实 PSI fast instantiation，其中 `B = F = GF(2^128)`。
4. 基于 `libOTe` silent VOLE 的真实本地相关性生成，而不是纯随机模拟。
5. 本地 Web UI，可展示协议阶段、耗时、网络流量和交集样本，并支持自定义数据集输入、去重和基础预处理。

当前**未实现**或**未作为最终成果交付**的内容：

- 论文中的 low-communication subfield-VOLE 变体。
- DOKVS / circuit PSI / malicious PSI。
- 与论文 Table 2 在相同硬件、相同网络环境下的一比一绝对数值复现。

## 仓库结构

- [`paper.md`](./paper.md): 论文文本，作为复现参照。
- [`volepsi2/`](./volepsi2): 当前毕业设计的主实现、测试、benchmark 和 Web UI。
- [`EVALUATION.md`](./EVALUATION.md): 已记录的 benchmark 命令、结果，以及与上游 `volepsi` 历史基线的比较。

## 从零开始的可复现构建

以下命令从仓库根目录执行。

### 1. 配置并构建 `volepsi2`

`volepsi2` 会先尝试 `find_package(libOTe)`。如果你已经安装了 `libOTe`，可通过 `CMAKE_PREFIX_PATH` 或 `libOTe_DIR` 指向它；如果没有，CMake 会自动回退到仓库内 vendored 的 `thirdparty/libOTe`、`thirdparty/coproto`、`thirdparty/macoro` 和 `thirdparty/function2`。

如果你想强制始终使用 vendored 路径：

```bash
cmake -S volepsi2 -B volepsi2/build -DVOLEPSI2_USE_VENDORED_LIBOTE=ON
```

如果你想显式使用外部已安装的 `libOTe`：

```bash
cmake -S volepsi2 -B volepsi2/build \
  -DCMAKE_PREFIX_PATH=/path/to/libote/prefix
```

然后编译：

```bash
cmake --build volepsi2/build --target core_test psi_test volepsi2_bench volepsi2_demo
```

说明：vendored 路径当前仍需要系统可发现的 `libsodium` 开发文件。

### 2. 运行回归测试

```bash
ctest --test-dir volepsi2/build --output-on-failure
```

当前测试分成两层：

- `core_test`: 内部后端回归，集中检查 GF(2^128)、OKVS 和 clustered OKVS。
- `psi_test`: 与 demo 协议路径一致的端到端 PSI 回归。

### 3. 启动最终展示用的 Web UI

```bash
./volepsi2/build/volepsi2_demo --port 8090
```

浏览器打开 `http://127.0.0.1:8090`。

## Web UI 展示内容

Web UI 会展示：

- synthetic demo 的接收方/发送方规模、线程数、聚类阈值等输入参数。
- 直接粘贴或导入接收方与发送方数据集，并在后端进行分隔、去重和空白过滤。
- `Hash Mapping`、`OKVS Encoding`、`VOLE Generation`、`Correction Transfer`、`Intersection Calculation` 五个协议阶段。
- 每个阶段的耗时与网络流量。
- 最终交集规模、交集样本、发送方/接收方样本。
- 当前是否启用 clustered OKVS、是否启用真实 silent VOLE。

这部分是最终答辩时最直接的演示界面。

## 本地结构化评测结论

完整命令和结果见 [EVALUATION.md](./EVALUATION.md)。这里给出摘要：

- `volepsi2` 已经具备可重复运行的 OKVS / PSI benchmark，不再只是“以后再测”。
- 在 `n = 2^12` 的本地测试中，clustered OKVS 在 4 线程下将 OKVS 总时间从 `80.365 ms` 降到 `61.795 ms`，说明 clustering 在当前实现中已经产生可观收益。
- 在 `n = 2^10`、`2^12`、`2^14` 的本地 PSI 测试中，`volepsi2` 的绝对运行时间仍明显慢于历史记录中的上游 `volepsi` 基线，因此本项目当前的定位是“功能性复现 + 可展示实现”，而不是“性能追平上游”。
- 论文在 `n = 2^20` 下报告了更强的多线程收益；本仓库当前 README 中记录的是本地机器上的小规模结构化评测，目的是给答辩和复现提供可重复证据，而不是冒充论文原始实验环境。

## 与论文的差异和限制

- 当前实现复现的是 Figure 4 的 fast instantiation，不是 low-communication subfield-VOLE 版本。
- Web UI 里的通信统计来自本地 socket 与 `coproto::LocalAsyncSocket`，适合展示协议阶段，但不等于真实跨机网络环境。
- `volepsi2` 的代码优先级是可读性、模块清晰度和前端可展示性，因此没有像上游 `volepsi` 那样做同等程度的内核级性能优化。
- 这意味着当前仓库适合作为本科毕业设计成果展示和论文复现讲解，但不应把它表述成“已经在性能上等价于上游参考实现”。

## 推荐阅读顺序

1. 看 [`volepsi2/README.md`](./volepsi2/README.md) 了解实现和运行方式。
2. 看 [EVALUATION.md](./EVALUATION.md) 了解本地 benchmark 命令和结果。
3. 启动 `volepsi2_demo`，直接从 Web UI 演示协议流程。
4. 需要算法细节时，再回到 [`paper.md`](./paper.md) 对照 Figure 1 和 Figure 4。
