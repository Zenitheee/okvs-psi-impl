# Local Evaluation

本文件记录当前仓库里已经实际运行过的 benchmark 命令和结果，用来回答两个问题：

1. `volepsi2` 现在到底跑到了什么程度？
2. 它和上游参考实现 `volepsi` 相比处于什么位置？

所有命令都从仓库根目录执行，结果初始记录于 2026-03-27，并于 2026-03-31 追加 `2^20` PSI 数据，使用本地已有构建产物。由于硬件、编译器、系统负载和网络环境都与论文不同，这里的数据**不是**论文 Table 2 的绝对复现，而是当前项目的本地结构化证据。

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

### Upstream `volepsi` historical PSI baseline

这些数值是删除本仓库内 `volepsi/` 目录前已经记录下来的历史基线，用于定位当前实现与上游优化实现之间的差距。它们不是当前仓库内可直接重跑的命令记录。

## Results

### `volepsi2` OKVS at `n = 2^12`

| Mode | Threads | Clustered | Table Size | Encode Avg (ms) | Decode Avg (ms) | Total Avg (ms) |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| OKVS | 1 | no | 5079 | 58.522 | 25.192 | 83.714 |
| OKVS | 4 | no | 5079 | 58.589 | 21.776 | 80.365 |
| OKVS | 1 | yes (`-bs 2048`) | 5886 | 59.354 | 28.887 | 88.241 |
| OKVS | 4 | yes (`-bs 2048`) | 5886 | 40.936 | 20.859 | 61.795 |

观察：

- 在当前实现和当前机器上，clustered OKVS 只有在 4 线程下才显示出明显收益。
- `n = 4096` 时，最佳 OKVS 配置是 clustered + 4 threads，总时间从 `80.365 ms` 降到 `61.795 ms`。

### `volepsi2` PSI size sweep

| Set Size | Threads | Clustered | Total Avg (ms) | OKVS Size |
| --- | ---: | ---: | ---: | ---: |
| `2^10 = 1024` | 1 | no | 117.842 | 1300 |
| `2^10 = 1024` | 4 | no | 127.288 | 1300 |
| `2^12 = 4096` | 1 | no | 224.314 | 5079 |
| `2^12 = 4096` | 4 | no | 207.009 | 5079 |
| `2^12 = 4096` | 1 | yes (`-bs 2048`) | 227.336 | 5886 |
| `2^12 = 4096` | 4 | yes (`-bs 2048`) | 209.149 | 5886 |
| `2^14 = 16384` | 1 | no | 731.280 | 20193 |
| `2^14 = 16384` | 4 | no | 685.094 | 20193 |
| `2^20 = 1048576` | 1 | yes (default `bin_size_hint = 16384`) | 22006.935 | 1355968 |
| `2^20 = 1048576` | 4 | yes (default `bin_size_hint = 16384`) | 11764.066 | 1355968 |

观察：

- 当前 `volepsi2` 已能稳定完成 `2^10` 到 `2^20` 的本地 PSI benchmark。
- `n = 2^20` 时默认 `bin_size_hint = 16384` 会触发 clustered OKVS，因此这两条数据反映的是默认配置下的完整 PSI 路径，而不是强行关闭 clustering 的结果。
- 多线程在 `n = 2^12` 和 `2^14` 上带来一定收益，而在 `n = 2^20` 上收益明显扩大，总时间从 `22006.935 ms` 降到 `11764.066 ms`。

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
- 论文 Table 2 的硬件和网络环境与当前仓库不同，所以这里只能做趋势性对照，不能硬说“已经复现论文绝对数值”。
- 如果后续需要把这份结果写入毕业论文正文，建议把本文件中的表格直接作为“本地复现评测”小节，并明确注明测试环境和限制。
