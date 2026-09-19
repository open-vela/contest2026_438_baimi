# VelaCare

面向独居老人和家庭照护者的主动式居家安全终端。老人端跑在匠芯创 D12X 上，家人端通过 ESP32-S3 网关看状态、确认告警和收听语音提示。

当前可演示版本：**D12X v0.8.0 family-care + ESP32-S3 v0.15.1**。本仓库提交的是可核实源码，不提交 `.img` / `.bin` 镜像。

## 一、作品简介

VelaCare 把烟雾、漏水、门窗和跌倒四路风险收到一块适老屏上，并用蜂鸣器、中文语音和家人网页把“发生了什么、老人是否安全、家人有没有看见”说清楚。

已经落地的能力：

- D12X 四页：首页、设备、求助、家人。跌倒时出现大按钮「确认安全」。
- ESP32 采集 MQ-2、雨水比较器、门磁和 MPU-6500/9250，每秒发送 `VC1` 心跳。
- 报警分级：普通提醒、严重告警、持续未恢复。
- 家人确认/静音只停蜂鸣器和语音，不改写传感器真值。门还开着，界面就必须继续显示异常。
- 老人确认安全只通知家人，跌倒状态要等 IMU 回到安全姿态才恢复。
- 跌倒语音在 ESP32 播放，不在 D12X 上播 TTS，避免界面卡死。
- 断网时事件写入 NVS，最多 12 条，网络恢复后可补传。

明确没有做、报告里也不宣称的：

- 没有在板端加载 Xiaomi MiMo，也没有运行时 `ai_agent`。
- `velacare_skill.c` / `velacare_cron.c` 仍是占位，当前固件不会扫描 `/data/agent/skills/`。
- MQ-2 和 IMU 跌倒是比赛原型，不能替代认证烟感和医疗跌倒报警器。
- 不是新硬件平台适配赛道，没有全新 BSP。

## 二、选题方向

AI 硬件产品创新。选择 D12X 官方板 `D12X-Demo68-nor`，把 openvela 的 LVGL 图形能力用在老人端，把传感器融合、语音和网页放在 ESP32 网关。

## 三、目录结构

- `app/velacare/` — D12X 端 VelaCare 应用源码，通过 manifest 映射到 `packages/demos/contest2026_438_velacare`
- `gateway/esp32_velacare_gateway/` — ESP32-S3 网关源码、语音 PCM 头文件和 wav 源
- `skills/velacare-dev/` — 本队自建开发 Skill（硬性要求）。沉淀反复使用的固件/协议/烧录/提交流程
- `data/agent/skills/` — 早期看护 Skill 设计草稿。当前固件未加载，不要当成运行时能力
- `docs/` — 运行说明、技术报告、演示视频占位
- `evidence/` — 少量真机照片
- `logs/gouzhongfei/` — 真实 AI Coding 日志，不是 example 占位
- `tools/` — 串口和协议辅助脚本
- `board/`、`quickapp/` — 官方模板保留，本作品未使用
- `contest2026_438_baimi.xml` — 把 `app/velacare` 链到 openvela 编译树
- `JUDGE_GUIDE.md` — 评委 3 分钟入口

## 四、运行方式

### 1. 拉工程

```bash
repo init -u https://github.com/open-vela/contest2026_438_baimi \
  -b dev-ai-contest-2026 -m contest2026_438_baimi.xml
repo sync -c -j8
```

在 `menuconfig` 中启用 `CONFIG_LVX_USE_DEMO_CONTEST2026_438_VELACARE`。

### 2. 编译 D12X

在 openvela 工作区根目录：

```bash
./build.sh vendor/artinchip/boards/d12x/demo68-nor/configs/nsh_lvgl -j8
```

本队实际构建在 WSL 树 `/root/openvela-work/contest2026_438_baimi` 完成。打包脚本见开发过程中的 `rebuild-v080-family-care.sh` 逻辑：只同步当前工作文件，并确认最终 ELF 不含 `velacare_tts` / `velacare_voice_pcm`。

烧录使用匠芯创 AiBurn，目标板 D12X-DEMO68-V1-2。不要把 Windows 上的 COM4 自动当烧录口反复抢占。

### 3. 编译 ESP32-S3

Arduino IDE / arduino-cli：

- FQBN：`esp32:esp32:esp32s3:FlashSize=8M,PartitionScheme=default_8MB`
- 源码：`gateway/esp32_velacare_gateway/esp32_velacare_gateway.ino`
- 必须一起编译 `voice_prompts.h`
- 本机调试口最近是 COM8（CP210x），烧录前请重新确认

### 4. 接线（当前真机）

| 模块 | ESP32-S3 | 说明 |
| --- | --- | --- |
| 门磁 | GPIO6 / GND | 内部上拉，LOW=正常，HIGH=告警 |
| 漏水 | GPIO4 | 雨水比较器 DO，低电平告警 |
| 烟雾 | GPIO5 | MQ-2 比较器经电平转换，低电平告警 |
| IMU | SDA GPIO8 / SCL GPIO9 | MPU-6500/9250，地址 0x68 |
| MAX98357A | DIN GPIO7 / BCLK GPIO15 / LRC GPIO16 | 外接喇叭，SPK± 差分 |
| 与 D12X UART | TX GPIO17 -> D12X RX1；RX GPIO18 <- D12X TX1 | 115200 8N1，共地，不并电源 |

跌倒双锚点（2026-09-18 本机标定，已写进固件）：

- 安全姿态：`(0.149537, 0.059815, 0.986945)`
- 跌倒姿态：`(-0.957276, -0.199432, 0.209404)`
- 夹角约 `87.04°`；回到安全锚点并稳定 2.5 秒才恢复

### 5. 配网与家人页

无已保存 Wi-Fi 时，ESP32 打开热点 `VelaCare-Setup-xxxx`，配置页 `http://192.168.4.1`。连上家庭网络后访问：

```text
http://<网关IP>/dashboard
```

自检页：`/selftest`。不要把热点密码写进仓库或报告。

## 五、AI Coding 使用说明

本作品几乎全程用 Codex Desktop 做需求拆解、协议设计、LVGL/ESP32 编码、真机联调和提交整理；部分调试也用过 Claude Code。AI 负责改代码和查日志，人负责接线、姿态标定、听语音和确认界面有没有卡死。

自建 Skill 见 `skills/velacare-dev/SKILL.md`。真实对话日志见 `logs/gouzhongfei/`，不是官方模板里的 example。

AI 协助编码占比按“成稿代码由 AI 起草、人负责验收和标定”估计约 80%–90%，这是协助口径，不是跳过人工审查。Token 总量无法从 Codex Desktop 完整导出，报告中按未统计处理，不编造数字。

## 六、队伍

- 队名：baimi
- 编号：438
- 仓库：`contest2026_438_baimi`
- 队长：林明强（GitHub `IdlebBack`）
- 队员：苟中飞（GitHub `gouzhongfei`）
- 分工：林明强负责仓库与提交协调；苟中飞负责 D12X 应用、ESP32 网关、传感器接入、真机联调和文档

演示视频计划于 2026-09-19 补拍后放入 `docs/` 或按官方要求上传。当前先看 `docs/demo-video.md`。