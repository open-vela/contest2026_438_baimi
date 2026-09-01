# VelaCare（contest2026_438_velacare）

基于 openvela + ai_agent 的**主动式居家安全与老人看护智能终端**应用，目标平台为
匠芯创 D12x（`D12X-Demo68-nor`，4.3 英寸 480×272 触控屏）。

本目录通过仓库 manifest 映射到 openvela 编译树的
`packages/demos/contest2026_438_velacare`。

## 功能

- 看护总览：时间、风险等级、温度/湿度/空气/活动状态一屏展示
- 环境监测：传感器采样与趋势条，支持模拟数据与真实传感器扩展
- 风险状态机：NORMAL / ATTENTION / WARNING / EMERGENCY / OFFLINE，本地规则主动判定
- 生活提醒：饮水/服药/作息定时提醒，支持确认与稍后 5 分钟
- 事件记录：风险、提醒、确认、处置全程落盘（`/data/velacare/`）
- AI 风险解释：openvelaClaw 可选接入，断网自动降级到本地建议
- 声光告警：预留 GPIO 节点，未接线时屏幕高亮提示
- 主动 Skill：`home-safety-guard`、`elder-care-reminder`，首次启动自动安装到
  `/data/agent/skills/`

## 运行

```bash
# openvela 工作区根目录，menuconfig 启用：
#   Application Configuration -> Packages -> VelaCare ...
#   （VELACARE_USE_DEMO、VELACARE_ENABLE_AGENT、LV_FONT_SIMSUN_16_CJK）
./build.sh vendor/artinchip/boards/d12x/demo68-nor/configs/nsh_lvgl/ -j8

# 上电后 NSH 中运行：
nsh> velacare --sim
nsh> velacare --selftest
```

详细编译烧录步骤见 `docs/BUILD_GUIDE.md`，演示剧本见 `docs/DEMO_SCRIPT.md`。

## 目录说明

```text
app/velacare/
├── velacare_main.c      # 入口：LVGL 初始化、事件循环、--sim/--selftest
├── velacare_core.h/.c   # 核心：传感器、风险状态机、提醒调度、事件存储、Agent
├── velacare_ui.h/.c     # LVGL 界面：首页/环境/提醒/事件/设置 + 通知弹窗
├── velacare_skills.h    # 内置 Skill 内容（启动时写入 /data/agent/skills/）
├── skills/              # Skill 源文件（与嵌入式内容保持一致）
├── tests/               # 宿主冒烟测试（无需开发板，可离线跑核心逻辑）
├── Kconfig / Makefile / Make.defs / CMakeLists.txt
└── README.md
```

## 测试

```bash
# 无开发板时验证核心逻辑（风险状态机/提醒/持久化/离线降级）
gcc -Wall -I tests/stubs -I tests/cjson \
    tests/host_smoke.c velacare_core.c tests/cjson/cJSON.c \
    -o /tmp/velacare_smoke && /tmp/velacare_smoke
```

详见 `tests/README.md`。
