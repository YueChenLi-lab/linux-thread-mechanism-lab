# Linux 线程创建与锁竞争：从 pthread 到内核等待

这是用于操作系统课前汇报的可复现实验包。它验证 Linux/glibc NPTL 的线程映射，并研究：**增加线程数量和临界区长度时，锁竞争如何改变吞吐、尾部获取延迟与内核等待；线程分片能否缓解，收益是否依赖多核？**

实验体现研究方法，不声称提出新的锁算法。结论限定于这个“可分片计数”工作负载。

## 1. 基础实验：只占两页 PPT

`src/basic.c` 创建三个 pthread；输出 PID/TID、共享变量地址、栈变量地址、TLS 地址和值，在线程退出前读取 `/proc/self/task`。一个线程睡眠 300 ms，另外两个完成心跳计数。

设计页：主线程 + 三个工作线程；观测身份、共享和独立状态、阻塞时的进展。

结果页：相同 PID、不同 TID；全局变量地址一致，栈与 TLS 地址不同；活跃内核任务数为 4；睡眠线程醒来时心跳已为 20。

它展示一对一模型的一个实例，不构造用户级线程运行时，也不直接比较一对一与多对一性能。若宿主 procfs 与进程 PID namespace 不同，`procfs_tid` 与 `gettid()` 数值可不同；本容器示例存在这种情况，任务数量仍可核对。正式在普通 Linux 主机上演示。

```bash
make check
./build/basic
```

## 2. 拓展实验与对照

| 项目 | 设计 |
|---|---|
| Baseline | 所有线程在一个普通 pthread mutex 下更新共享计数器 |
| 改进 | 每线程独立计数器及独立 mutex，结束后归并；mutex 与计数器按 128 字节对齐分开布局 |
| 对照 | 相同总操作数、临界区 CPU 工作量与编译参数；single 将所有工作线程固定到一个逻辑 CPU，spread 将其分配到多个可用逻辑 CPU |
| 自变量 | 工作线程 1/2/4/8；临界区额外 CPU 工作目标 0/5/20 us；shared/sharded；single/spread |
| 指标 | 总吞吐、每 64 次采样一次的获取延迟 p50/p95/p99、工作线程 CPU 时间和主动/被动上下文切换 |
| 重复 | 每配置一次预热、默认 10 次测量、固定种子随机执行顺序 |
| 统计 | 每配置跨运行中位数及 IQR；IQR 是离散程度，不能称为置信区间 |
| 正确性 | 共享或分片归并后的计数必须等于总操作数，包含无法整除线程数的情况 |

临界区“工作”用 CLOCK_MONOTONIC 忙等近似，未使用 sleep。参数代表目标额外 CPU 工作时间，不是完整持锁时间；时钟调用与抢占会造成误差。研究的是合成计数负载；分片仅适用于允许最终归并的业务，不能替换所有需要即时全局一致的共享状态。

创建在测量前完成；工作线程通过屏障就绪，由 release/acquire 原子变量同时放行。墙钟从放行前开始，到最晚工作线程完成结束，包含启动后的必要观测开销，排除创建和最终 join 的收尾时间。CPU 时间和上下文切换仅统计工作线程的工作区间，排除启动屏障和主线程 join。

```bash
# Python 环境已有 matplotlib 时可跳过安装
python3 -m pip install -r requirements.txt
python3 scripts/run.py --out results/host-full
python3 scripts/analyze.py results/host-full
# 短验证：32 配置，预热后每组测 3 次；用于验证流程
python3 scripts/run.py --quick --out results/host-quick
python3 scripts/analyze.py results/host-quick
```

输出：`environment.json`、每次运行的 `raw.jsonl`、`summary.csv`、PNG/SVG 图。线程超过可用工作 CPU 数时 spread 会复用 CPU，应结合 JSON 中 workers 的 CPU 和 lscpu 拓扑解释；逻辑 CPU 数不等于物理核心数。主线程在有条件时使用最后一个可用 CPU，其他 CPU 分配给工作线程。

正式实验尽量在固定机器、固定电源模式、相同编译参数下执行，记录 CPU 拓扑、核数、内核、glibc 与 cgroup 限额。脚本记录所见的 cgroup 根配额；若进程属于更深的 cgroup，还须核对其有效限制。结果图的 p99 是“每次运行的样本 p99 再取中位数”，不是全部操作的真实尾分位数；稀少的极端事件可能未被采样。零工作条件需特别注意计时开销。

## 3. 系统调用证据（单独运行）

```bash
python3 scripts/trace.py --out results/host-traces
```

跟踪 `clone`、`clone3`、`futex` 和测量区间的 write 标记，保留完整原始跟踪文件。创建链：`pthread_create → glibc NPTL 的 create_thread → __clone_internal → 实际观察到的 clone/clone3 → 内核任务`。

重点解释 CLONE_VM（共享地址空间）、CLONE_THREAD（同一线程组）与 CLONE_SETTLS（设置线程本地存储）。调用路径随 glibc、内核和运行环境变化；必须根据机器的 glibc 版本读对应源码，不能把 master 的所有内部细节直接当成实验版本。

跟踪三种情形：分片无竞争、共享零额外工作、共享 20 us。只计测量标记之间的**目标 mutex 地址**上 WAIT/WAKE 调用入口，过滤屏障、join 和其他库内部 futex。脚本的地址关联针对 glibc 普通 mutex 的锁字在对象基地址的布局；请先人工确认跟踪与该版本 glibc 布局，换 libc 时不要直接套用。

`trace_summary.json` 是调用计数，不是成功睡眠次数。WAIT 可能返回 EAGAIN，WAKE 也可能未唤醒任何人。可见 EAGAIN 数不包含所有 resumed 行，需人工检查完整 trace。`strace` 会改变调度与竞争，因此只用来验证机制，不把这些运行的吞吐放进主性能图。

## 4. 调度证据（正式机器上补齐）

```bash
python3 scripts/sched.py --out results/host-scheduler
# 根据 .stdout 中的 worker TID，筛选对应 timehist 行
```

脚本分别对 shared/sharded 执行 `perf sched record` 与 `perf sched timehist`，记录等待、调度延迟及运行片段。需要机器安装与内核匹配的 perf，并允许采集调度事件。timehist 的 wait time 包含不同 off-CPU 原因，不能一概当成等待目标锁；sch delay 是任务可运行后到真正上 CPU 的延迟。

`getrusage` 的切换次数属于辅助证据；没有 sched_switch/sched_waking 跟踪，不能宣称已测到调度延迟或唤醒后运行的时间。

若希望做“同一次运行内”的精细时序，把 sched:sched_switch、sched:sched_waking 与 syscalls:sys_enter_futex/sys_exit_futex 录进同一个 perf 数据流，按 TID、目标锁地址和时间顺序归因，检查返回值并处理任务退出。不同 strace/perf 运行只能互证趋势，不能拼成一条真实时序。

## 5. 当前验证状态

`results/container-smoke` 包含当前受限容器的 32 组、96 次不带跟踪的测量及图表。它用于证明流程可运行，不替代正式主机的数据。`results/container-traces` 保留实际 strace 失败记录：ptrace 被环境禁止，没有伪造系统调用结果。当前环境未提供 perf，没有调度事件数据。正式汇报应完成主机复跑后再展示“创建—等待—调度”的完整证据链。

## 6. GitHub 交付

仓库：[YueChenLi-lab/linux-thread-mechanism-lab](https://github.com/YueChenLi-lab/linux-thread-mechanism-lab)。初始可见性为私有。仓库包含源码、脚本、README、汇报提纲、环境信息、原始 CSV/JSON 和选定图表；不包含课程 PDF、编译产物或凭据。

代码和说明可以继续修改；每次提交保留版本记录，可比较或恢复旧版本。在网页打开文件后点击编辑按钮即可修改文本文件；批量开发建议使用 Git 或 VS Code。

源码已包含 GitHub Actions 正确性检查；若放在已有仓库子目录，需要将 workflow 放到该仓库根 `.github/workflows/` 并设置工作目录，子目录里的 workflow 不会自动执行。CI 的共享 runner 不作为性能数据来源。

## 7. 权威参考

- [pthreads(7)：NPTL 一对一映射、clone 与 futex](https://man7.org/linux/man-pages/man7/pthreads.7.html)
- [futex(7)：无竞争的用户态路径与内核等待/唤醒](https://man7.org/linux/man-pages/man7/futex.7.html)
- [glibc 2.39 pthread_create.c](https://github.com/bminor/glibc/blob/glibc-2.39/nptl/pthread_create.c)
- [glibc 2.39 clone-internal.c](https://github.com/bminor/glibc/blob/glibc-2.39/sysdeps/unix/sysv/linux/clone-internal.c)
- [perf-sched(1)：调度事件与 timehist 的含义](https://man7.org/linux/man-pages/man1/perf-sched.1.html)

见 `REPORT_PLAN.md` 获取七页汇报结构和答辩准备。
