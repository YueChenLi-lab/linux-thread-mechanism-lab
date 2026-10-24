# Linux 0.11 进程调度与轨迹实验（第五章）

本目录独立于仓库原有的 Linux pthread 线程实验。提供 **五子进程负载**、宿主 Linux 验证脚本、Linux 0.11 实验依赖检查和 Bochs 真机内核实验复现入口。

## 快速运行：宿主 Linux 五进程自检

1. 进入本目录：cd labs/linux011-process-scheduling
2. 执行：bash host/run_host.sh
3. 输出结果位于 .work/host-时间戳/result.txt，看到 ALL_CHILDREN_REAPED=5 表示五个子进程均已回收。

该路径可以在现代 Ubuntu/WSL 直接运行，但其数据 **不是** Linux 0.11 的内核轨迹。

## Linux 0.11 实验环境（WSL Ubuntu）

真实内核部分基于开源项目 https://github.com/appleweiping/hit-os 的 Linux 0.11 进程轨迹实验，其内核补丁记录 N(新建)/J(就绪)/R(运行)/W(阻塞)/E(退出) 事件。

1. 检查环境：bash scripts/doctor.sh
2. 允许安装缺失工具时执行：sudo apt update && sudo apt install build-essential gcc-multilib bin86 bochs bochs-x xvfb imagemagick git python3
3. 另外准备 gcc-3.4 历史工具链；并准备课程 oslab 材料中的 hdc-0.11.img、bochs/BIOS-bochs-latest、bochs/vgabios.bin。
4. 设置课程资源目录：export OSLAB="$HOME/oslab"（按本机的实际目录修改）。
5. 执行：bash scripts/bootstrap.sh（只在 .work/ 建立新的上游副本，不覆盖旧工程）。
6. 执行：bash scripts/run_guest.sh

启动后从 .work/guest-时间戳/ 获取 run.log、linux011-bochs.png、process.log、statistics.txt、timeline.png。

**复现状态说明**：宿主程序已经在本轮 Linux 容器真实编译运行，创建和回收五子进程验证通过；Bochs Linux 0.11 内核部分本轮没有实际启动（环境缺少 Bochs、gcc-3.4 和 oslab 课程镜像），故不把它标成已验证成功。需在用户 WSL 运行上述入口。

## 与之前课程报告的关系

- 第一章 ps/top/pstree 为普通 Linux 进程观察。
- 第五章 Linux 0.11 进程事件应由 guest 中的 /var/process.log 和 Bochs 截图作证。
- 本目录含五子进程负载；上游原始进程轨迹示例只有四子进程，本项目在隔离 checkout 中将其替换成五子进程样本。
- 之前 README(1).md 中提到的 QEMU 版 run_lab.py、kernel_changes.patch 和 priority=15/5 双镜像 **并未随原始附件提供**；这里采用公开上游的 Bochs Linux 0.11 复现方案，不冒充那些缺失文件。
- nice=0/10 只是普通 Linux 调度实验，不是 Linux 0.11 priority=15/5 的对照证据。两组 priority 结果仍需真实构建并测量。

## 文件结构与安全

- process.c：兼容旧 libc 风格的五进程 CPU/I-O 负载。
- host/run_host.sh：宿主编译、运行、五进程回收断言。
- scripts/doctor.sh：仅检查，不安装。
- scripts/bootstrap.sh：将上游固定在提交 01147f5bb8e0d2d9bdafbc51c5aa6b99dbb0f035，在 .work 内准备五进程样本。
- scripts/run_guest.sh：校验课程镜像与依赖，实际调用上游 Bochs 运行器。
- .gitignore：防止提交本地镜像、日志和二进制。

注意：上游旧实验脚本会清理 Bochs/Xvfb 进程。包装脚本会预检，发现已有相关进程就拒绝运行；务必在空闲的隔离实验 WSL 中使用。课程镜像与第三方历史二进制不包含在仓库。

上游来源与版权： https://github.com/appleweiping/hit-os ，保留其 Linux 0.11 的 GPLv2 等许可证约束。