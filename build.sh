#!/bin/bash
# 设置遇到错误立即退出，避免编译失败后仍然强行运行测试或程序
#它表示只要脚本中出现任何非零返回值的命令（例如 cmake --build 编译报错，或者 ctest 测试不通过），脚本就会立刻停止运行，不会去执行后面的旧程序。
# -e：命令失败时通常立即退出
# -u：使用未定义的变量时退出
# -o pipefail：管道中任何命令失败都视为失败
set -euo pipefail

# 定义变量，方便统一修改
BUILD_DIR="build/debug"
INSTALL_DIR="install/debug"

BUILD_LOG="$BUILD_DIR/build.log"
INSTALL_LOG="$BUILD_DIR/install.log"
# rm -rf "$BUILD_DIR" "$INSTALL_DIR" 


PARALLEL_LEVEL=$(( ($(nproc) * 3 + 1) / 2 ))
# -S .   指定源码目录
# -B     指定构建目录
# DCMAKE_BUILD_PARALLEL_LEVEL : 主要编译第三方库使用的
# ALWAYS	显示 Installing 和 Up-to-date，默认行为
# LAZY	只显示实际安装或更新的文件
# NEVER	不显示普通文件安装状态信息
cmake -S . -B "$BUILD_DIR"  \
        -DCMAKE_BUILD_PARALLEL_LEVEL="$PARALLEL_LEVEL"\
        -DCMAKE_INSTALL_MESSAGE=LAZY

# --parallel：启用并行编译
# nproc：获取当前可用的 CPU 逻辑处理器数量
#cmake --build "$BUILD_DIR"  --parallel "($nproc)"
#cmake --build "$BUILD_DIR"  --parallel 8
# if ! cmake --build "$BUILD_DIR" \
#     --parallel "$PARALLEL_LEVEL" \
#     > "$BUILD_LOG" 2>&1
# then
#     echo "编译失败，完整日志："
#     cat "$BUILD_LOG"
#     exit 1
# fi
cmake --build "$BUILD_DIR" --parallel "$PARALLEL_LEVEL"  

# --test-dir：指定测试所在的 构建目录  # 不是目标文件目录
# --output-on-failure：只在测试失败时输出详细信息
# --no-tests=error：未注册测试时将其视为错误
# ctest --test-dir "$TEST_BUILD_DIR" \
#       --output-on-failure \
#       --no-tests=error

 # --prefix "$INSTALL_DIR" \
cmake --install "$BUILD_DIR" 

# --prefix "$INSTALL_DIR"
# --prefix "$BUILD_DIR/install"  # 指定安装目录                             