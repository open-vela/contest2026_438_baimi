# 评委 3 分钟入口

仓库：https://github.com/open-vela/contest2026_438_baimi
分支：请看官方仓 `dev-ai-contest-2026`（不要只看未合并 PR 或 fork）
赛道：AI 硬件产品创新
作品：VelaCare（D12X 老人端 + ESP32-S3 网关）

## 先看这些

1. [README.md](README.md) — 作品说明和运行方式
2. `app/velacare/` — D12X v0.8.0 源码
3. `gateway/esp32_velacare_gateway/esp32_velacare_gateway.ino` — ESP32 v0.15.1，含跌倒双锚点
4. `skills/velacare-dev/SKILL.md` — 自建 Skill
5. `logs/gouzhongfei/` — 真实 AI Coding 日志
6. `docs/` — 技术报告；演示视频明天补拍
7. `evidence/` — 真机照片，不是模拟器截图冒充成品

## 真机怎么演示

1. D12X 烧录 v0.8.0 family-care 镜像后应看到「家庭健康守护」四页。
2. ESP32 联网后打开 `http://<网关IP>/dashboard`。
3. 开门磁 -> 设备页和网关卡片显示门窗异常；合上后恢复。点确认/静音，蜂鸣器停，门窗告警仍在。
4. 把 IMU 放到跌倒姿态 -> 语音「检测到疑似跌倒」，D12X 出「确认安全」。回到安全姿态并稳定约 2.5 秒后恢复。
5. 老人点「确认安全」后，网关显示老人已确认，但跌倒卡片在 IMU 恢复前保持告警。

## 请按源码核实，不要按旧材料

以下内容已经不做、也不再当卖点：

- 8 月骨架里的 5 级状态机文案
- D12X 本地中文 TTS（v0.8.0 明确不链接）
- 运行时加载 `/data/agent/skills/` 或 Xiaomi MiMo
- 只交镜像、不交源码
- `logs/` 里的 example 占位日志

## 版本配对

| 端 | 版本 | 说明 |
| --- | --- | --- |
| D12X | v0.8.0 family-care | 家人页、FAM1/FACK1、确认安全 |
| ESP32 | v0.15.1 | 固定双锚点跌倒、语音、网页守护 |

不要用 v0.7.0 / v0.14.x 文档理解当前固件。