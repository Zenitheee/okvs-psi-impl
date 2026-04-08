# Local Evaluation

本文件记录当前仓库里已经实际运行过的 benchmark 命令和结果，用来回答两个问题：

1. `volepsi2` 现在到底跑到了什么程度？
2. 它和上游参考实现 `volepsi` 相比处于什么位置？

所有命令都从仓库根目录执行，结果初始记录于 2026-03-27，并于 2026-03-31 在重新构建 `volepsi2_bench` 后整表重测，随后于 2026-04-01 增补随机化压力验证。本表于 2026-04-08 重新配置为 `CMAKE_BUILD_TYPE=Release` 后整表重测。由于硬件、编译器、系统负载和网络环境都与论文不同，这里的数据**不是**论文 Table 2 的绝对复现，而是当前项目的本地结构化证据。

## Commands

### Release build used for the 2026-04-08 run

```bash
cmake -S volepsi2 -B volepsi2/build -DCMAKE_BUILD_TYPE=Release
cmake --build volepsi2/build --target core_test psi_test volepsi2_bench volepsi2_demo
```

### `volepsi2` OKVS

```bash
./volepsi2/build/volepsi2_bench okvs -nn 12 -t 3 -nt 1
./volepsi2/build/volepsi2_bench okvs -nn 12 -t 3 -nt 4
./volepsi2/build/volepsi2_bench okvs -nn 12 -t 3 -nt 1 -bs 2048
./volepsi2/build/volepsi2_bench okvs -nn 12 -t 3 -nt 4 -bs 2048
```

### `volepsi2` PSI

```bash
./volepsi2/build/volepsi2_bench psi -nn 10 -t 3 -nt 1
./volepsi2/build/volepsi2_bench psi -nn 10 -t 3 -nt 4
./volepsi2/build/volepsi2_bench psi -nn 12 -t 3 -nt 1
./volepsi2/build/volepsi2_bench psi -nn 12 -t 3 -nt 4
./volepsi2/build/volepsi2_bench psi -nn 12 -t 3 -nt 1 -bs 2048
./volepsi2/build/volepsi2_bench psi -nn 12 -t 3 -nt 4 -bs 2048
./volepsi2/build/volepsi2_bench psi -nn 14 -t 3 -nt 1
./volepsi2/build/volepsi2_bench psi -nn 14 -t 3 -nt 4
./volepsi2/build/volepsi2_bench psi -nn 20 -t 3 -nt 1
./volepsi2/build/volepsi2_bench psi -nn 20 -t 3 -nt 4
```

### `volepsi2` randomized stress validation

```bash
./volepsi2/build/volepsi2_bench stress -t 20 -nt 4 -bs 2048
```

### Upstream `volepsi` Release PSI baseline

这些数值在 `/home/zen/Project/okvs-psi-origin/volepsi` 中使用 Release 构建的 upstream `volepsi` frontend 重测。每一行使用一次独立的 `-t 1 -v` 运行；没有使用 upstream frontend 的 `-t 3`，因为该循环在第一次协议执行后会复用已经初始化的协议状态，不能直接作为独立运行平均值与 `volepsi2_bench -t 3` 对比。

命令模板如下，其中 `-nn`、`-nt` 和可选 `-bs 2048` 按表格行替换：

```bash
/home/zen/Project/okvs-psi-origin/volepsi/out/build/linux/frontend/frontend -perf -psi -nn 12 -t 1 -nt 4 -v
```

## Results

### `volepsi2` OKVS at `n = 2^12`

| Mode | Threads | clustering 优化 | Table Size | Encode Avg (ms) | Decode Avg (ms) | Total Avg (ms) |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| OKVS | 1 | no | 5079 | 4.642 | 2.973 | 7.615 |
| OKVS | 4 | no | 5079 | 4.710 | 1.835 | 6.545 |
| OKVS | 1 | yes (`-bs 2048`) | 5710 | 4.795 | 3.905 | 8.699 |
| OKVS | 4 | yes (`-bs 2048`) | 5710 | 5.506 | 1.665 | 7.171 |

观察：

- 在当前 Release 结果中，最佳 OKVS 配置是未启用 clustering 优化 + 4 threads，总时间为 `6.545 ms`；`-bs 2048` 的聚类路径在这个规模下没有更快。

### `volepsi2` PSI size sweep

| Set Size | Threads | clustering 优化 | Total Avg (ms) | OKVS Size |
| --- | ---: | ---: | ---: | ---: |
| `2^10 = 1024` | 1 | no | 27.415 | 1300 |
| `2^10 = 1024` | 4 | no | 27.028 | 1300 |
| `2^12 = 4096` | 1 | no | 36.776 | 5079 |
| `2^12 = 4096` | 4 | no | 34.383 | 5079 |
| `2^12 = 4096` | 1 | yes (`-bs 2048`) | 39.176 | 5710 |
| `2^12 = 4096` | 4 | yes (`-bs 2048`) | 35.711 | 5710 |
| `2^14 = 16384` | 1 | no | 84.744 | 20193 |
| `2^14 = 16384` | 4 | no | 69.007 | 20193 |
| `2^20 = 1048576` | 1 | yes (default `bin_size_hint = 16384`) | 5770.794 | 1372928 |
| `2^20 = 1048576` | 4 | yes (default `bin_size_hint = 16384`) | 3365.752 | 1372928 |

观察：

- 当前 `volepsi2` 已能稳定完成 `2^10` 到 `2^20` 的本地 PSI benchmark。
- `n = 2^20` 时默认 `bin_size_hint = 16384` 会触发 clustering 优化 OKVS，因此这两条数据反映的是默认配置下的完整 PSI 路径，而不是强行关闭 clustering 的结果。
- 对完整 PSI 路径而言，多线程在 `2^10`、`2^12`、`2^14` 和 `2^20` 上都带来收益；其中 `n = 2^20` 时总时间从 `5770.794 ms` 降到 `3365.752 ms`。
- 在当前 Release 实现里，`n = 2^12` 的完整 PSI 路径并没有因为 `-bs 2048` 受益；聚类路径的主要价值仍体现在默认大规模输入会自动分箱，避免单个 OKVS 过大。

### `volepsi2` randomized stress validation

该模式会在 `n = 2^10`、`2^12`、`2^14` 上重复执行两类检查：

- 随机 OKVS encode/decode round-trip。
- 带真实交集的 PSI correctness check。

本次记录命令为：

```bash
./volepsi2/build/volepsi2_bench stress -t 20 -nt 4 -bs 2048
```

| Set Size | Trials | clustering 优化 OKVS | clustering 优化 PSI | OKVS Failures | PSI Failures | OKVS Avg Total (ms) | PSI Avg Total (ms) |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| `2^10 = 1024` | 20 | no | no | 0 | 0 | 1.945 | 28.748 |
| `2^12 = 4096` | 20 | yes | yes | 0 | 0 | 5.647 | 35.737 |
| `2^14 = 16384` | 20 | yes | yes | 0 | 0 | 14.398 | 64.222 |

观察：

- 在这 60 组随机 OKVS round-trip 和 60 组随机 PSI correctness check 中，当前实现都没有观察到失败。
- `-bs 2048` 让 `2^12` 和 `2^14` 的压力验证路径都实际经过 clustering 优化 OKVS，而 `2^10` 保持未聚类路径，因此这组验证覆盖了两种编码分支。
- 这里的 PSI correctness check 仍走 `volepsi2_bench stress` 的 benchmark-style 路径，也就是 silent VOLE 为真实本地相关性生成，但校正向量与标签传输不经过 demo 的 local socket UI 路径；后者仍由 `psi_test` 里的 `local_socket_flow` 用例负责覆盖。

### Upstream `volepsi` Release PSI baseline

| Set Size | Threads | clustering 优化 Flag | `volepsi2` Release Avg (ms) | upstream `volepsi` Release (ms) | `volepsi2` / upstream |
| --- | ---: | --- | ---: | ---: | ---: |
| `2^10 = 1024` | 1 | default | 27.415 | 30.488 | 0.899x |
| `2^10 = 1024` | 4 | default | 27.028 | 29.635 | 0.912x |
| `2^12 = 4096` | 1 | default | 36.776 | 30.727 | 1.197x |
| `2^12 = 4096` | 4 | default | 34.383 | 30.936 | 1.111x |
| `2^12 = 4096` | 1 | `-bs 2048` | 39.176 | 30.151 | 1.299x |
| `2^12 = 4096` | 4 | `-bs 2048` | 35.711 | 32.837 | 1.088x |
| `2^14 = 16384` | 1 | default | 84.744 | 34.004 | 2.492x |
| `2^14 = 16384` | 4 | default | 69.007 | 33.975 | 2.031x |
| `2^20 = 1048576` | 1 | default | 5770.794 | 836.613 | 6.898x |
| `2^20 = 1048576` | 4 | default | 3365.752 | 466.499 | 7.215x |

## Interpretation

- 从绝对性能看，当前 `volepsi2` 在 `2^10` 上略快于 upstream `volepsi`，在 `2^12` 上同一量级但略慢，在 `2^14` 和 `2^20` 上明显慢于 upstream `volepsi`。
- 从趋势上看，当前实现能观察到论文强调的一部分现象：多线程在较大输入下会有帮助；但 `n = 2^12` 的 clustering 优化在当前本地端到端 PSI benchmark 中没有带来收益。
- 因为 upstream `volepsi` 与当前 `volepsi2` 的 benchmark 前端并不完全等价，在答辩或论文中更合适的表述是：
  - “我们完成了协议与数据结构的可运行复现，并给出了本地结构化评测。”
  - “Release 构建下的当前实现可运行并覆盖到 `2^20`，小规模结果接近 upstream 基线，但大规模性能仍明显落后。”

## Caveats

- 上游 `volepsi` 与 `volepsi2` 的 benchmark 前端并不完全等价，尤其在实现层级、优化程度和统计方式上存在差异，因此这里的比较用于项目定位，不应写成严格公平竞赛。
- 当前 `volepsi2_bench psi` 输出的 `claim_scope=benchmark_fallback` / `modeled_transfers=1` 表明：silent VOLE 字节是本地实测，但校正向量和 sender tag 是建模值。这与 Web UI 里的本地 socket demo 路径不是同一种流量口径。
- 论文 Table 2 的硬件和网络环境与当前仓库不同，所以这里只能做趋势性对照，不能硬说“已经复现论文绝对数值”。
- 如果后续需要把这份结果写入毕业论文正文，建议把本文件中的表格直接作为“本地复现评测”小节，并明确注明测试环境和限制。
