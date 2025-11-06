# Blazing Fast PSI (CCS '22) Reproduction

本项目是对论文 "Blazing Fast PSI from Improved OKVS and Subfield VOLE"  的复现，作为武汉大学的本科毕业设计项目。

**项目状态:  (进行中)**

## Project Background

本项目旨在复现2022年ACM SIGSAC (CCS '22) 会议上的杰出论文：

* **论文标题:** Blazing Fast PSI from Improved OKVS and Subfield VOLE 
* **作者:** Srinivasan Raghuraman, Peter Rindal
* **会议:** Proceedings of the 2022 ACM SIGSAC Conference on Computer and Communications Security (CCS '22) 
* **DOI:** `https://doi.org/10.1145/3548606.3560658` 

该论文提出了一种新型的**隐私集合求交 (PSI)** 协议，其核心创新点在于一种**改进的OKVS (Oblivious Key-Value Store)** 数据结构  和对 **Subfield-VOLE** 技术的应用。

## Project Goals

此仓库的目标是：
1.  **复现核心算法:** 逐步实现论文中提出的OKVS `Encode` 和 `Decode` 算法 (Figure 1)。
2.  **构建PSI协议:** 基于实现的OKVS和VOLE（Vector-OLE）库，构建完整的PSI协议 (Figure 4)。
3.  **性能分析:** 尝试复现论文中的性能基准 (Table 2)。
4.  **封装与展示:** （最终）开发一个简单的用户界面来演示PSI功能。
