#!/bin/sh
# VelaCare 宿主静态检查（不依赖 openvela 编译树）
# 用法：在 app/velacare/ 目录下执行  bash tests/check_all.sh
set -e

cd "$(dirname "$0")/.."

echo "== syntax check: velacare_core.c =="
gcc -Wall -fsyntax-only -I tests/stubs -I tests/cjson velacare_core.c

echo "== syntax check: velacare_ui.c =="
gcc -Wall -fsyntax-only -I tests/stubs -I tests/cjson velacare_ui.c

echo "== syntax check: velacare_main.c =="
gcc -Wall -fsyntax-only -I tests/stubs -I tests/cjson velacare_main.c

echo "== smoke test: core logic =="
gcc -Wall -I tests/stubs -I tests/cjson \
    tests/host_smoke.c velacare_core.c tests/cjson/cJSON.c \
    -o /tmp/velacare_smoke
/tmp/velacare_smoke

echo "ALL CHECKS PASSED"
