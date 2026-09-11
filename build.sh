#!/bin/bash
# build.sh - 编译 libadofai_mod.so (arm64-v8a)
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/build-android"
ABI="arm64-v8a"

echo "[1/4] 探测 Android NDK..."
NDK="${ANDROID_NDK_HOME:-$ANDROID_NDK_ROOT}"
if [ -z "$NDK" ]; then
    # 常见路径兜底
    for p in "$HOME/Android/Sdk/ndk" "$HOME/Library/Android/sdk/ndk"; do
        [ -d "$p" ] && NDK="$(ls -d "$p"/*/ 2>/dev/null | sort -r | head -n1)"
    done
fi
[ -z "$NDK" ] && { echo "未找到 NDK, 请设置 ANDROID_NDK_HOME"; exit 1; }
echo "    NDK = $NDK"

# 取 NDK 版本 (r25 用 toolchain, r22 以下用 ndk-build)
NDK_VER="$(basename "$NDK")"
echo "    NDK_VER = $NDK_VER"

echo "[2/4] 准备 minizip (unzip)..."
# 若 core/minizip 不存在则尝试系统包; 这里假设你已放好
if [ ! -d "$SCRIPT_DIR/core/minizip" ]; then
    echo "    警告: core/minizip 缺失, 请放入 minizip(unzip.h) 源码"
fi

echo "[3/4] CMake 配置 + 编译..."
rm -rf "$BUILD_DIR"
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

# 使用 NDK 自带的 toolchain 文件
TOOLCHAIN="$NDK/build/cmake/android.toolchain.cmake"
[ -f "$TOOLCHAIN" ] || { echo "找不到 android.toolchain.cmake"; exit 1; }

cmake "$SCRIPT_DIR" \
    -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN" \
    -DANDROID_ABI="$ABI" \
    -DANDROID_PLATFORM=android-21 \
    -DCMAKE_BUILD_TYPE=Release

make -j"$(nproc 2>/dev/null || echo 4)"

echo "[4/4] 完成"
SO="$BUILD_DIR/lib/$ABI/libadofai_mod.so"
[ -f "$SO" ] && cp "$SO" "$SCRIPT_DIR/libadofai_mod.so" && echo "产物: $SCRIPT_DIR/libadofai_mod.so"
