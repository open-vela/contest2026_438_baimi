# 比赛打包

队伍 `contest2026_438_baimi` / 白米，赛道 AI 硬件产品创新。本 Skill 加上 `_contest_stage/data/agent/skills/` 下四份设计草稿，用来满足“自建至少一个 Skill”的要求。评委认可把反复使用的开发流程写成 `SKILL.md`。

## 要做

- 提交评委能核对的**源码**：D12X `app/velacare`、ESP32 `esp32_velacare_gateway`、本 `velacare-dev` Skill、以及仓库 `logs/` 里的真实 AI Coding 日志。
- 把代码合进官方队仓。只活在未合并 PR 里的代码不算提交。
- 版本名称对齐：D12X v0.8.0 family-care，ESP32 v0.15.1。
- 硬件如实写：D12X-DEMO68-V1-2 + ESP32-S3 网关，原型传感器，板载蜂鸣器，ESP32 外接喇叭。
- 把本 Skill 目录作为比赛要求的自建 Skill 一起交。

## 不要做

- 用 `.img` / `.bin` / 截图代替源码。
- 交示例或看起来是生成出来的 AI 日志。要用本队真实 Codex/Claude 会话。
- 把 v0.7.0 防卡死文档混进 v0.8.0 family-care。
- 任何地方写成 MPU6050。
- 宣称当前 v0.8.0 加载了小米 MiMo、运行时 `ai_agent`，或会读 `/data/agent/skills/`。
- 公开或写入配网热点密码。SSID 写成 `VelaCare-Setup-xxxx` 即可。
- 把 MQ-2、雨水探头、门磁、IMU 跌倒说成认证烟感、水浸、安防或医用跌倒设备。

## 诚实的产品边界

台架上已经实现：480x272 LVGL 界面、UART `VC1` 心跳、门窗/烟雾/漏水/跌倒原型输入、双锚点 IMU 跌倒、老人 `OK1`、家人 `FAM1`、ESP32 中文语音、D12X 蜂鸣器、本地网页。

v0.8.0 没有实现：板端 `ai_agent`、小米 MiMo、运行时加载 `_contest_stage/data/agent/skills/`。

`_contest_stage/data/agent/skills/` 里四个 Markdown 是设计草稿。打包时用 README 说明这一点，不要暗示它们现在已经在跑。

## 日志和 CLA

- AI Coding 日志放在 `contest2026_438_baimi/logs/`，不要 gitignore 掉。
- CLA 通过后把 PR 合进官方队仓。不要只把唯一一份代码留在笔记本桌面。
