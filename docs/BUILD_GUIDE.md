# VelaCare 构建与烧录指南（Ubuntu / VirtualBox）

本指南适用于在 Ubuntu 虚拟机中编译 openvela + VelaCare 固件并烧录到
匠芯创 D12x（D12X-Demo68-nor）开发板。

## 0. 前置条件

- Ubuntu 20.04/22.04 x86_64（虚拟机建议 ≥ 4 核、8 GB 内存、80 GB 磁盘）
- 已安装 `git`、`python3`、`curl`、`repo` 工具
- D12x 开发板 + 4.3 英寸触控屏 + USB 下载线/串口线
- 首次编译前确认网络可访问 GitHub（拉取源码与工具链）

```bash
sudo apt update && sudo apt install -y git python3 curl unzip
mkdir -p ~/.bin
curl https://storage.googleapis.com/git-repo-downloads/repo > ~/.bin/repo
chmod a+x ~/.bin/repo
echo 'export PATH="$HOME/.bin:$PATH"' >> ~/.bashrc
source ~/.bashrc
```

## 1. 拉取完整工程

```bash
mkdir -p ~/openvela && cd ~/openvela
repo init -u https://github.com/open-vela/contest2026_438_baimi \
  -b dev-ai-contest-2026 -m contest2026_438_baimi.xml
repo sync -c -j8
```

同步后队伍仓位于 `~/openvela/contest2026_438_baimi/`，其中的
`app/velacare/` 会自动软链到 `packages/demos/contest2026_438_velacare`。

> 只在本仓 `contest2026_438_baimi/` 内修改代码；公共仓（nuttx、vendor 等）不要直接改。

## 2. 工具链

```bash
cd ~/openvela
./vendor/artinchip/tools/env.sh
```

脚本会自动下载并解压 RISC-V 工具链。若失败，检查网络后重试。

## 3. 配置（menuconfig）

```bash
./build.sh vendor/artinchip/boards/d12x/demo68-nor/configs/nsh_lvgl/ \
  --cmake menuconfig
```

必须启用的选项：

| 菜单路径 | 选项 | 值 |
| --- | --- | --- |
| Application Configuration → Packages → VelaCare | `VELACARE_USE_DEMO` | y |
| 同上 | `VELACARE_ENABLE_AGENT` | y（ai_agent 就绪后；否则 n） |
| Graphics support → LVGL | `LV_FONT_SIMSUN_16_CJK` | y（中文显示） |
| Graphics support → LVGL | `LV_FONT_MONTSERRAT_28` | y（大号数字，可选） |
| Application Configuration → Packages → Vela AI Agent | `EXAMPLES_AI_AGENT_VELA` | y（启用 agent 时） |

可选：`VELACARE_POLL_MS` 传感器轮询周期（默认 5000）、
`VELACARE_ALARM_NODE` 告警设备节点（默认空 = 仅日志/屏幕提示）。

> 说明：ai_agent 在 D12x 上的编译依赖网络/驱动条件，如暂不可用，先把
> `VELACARE_ENABLE_AGENT` 关掉做 UI-only 固件验证屏幕触控，后续再打开。

## 4. 编译与打包

```bash
cd ~/openvela
./build.sh vendor/artinchip/boards/d12x/demo68-nor/configs/nsh_lvgl/ -j8
cd vendor/artinchip/pack
./pack.sh
```

产物：

- `nuttx/nuttx`（ELF，GDB 调试）
- `nuttx/nuttx.bin`（平面镜像）
- `vendor/artinchip/pack/prebuilt/d12x_demo68-nor_v1.0.0.img`（完整烧录镜像）

改 defconfig 不生效时：`rm -rf cmake_out/` 后重新构建。

## 5. 烧录

按匠芯官方文档使用 D12x 烧录工具烧写
`d12x_demo68-nor_v1.0.0.img`：

- 硬件文档：https://aicdoc.artinchip.com/topics/product/d12x-demo-v1.html
- 烧录方法：https://aicdoc.artinchip.com/topics/quickstart/quick-start-chapter-tool-quickstart-d12x.html

## 6. 运行

上电后通过串口进入 NSH：

```bash
nsh> velacare --sim            # 模拟传感器模式（完整演示，推荐）
nsh> velacare --selftest       # 核心自检
nsh> velacare                  # 默认模式（按 settings.json 记忆状态）
```

屏幕出现五页界面（首页/环境/提醒/事件/设置）即成功。首页右上角状态显示
「AI 分析：在线」表示已连上 ai_agent；显示「离线（本地规则）」时核心功能仍可用。

## 7. 常见问题

| 问题 | 处理 |
| --- | --- |
| 中文显示为方块 | menuconfig 开启 `LV_FONT_SIMSUN_16_CJK` 后重新编译 |
| 屏幕无显示/触控无反应 | 确认烧的是 `nsh_lvgl` 配置产物；检查 `/dev/fb0`、`/dev/input0` |
| 找不到 velacare 命令 | 确认 `VELACARE_USE_DEMO=y` 且构建成功，`CONFIGURED_APPS` 含 velacare |
| agent 相关编译报错 | 暂时关闭 `VELACARE_ENABLE_AGENT` 先跑 UI，再排查 ai_agent 依赖 |
| 事件/提醒重启丢失 | 确认 `/data/velacare/` 所在分区可写；`settings.json` 会记忆开关 |

## 8. 日志归集（AI Coding）

在工作区内用官方支持的 AI 工具（Claude Code / OpenCode / Codex）开发时，
会话结束会自动写入 `contest2026_438_baimi/logs/<github用户名>/...`，
提交时：

```bash
cd ~/openvela/contest2026_438_baimi
git add logs/
git commit -s -m "logs: capture AI sessions"
git push
```
