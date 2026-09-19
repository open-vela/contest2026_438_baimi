---
name: velacare-dev
description: 按白米队 VelaCare 的真实流程改 D12X 和 ESP32-S3 固件、UART 协议、跌倒标定、烧录和比赛打包。用于 VelaCare、D12X-DEMO68、ESP32 网关、family-care、VC1/VCB1、跌倒检测或提交。不要用于普通 hello_app、无关 ESP32 项目或空想的养老产品。
metadata:
  short-description: VelaCare 的 D12X+ESP32 固件、烧录和提交流程
---

# VelaCare 开发 Skill

这是 **contest2026_438_baimi / 白米队** 反复使用的开发流程，作品 **VelaCare**，赛道 **AI 硬件产品创新**。只写本队实际做过的事。不要编造传感器、板端大模型和认证设备。

## 当前作品

- 硬件：匠芯创 D12X-DEMO68-V1-2（4.3 寸 480x272 LVGL，板载蜂鸣器）+ ESP32-S3 网关。
- 当前固件：D12X **v0.8.0 family-care**，ESP32 **v0.15.1**。
- 语音只在 ESP32 播放。D12X v0.8.0 不得链接 TTS/PCM。
- 传感器都在 ESP32：MQ-2 烟雾 GPIO5，漏水 GPIO4，门磁 GPIO6，IMU MPU-6500/9250 的 I2C 用 GPIO8/GPIO9，MAX98357A 用 GPIO7/15/16。
- 串口：ESP32 GPIO17 TX 到 D12X RX1，GPIO18 RX 接 D12X TX1，115200 8N1，CRC16/CCITT-FALSE，心跳帧 `VC1`。

## 以哪份源码为准

先改 Windows 工作文件，再同步进已经稳定的 WSL 树。不要把未合并 PR、旧 `.img` 名字或 v0.7.0 烧录说明当成当前版本。

| 端 | 在这里改 | 不要当成可改源码 |
| --- | --- | --- |
| D12X 当前工作文件 | `tmp/v070-freeze-fix-src/app/velacare/` | 打包好的 `.img`、`tmp/v069-src`、`tmp/v068-src` |
| D12X 继承单元 | WSL `app/velacare/{velacare_state,velacare_buzzer,velacare_skill,velacare_cron}.c` | Windows 上的 TTS/PCM 文件 |
| ESP32 网关 | `esp32_velacare_gateway/` | `esp32_gateway_build_*`、旧 `ESP32_VelaCare_v0*.bin` |
| 比赛打包 | `_contest_stage/` 和合并后的 `contest2026_438_baimi` 源码 | 示例日志、只交截图的压缩包 |

v0.8.0 重建时从 Windows 拷贝这些 D12X 文件：

- `velacare_main.c`、`velacare_sensor.c/.h`、`velacare_settings.c/.h`、`velacare_lvgl.c/.h`、`lv_font_velacare_cn_18.c`、`lv_font_velacare_cn_28.c`、`Makefile`

WSL 里继承、**不要被这次拷贝覆盖**的文件：`velacare_state.c`、`velacare_buzzer.c`、`velacare_skill.c`、`velacare_cron.c`。Windows 上还有 `velacare_tts.c` 和 `velacare_voice_pcm.c`，必须保持不链接。

WSL 工程：`/root/openvela-work/contest2026_438_baimi`。板级配置：`vendor/artinchip/boards/d12x/demo68-nor/configs/nsh_lvgl`。重建脚本：`tmp/rebuild-v080-family-care.sh`。

改代码前先看对应说明：

- 文件和重建分工：[references/source-map.md](references/source-map.md)
- 串口约定：[references/protocol.md](references/protocol.md)
- 烧录和台架测试：[references/flash-test.md](references/flash-test.md)
- 比赛打包：[references/submission.md](references/submission.md)

固件或打包收工前，跑 `scripts/check_velacare_invariants.py`。

## 什么时候重建哪块板

改传感器、跌倒锚点、语音、Wi-Fi 网页、`VC1` 生成、`VCB1` 静音/确认、家人 `FAM1`、NVS 事件缓存时，重建 **ESP32**。

改 LVGL 页面、中文字体、蜂鸣器 PWM、`VC1`/`VCB1`/`FAM1` 解析、SOS/OK1/CARE 请求号、设置/`request-state.bin`、卡死/线程问题时，重建 **D12X**。

只有帧格式、CRC 规则或请求号含义变了，才两块板一起重建。静音/确认类问题通常是 ESP32 发送加 D12X 解析，不是改传感器真值。

不要为了“加语音”去重建 D12X。不要为了“修中文缺字”去重建 ESP32。

## 硬约定

1. `VCB1` 静音/确认只停 D12X 蜂鸣器，不得改写 `VC1` 里的烟雾、漏水、门窗、跌倒真值。
2. 老人点「确认安全」发出的 `OK1` 不能清除跌倒。只有 IMU 回到安全锚点附近并稳定 2.5 秒，跌倒才恢复。
3. IMU 是 MPU-6500（`WHO_AM_I=0x70`）或 MPU-9250（`0x71`），地址 0x68。任何地方都不要写成 MPU6050。
4. 双锚点跌倒，2026-09-18 本机标定：安全 `(0.149537, 0.059815, 0.986945)`，跌倒 `(-0.957276, -0.199432, 0.209404)`，夹角约 `87.04°`。不要让跌倒姿态把安全锚点覆盖掉。
5. D12X 的 Makefile 和最终 ELF 不得出现 `velacare_tts` 或 `velacare_voice_pcm`。
6. 不要在 Windows 上对 COM4 自动烧 D12X。ESP32 最近是 COM8（CP210x），烧录前重新确认端口。
7. 配网热点 SSID 是 `VelaCare-Setup-xxxx`。热点密码不要复制、打印、写进仓库。
8. 文档和界面版本必须写 v0.8.0 / v0.15.1。不要把 v0.7.0 防卡死说明混进 v0.8.0 family-care。

## 诚实写清还没做的

这些只是设计意图，不是当前 v0.8.0 运行时能力：

- 没有加载小米 MiMo。
- 没有加载 openvela `ai_agent`。固件不会读 `/data/agent/skills/`。
- MQ-2、雨水探头、门磁、IMU 跌倒都是比赛原型输入，不是认证烟感、水浸或医用跌倒报警器。

`_contest_stage/data/agent/skills/` 下四个文件是**设计草稿**，给以后的智能体用。不要说它们现在已经在板子上执行。

## 这个 Skill 就是为了避免这些坑

- 跌倒语音后界面卡死：根因是 D12X 本地 TTS/PCM，以及在 LVGL 线程做串口/IO。语音放 ESP32，D12X 的 IO 放控制线程。
- 点静音后门窗界面变正常：把 `VCB1` 误当成传感器真值。门还开着，界面就必须继续显示门窗异常。
- 只交镜像不交源码、交示例日志、代码只活在未合并 PR 里。
- 写成 MPU6050、宣称运行时 `ai_agent`/MiMo、把 v0.7.0 当成交付固件。

## 默认步骤

1. 先分清这次是 D12X 界面/协议、ESP32 传感器/语音/Wi-Fi、跌倒标定、烧录，还是比赛打包。
2. 改对应的真实源码。帧字段顺序和 CRC 规则不要单边改。
3. 只重建拥有这次改动的那块 MCU。确认 D12X 仍然没有 TTS/PCM 目标文件。
4. 按 [references/flash-test.md](references/flash-test.md) 的端口规则烧录，再跑那里的台架顺序。
5. 打包按 [references/submission.md](references/submission.md)，没做的能力如实写。
