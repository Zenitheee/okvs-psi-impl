# Local Evaluation

本文件记录当前仓库里已经实际运行过的 benchmark 命令和结果，用来回答两个问题：

1. `volepsi2` 现在到底跑到了什么程度？
2. 它和上游参考实现 `volepsi` 相比处于什么位置？

所有命令都从仓库根目录执行，结果初始记录于 2026-03-27，并于 2026-03-31 在重新构建 `volepsi2_bench` 后整表重测，随后于 2026-04-01 增补随机化压力验证。由于硬件、编译器、系统负载和网络环境都与论文不同，这里的数据**不是**论文 Table 2 的绝对复现，而是当前项目的本地结构化证据。

## Commands

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

### Upstream `volepsi` historical PSI baseline

这些数值是删除本仓库内 `volepsi/` 目录前已经记录下来的历史基线，用于定位当前实现与上游优化实现之间的差距。它们不是当前仓库内可直接重跑的命令记录。

## Results

### `volepsi2` OKVS at `n = 2^12`

| Mode | Threads | Clustered | Table Size | Encode Avg (ms) | Decode Avg (ms) | Total Avg (ms) |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| OKVS | 1 | no | 5079 | 29.455 | 21.628 | 51.083 |
| OKVS | 4 | no | 5079 | 29.490 | 7.068 | 36.558 |
| OKVS | 1 | yes (`-bs 2048`) | 5710 | 32.745 | 28.663 | 61.409 |
| OKVS | 4 | yes (`-bs 2048`) | 5710 | 21.911 | 7.987 | 29.898 |

观察：

- 在当前实现和当前机器上，clustered OKVS 仍然只有在 4 线程下才显示出明显收益；单线程反而更慢。
- `n = 4096` 时，最佳 OKVS 配置是 clustered + 4 threads，总时间从 `36.558 ms` 降到 `29.898 ms`。

### `volepsi2` PSI size sweep

| Set Size | Threads | Clustered | Total Avg (ms) | OKVS Size |
| --- | ---: | ---: | ---: | ---: |
| `2^10 = 1024` | 1 | no | 69.933 | 1300 |
| `2^10 = 1024` | 4 | no | 64.961 | 1300 |
| `2^12 = 4096` | 1 | no | 156.146 | 5079 |
| `2^12 = 4096` | 4 | no | 124.142 | 5079 |
| `2^12 = 4096` | 1 | yes (`-bs 2048`) | 172.828 | 5710 |
| `2^12 = 4096` | 4 | yes (`-bs 2048`) | 127.754 | 5710 |
| `2^14 = 16384` | 1 | no | 499.452 | 20193 |
| `2^14 = 16384` | 4 | no | 399.159 | 20193 |
| `2^20 = 1048576` | 1 | yes (default `bin_size_hint = 16384`) | 37462.610 | 1372928 |
| `2^20 = 1048576` | 4 | yes (default `bin_size_hint = 16384`) | 21589.079 | 1372928 |

观察：

- 当前 `volepsi2` 已能稳定完成 `2^10` 到 `2^20` 的本地 PSI benchmark。
- `n = 2^20` 时默认 `bin_size_hint = 16384` 会触发 clustered OKVS，因此这两条数据反映的是默认配置下的完整 PSI 路径，而不是强行关闭 clustering 的结果。
- 对完整 PSI 路径而言，多线程在 `2^10`、`2^12`、`2^14` 和 `2^20` 上都带来稳定收益；其中 `n = 2^20` 时总时间从 `37462.610 ms` 降到 `21589.079 ms`。
- 在当前实现里，`n = 2^12` 的完整 PSI 路径并没有因为 `-bs 2048` 受益，说明“clustered OKVS 更快”的现象目前主要体现在独立 OKVS benchmark，而不是这个规模下的端到端 PSI。

### `volepsi2` randomized stress validation

该模式会在 `n = 2^10`、`2^12`、`2^14` 上重复执行两类检查：

- 随机 OKVS encode/decode round-trip。
- 带真实交集的 PSI correctness check。

本次记录命令为：

```bash
./volepsi2/build/volepsi2_bench stress -t 20 -nt 4 -bs 2048
```

| Set Size | Trials | Clustered OKVS | Clustered PSI | OKVS Failures | PSI Failures | OKVS Avg Total (ms) | PSI Avg Total (ms) |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| `2^10 = 1024` | 20 | no | no | 0 | 0 | 9.644 | 67.600 |
| `2^12 = 4096` | 20 | yes | yes | 0 | 0 | 30.887 | 132.739 |
| `2^14 = 16384` | 20 | yes | yes | 0 | 0 | 91.820 | 365.418 |

观察：

- 在这 60 组随机 OKVS round-trip 和 60 组随机 PSI correctness check 中，当前实现都没有观察到失败。
- `-bs 2048` 让 `2^12` 和 `2^14` 的压力验证路径都实际经过 clustered OKVS，而 `2^10` 保持未聚类路径，因此这组验证覆盖了两种编码分支。
- 这里的 PSI correctness check 仍走 `volepsi2_bench stress` 的 benchmark-style 路径，也就是 silent VOLE 为真实本地相关性生成，但校正向量与标签传输不经过 demo 的 local socket UI 路径；后者仍由 `psi_test` 里的 `local_socket_flow` 用例负责覆盖。

### Upstream `volepsi` historical PSI baseline

| Set Size | Threads | Clustered Flag | Total (ms) |
| --- | ---: | --- | ---: |
| `2^10 = 1024` | 1 | default | 30.745 |
| `2^10 = 1024` | 4 | default | 32.060 |
| `2^12 = 4096` | 1 | default | 37.022 |
| `2^12 = 4096` | 4 | default | 39.112 |
| `2^12 = 4096` | 1 | `-bs 2048` | 37.656 |
| `2^12 = 4096` | 4 | `-bs 2048` | 41.632 |
| `2^14 = 16384` | 1 | default | 74.678 |
| `2^14 = 16384` | 4 | default | 73.992 |

## Interpretation

- `volepsi2` 已经不是“没有 benchmark 的项目”；它现在有明确命令、明确结果和已记录的基线对比。
- 从绝对性能看，当前 `volepsi2` 明显慢于优化过的上游 `volepsi`。这很正常，因为 `volepsi2` 的目标是独立复现、可读性和 Web UI 演示，而不是直接复刻上游所有性能优化。
- 从趋势上看，当前实现已经能观察到论文强调的一部分现象：clustered OKVS 在合适配置下会更快，多线程在较大输入下会有帮助。
- 但从结果也能看出，本仓库还没有复现出论文和上游实现的强性能优势，因此在答辩或论文中更合适的表述是：
  - “我们完成了协议与数据结构的可运行复现，并给出了本地结构化评测。”
  - “当前实现尚未在性能上追平上游优化实现。”

## Caveats

- 上游 `volepsi` 与 `volepsi2` 的 benchmark 前端并不完全等价，尤其在实现层级、优化程度和统计方式上存在差异，因此这里的比较用于项目定位，不应写成严格公平竞赛。
- 当前 `volepsi2_bench psi` 输出的 `claim_scope=benchmark_fallback` / `modeled_transfers=1` 表明：silent VOLE 字节是本地实测，但校正向量和 sender tag 仍是建模值。这与 Web UI 里的本地 socket demo 路径不是同一种流量口径。
- 论文 Table 2 的硬件和网络环境与当前仓库不同，所以这里只能做趋势性对照，不能硬说“已经复现论文绝对数值”。
- 如果后续需要把这份结果写入毕业论文正文，建议把本文件中的表格直接作为“本地复现评测”小节，并明确注明测试环境和限制。
