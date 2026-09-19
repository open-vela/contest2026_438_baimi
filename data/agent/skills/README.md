# 看护 Skill 设计草稿（当前 v0.8.0 固件不会加载）

这四个 Markdown 文件是**设计阶段**写的 Skill 草稿，面向以后可能接入的 openvela `ai_agent`。它们记录的是居家安全、老人提醒、日常作息和紧急广播的流程设想。

当前交付的 D12X 固件是 **v0.8.0 family-care**。这块镜像：

- 不会启动板端 `ai_agent`
- 不会读取 `/data/agent/skills/`
- 运行时也不会调用小米 MiMo

把这些草稿留在仓库里，是让评委看到后续智能体打算怎么管老人日常。**不要把它们写成今天真机已经在跑的能力。**

| 文件 | 用途 |
| --- | --- |
| `home-safety-guard.md` | 汇总烟雾、漏水、门窗、跌倒等家里安全状态 |
| `elder-care-reminder.md` | 吃药、活动一类的老人提醒 |
| `routine-scheduler.md` | 晨检、午休、晚检等日常节奏 |
| `emergency-broadcast.md` | 紧急时蜂鸣、界面和联系家人的流程 |

本队真正提交、实际反复使用的自建开发 Skill 在 `skills/velacare-dev/`，记录的是 D12X + ESP32-S3 的真实开发、烧录和提交流程。
