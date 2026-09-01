# VelaCare 宿主冒烟测试（host smoke test）

无需开发板即可在 Linux/macOS/Windows（需 C 编译器）上验证 VelaCare：
核心逻辑（风险状态机、告警、提醒调度、事件持久化、离线 AI 降级）冒烟测试 +
全部源文件静态语法检查。

`stubs/` 提供最小 NuttX/LVGL 无关的系统头桩（内存文件系统模拟落盘），
`cjson/` 为 MIT 协议的单文件 cJSON 库（[DaveGamble/cJSON](https://github.com/DaveGamble/cJSON)，
仅用于宿主测试，不进入固件）。

## 运行

```bash
# 在 app/velacare/ 目录下
bash tests/check_all.sh    # 一键：语法检查 + 冒烟测试

# 或手动执行冒烟测试
gcc -Wall \
    -I tests/stubs -I tests/cjson \
    tests/host_smoke.c velacare_core.c tests/cjson/cJSON.c \
    -o /tmp/velacare_smoke
/tmp/velacare_smoke
```

期望输出以 `ALL PASS (0 failures)` 结束；出现 `FAIL` 即代表核心逻辑回归。

> 提示：`stubs/nuttx/config.h` 开启了 `CONFIG_VELACARE_ENABLE_AGENT`，
> 因此该测试同时覆盖 velaclaw 离线降级路径。
