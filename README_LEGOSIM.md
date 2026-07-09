# ChipletPart_legosim 使用说明

`ChipletPart_legosim` 是一个面向 LegoSim/Popnet 联动实验的 ChipletPart 版本。它保留原始 ChipletPart 的划分与 cost model 流程，并额外支持：

- 使用 3DBlox 输入文件运行 ChipletPart。
- 将 ChipletPart 划分结果导出为 LegoSim 可运行目录。
- 为 Popnet 生成 block-level 拓扑文件。
- 生成 gem5 synthetic workload，通过 InterChiplet API 产生通信流量。
- 在 block-level Popnet 边权中同时考虑通信带宽、floorplan 线长估计和是否跨 chiplet。

## 目录结构

常用目录如下：

```text
ChipletPart_legosim/
  3dblox_test_cases/       # 3DBlox 输入 testcase
  build/                   # CMake 编译输出，Git 忽略
  results/                 # ChipletPart/LegoSim 生成结果，Git 忽略
  scripts/                 # 环境配置与辅助脚本
  src/                     # ChipletPart 主代码
  cost_model/              # cost model
  GKlib/                   # Git submodule
  METIS/                   # Git submodule
  run_chiplet_test.sh      # 推荐入口脚本
```

## 获取代码

第一次下载时建议使用递归子模块：

```bash
git clone --recursive ssh://git@ssh.github.com:443/liljz250090/ChipletPart_legosim.git
cd ChipletPart_legosim
```

如果已经 clone，但子模块为空：

```bash
git submodule update --init --recursive
```

## 环境配置

项目不需要 sudo。这里提供的脚本不会创建环境，也不会安装依赖；它只检查当前 shell 是否已经具备编译条件、初始化子模块，并可选执行编译。

主要依赖如下：

```text
CMake 3.14+
C++20 compiler
Boost 1.71+
Eigen3
yaml-cpp
OpenMP-capable compiler/runtime
Git
make or ninja
```

如果希望用系统包管理器安装依赖，可以先预览安装命令：

```bash
./scripts/install_chipletpart_deps.sh
```

确认命令没问题后再实际执行：

```bash
./scripts/install_chipletpart_deps.sh --execute --yes
```

这个脚本只调用系统包管理器，支持 `apt-get`、`dnf`、`yum`、`pacman` 和 `brew`。如果当前用户没有系统安装权限，可以把脚本打印出的命令交给有权限的账号执行。

检查当前环境、初始化子模块并编译：

```bash
./scripts/setup_chipletpart_env.sh --build
```

如果移动过目录或想清理旧 CMake cache：

```bash
./scripts/setup_chipletpart_env.sh --clean-build --build
```

脚本默认使用：

```text
build dir: build
build type: Release
```

可以通过环境变量覆盖：

```bash
BUILD_DIR=build-debug CMAKE_BUILD_TYPE=Debug ./scripts/setup_chipletpart_env.sh --build
```

## 编译

手动编译命令：

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DENABLE_OPENDB_BACKEND=OFF
cmake --build build -j4
```

可执行文件生成在：

```text
build/bin/chipletPart
```

## 运行 ChipletPart

### 使用 3DBlox 输入

推荐入口：

```bash
./run_chiplet_test.sh ga100 --seed 1 --3dblox
```

可用 testcase 包括：

```text
ga100
epyc7282
48_1_14_4_1600_1600
48_2_14_4_1600_1600
48_4_14_4_1600_1600
48_8_14_4_1600_1600
```

对应文件在：

```text
3dblox_test_cases/<testcase>_3dblox/<testcase>.3dbx
3dblox_test_cases/<testcase>_3dblox/<testcase>.3dbv
```

### 导出 LegoSim 输入

使用 `--legosim`：

```bash
./run_chiplet_test.sh ga100 --seed 1 --3dblox --legosim
```

`--legosim` 会自动启用 `--3dblox`，并覆盖同名输出目录：

```text
results/<testcase>_legosim/
```

例如：

```text
results/ga100_legosim/
```

## LegoSim 输出目录

`--legosim` 生成的目录包含：

```text
gem5_popnet.yml           # LegoSim benchmark 配置
run_legosim.sh            # 运行 LegoSim 的脚本
run_popnet.sh             # phase2 调用 Popnet 的脚本
src/synthetic_workload.cpp# gem5 workload 源码
Makefile                  # 编译 synthetic workload
bench.txt                 # InterChiplet/Popnet traffic 文件
bench_popnet.txt          # run_popnet.sh 转换后的 Popnet trace
topology.gv               # Popnet 使用的 block-level 拓扑
block_topology.gv         # block-level 拓扑副本
chiplet_topology.gv       # chiplet-level 对照拓扑
chiplet_edges.tsv         # chiplet 聚合边
block_edges.tsv           # block-level 边及 weight 分解
chiplet_gateways.tsv      # chiplet 到 block gateway 的映射
partition_to_blocks.tsv   # chiplet/partition 到 block 的映射
delayInfo.block.txt       # Popnet block-level delay 输出
delayInfo.txt             # 映射回 chiplet id 的 delay 输出
```

## 修改 LegoSim 调用位置

`run_legosim.sh` 需要知道 LegoSim 仓库根目录，也就是包含这些路径的目录：

```text
interchiplet/bin/interchiplet
gem5/build/X86/gem5.opt
popnet_chiplet/build/popnet
```

默认生成的 `run_legosim.sh` 使用：

```text
/home/jzli/project/Chiplet_Heterogeneous_newVersion
```

如果只是本次运行临时指定 LegoSim 位置，推荐直接设置 `SIMULATOR_ROOT`：

```bash
cd /home/jzli/project/ChipletPart_legosim/results/ga100_legosim
SIMULATOR_ROOT=/path/to/Chiplet_Heterogeneous_newVersion ./run_legosim.sh
```

如果想修改某个已经生成的输出目录，编辑该目录下的 `run_legosim.sh`：

```bash
vim /home/jzli/project/ChipletPart_legosim/results/ga100_legosim/run_legosim.sh
```

把这一行改成你的 LegoSim 仓库路径：

```bash
DEFAULT_SIMULATOR_ROOT="/path/to/Chiplet_Heterogeneous_newVersion"
```

如果想修改以后所有新生成目录里的默认路径，修改 ChipletPart 生成脚本的位置：

```text
src/ChipletPart.cpp
```

搜索并修改：

```text
DEFAULT_SIMULATOR_ROOT="/home/jzli/project/Chiplet_Heterogeneous_newVersion"
```

修改后重新编译 ChipletPart，再重新生成 LegoSim 输出目录。

## 生成输出后运行 LegoSim

运行前请确保当前 shell 已经具备 LegoSim、gem5、interchiplet 和 Popnet 所需依赖。

先用 ChipletPart 生成 LegoSim 输入目录：

```bash
cd /home/jzli/project/ChipletPart_legosim
./run_chiplet_test.sh ga100 --seed 1 --3dblox --legosim
```

生成结果会覆盖写入：

```text
results/ga100_legosim/
```

进入输出目录并启动仿真：

```bash
cd /home/jzli/project/ChipletPart_legosim/results/ga100_legosim
./run_legosim.sh
```

如果 LegoSim 仓库不在默认位置：

```bash
SIMULATOR_ROOT=/path/to/Chiplet_Heterogeneous_newVersion ./run_legosim.sh
```

`run_legosim.sh` 会先执行 `make` 编译生成的 synthetic workload，然后调用：

```text
$SIMULATOR_ROOT/interchiplet/bin/interchiplet gem5_popnet.yml --cwd <输出目录>
```

仿真日志和结果会写回当前输出目录，例如：

```text
proc_r1_p1_t*/gem5.*.log
proc_r1_p2_t0/popnet.log
proc_r1_p2_t0/popnet_0.log
delayInfo.txt
delayInfo.block.txt
```

## 查看实验指标

主要看这些文件：

```text
delayInfo.txt
delayInfo.block.txt
proc_r1_p2_t0/popnet.log
proc_r1_p2_t0/popnet_0.log
```

常用含义：

```text
delayInfo.block.txt  # Popnet block-level delay
delayInfo.txt        # 映射回 chiplet id 的 delay
popnet.log           # Popnet 统计信息
popnet_0.log         # LegoSim phase2 日志
```

LegoSim 最后的：

```text
Benchmark elapses ... cycle
```

表示整个 benchmark 在 LegoSim 统一时间轴上的总仿真周期。若要评价拓扑和划分对互连的影响，优先看 Popnet delay。

## 常见问题

### CMake 提示 source directory 不匹配

如果移动过项目目录，旧 `build/` 里的 cache 会记录旧路径。删除重新配置：

```bash
rm -rf build
cmake -S . -B build
cmake --build build -j4
```

### `run_legosim.sh` 找不到 interchiplet

指定 LegoSim 根目录：

```bash
SIMULATOR_ROOT=/home/jzli/project/Chiplet_Heterogeneous_newVersion ./run_legosim.sh
```

### 子模块为空

```bash
git submodule update --init --recursive
```
