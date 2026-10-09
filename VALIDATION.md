# 本次已完成的验证

日期：2026-10-09。环境：受限 Linux 容器；完整环境信息在 `results/container-smoke/environment.json`。

1. gcc/cc 使用 `-O2 -g -std=c11 -Wall -Wextra -Werror -pthread` 编译成功。
2. `make check` 通过：PID/TID、共享地址、栈/TLS 地址与值、4 个活跃内核任务、睡眠期间其他线程进展；12 个计数/亲和性对照，含 10003 次无法整除线程数的工作量。
3. 32 组配置各一次预热、3 次不带跟踪测量，共 96 条正式记录；计数全部正确。
4. 原始记录、环境、CSV、PNG/SVG 已生成，图像已检查可读性。
5. `strace` 在执行 ptrace 时收到 Operation not permitted，原始失败 stderr 已保留；没有生成可用调用证据。
6. `perf` 不可用；`scripts/sched.py` 已做 Python 语法检查，未完成主机端事件采集。
7. GitHub 目标：`YueChenLi-lab/linux-thread-mechanism-lab`，初始可见性为私有；本报告与源码、原始结果随初始实验提交发布。

## 一个可用于安排正式实验的预实验观察

条件：总工作量 20000 次，额外临界区 CPU 工作目标 10 us，4 个工作线程，3 次运行的中位数。

| 策略 | 同一 CPU：操作/s | 多 CPU：操作/s |
|---|---:|---:|
| 共享锁 shared | 88,353 | 69,115 |
| 分片 sharded | 90,920 | 335,372 |

多 CPU 下的吞吐比约为 4.85；同一 CPU 下约为 1.03。多 CPU 的共享锁获取延迟样本 p99 中位数约为 1998 us，分片约为 0.041 us。仅为小规模容器预实验观察，不是普遍结论；尤其短任务、三次重复、固定间隔采样都限制了推断。

这个结果支持进一步检验“共享竞争限制并行收益”的解释，但没有目标锁 futex 与调度跟踪，还不能把吞吐差异定量分解为内核等待、调度或缓存开销。

## 复现前需知道的边界

- 性能 CSV 不包含 strace/perf 运行。
- IQR 不是置信区间；获取延迟 p99 基于每 64 次一次采样。
- quick 的零工作用例可能非常短，正式结果应增加工作量及重复次数。
- 当前 procfs 与调用者 PID namespace 不同，`procfs_tid` 与 gettid 数值可能不同，数量仍符合一对一映射。
- `environment.json` 中源码 hash 记录了测量当时的文件；之后 basic.c 仅将输出标签从 kernel_tid 改为 procfs_tid，以准确表达 namespace 差异。性能程序 contention.c 未改动。
- syscall 地址过滤与 perf 采集脚本需要在允许跟踪的 glibc Linux 主机复核。
