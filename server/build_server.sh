#!/usr/bin/env bash
# 编译 Kanye 中继：Linux/Termux 版 Kanye，以及 Windows 版 Kanye.exe（交叉编译）
set -e
cd "$(dirname "$0")"
echo "[1/2] 编译 Linux/Termux 版 Kanye ..."
cc -O2 -Wall -o Kanye kanye.c
echo "      -> server/Kanye"
echo "[2/2] 交叉编译 Windows 版 Kanye.exe ..."
if command -v x86_64-w64-mingw32-gcc >/dev/null 2>&1; then
  x86_64-w64-mingw32-gcc -O2 -Wall -static -o Kanye.exe kanye.c -lws2_32
  echo "      -> server/Kanye.exe"
else
  echo "      未找到 mingw，跳过 Windows 版（可在 Windows 上用 gcc 编译，见 README）"
fi
echo "完成。"
