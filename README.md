# VelaCare：基于 openvela 的主动式居家安全与老人看护智能终端

## 一、作品简介

VelaCare 是一台面向独居老人和家庭照护者的主动式居家安全终端，运行在
**openvela + ai_agent** 上，硬件为匠芯创 **D12x（D12X-Demo68-nor）** 开发板与
4.3 英寸触控屏。

它把「环境监测 → 风险识别 → 主动提醒 → 处置确认 → 事件记录」整合成一个闭环：
通过本地规则状态机（NORMAL / ATTENTION / WARNING / EMERGENCY / OFFLINE）主动发现
高温、空气异常、长时间无人活动等风险，自动弹窗告警并联动声光；按预设时间主动
推送饮水、服药、作息提醒，支持确认与稍后升级；所有事件落盘可回看。联网时由
ai_agent 对风险摘要生成解释与照护建议，断网时自动降级为本地规则与本地建议，
核心功能完全离线可用。

**亮点：**

- 真正的「主动 + 执行」：阈值主动、定时主动、事件主动三类场景齐备，不是被动问答
- 适老化交互：大字体、大按钮、分级配色（绿/黄/橙/红），GT911 电容触控直操作
- 端侧优先、云端可选：LLM 断连不影响任何核心能力
- 两个可复用主动 Skill：`home-safety-guard`、`elder-care-reminder`，随应用自动安装
- 完整演示闭环：模拟传感器 + 一键触发异常，无需外设即可完整跑通演示流程

## 二、选题方向

**AI 硬件产品创新**。基于 openvela + ai_agent 开发能主动感知、主动提醒、可执行
处置闭环的嵌入式 AI 应用，覆盖大赛要求的「阈值主动、定时主动、事件主动」三类
主动任务，并沉淀自定义 Skill。

## 三、目录结构

```text
contest2026_438_baimi/
├── app/velacare/              # VelaCare 应用（映射到 packages/demos/contest2026_438_velacare）
│   ├── velacare_main.c        # 入口：LVGL 初始化、事件循环、--sim/--selftest
│   ├── velacare_core.h/.c     # 核心：传感器、风险状态机、提醒调度、事件存储、Agent
│   ├── velacare_ui.h/.c       # LVGL 界面：首页/环境/提醒/事件/设置 + 通知弹窗
│   ├── velacare_skills.h      # 内置 Skill（启动时写入 /data/agent/skills/）
│   ├── skills/                # Skill 源文件（home-safety-guard / elder-care-reminder）
│   ├── tests/                 # 宿主冒烟测试（无开发板可跑，验证核心逻辑）
│   └── Kconfig / Makefile / Make.defs / CMakeLists.txt
├── docs/                      # 架构说明、构建指南、演示剧本、测试记录
├── logs/                      # AI Coding 日志（按组委会规范导出提交）
├── contest2026_438_baimi.xml  # 仓库 manifest（linkfile 映射）
└── openvela.xml               # openvela 全量工程清单（组委会提供，勿改）
```

## 四、运行方式

在 Ubuntu 开发环境（本队使用 VirtualBox 虚拟机）中：

```bash
# 1. 首次拉取完整工程
repo init -u https://github.com/open-vela/contest2026_438_baimi \
  -b dev-ai-contest-2026 -m contest2026_438_baimi.xml
repo sync -c -j8

# 2. 下载 D12x 工具链（首次）
./vendor/artinchip/tools/env.sh

# 3. menuconfig 启用 VelaCare 与中文字体
./build.sh vendor/artinchip/boards/d12x/demo68-nor/configs/nsh_lvgl/ \
  --cmake menuconfig
#   Application Configuration -> Packages -> VelaCare -> VELACARE_USE_DEMO=y
#   VELACARE_ENABLE_AGENT=y（ai_agent 就绪后；否则可先关掉做 UI-only 固件）
#   Graphics support -> LVGL -> Enable Simsun 16 CJK（中文显示）

# 4. 编译 + 打包
./build.sh vendor/artinchip/boards/d12x/demo68-nor/configs/nsh_lvgl/ -j8
cd vendor/artinchip/pack && ./pack.sh

# 5. 烧录（D12x 官方烧录工具）后，NSH 中运行：
nsh> velacare --sim          # 模拟传感器模式，完整演示
nsh> velacare --selftest     # 核心自检
```

完整步骤、菜单选项与排错见 [docs/BUILD_GUIDE.md](docs/BUILD_GUIDE.md)。

## 五、AI Coding 使用说明

- **需求拆解与方案设计**：借助 AI 分析赛道要求，参考官方 mini_memo 示例，确定
  「本地规则兜底 + ai_agent 增强」的端侧架构与五页 UI 结构。
- **编码**：AI 生成 C 代码骨架（LVGL 页面、cJSON 持久化、velaclaw 客户端调用），
  人工 review 并修复线程安全、空指针、编码等问题。
- **调试**：通过 `--selftest` 自检、syslog 分级日志、模拟数据 + 一键异常按钮
  快速复现风险链路。
- **文档**：AI 协作整理架构说明、构建指南、演示剧本与测试记录。
- 本作品的全部 AI 对话日志按组委会《AI Coding 日志归集与提交手册》导出到
  `logs/` 目录，随代码一并提交。

## 附：开发进度

- [x] 应用框架：LVGL 五页 UI、核心状态机、提醒调度、事件存储
- [x] 主动 Skill：home-safety-guard / elder-care-reminder
- [x] 文档：架构 / 构建 / 演示 / 测试
- [x] 宿主冒烟测试：风险状态机 / 提醒 / 持久化 / 离线降级（ALL PASS）
- [ ] D12x 实机：工具链编译、烧录、屏幕触控验证（按官方 `nsh_lvgl` 配置）
- [ ] ai_agent 实机接入与 MiMo 配置（D12x 网络条件就绪后）
- [ ] 演示视频、PPT 与最终提交材料
