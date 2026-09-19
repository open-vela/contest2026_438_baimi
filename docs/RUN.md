# 烧录与复现说明

## D12X

- 板型：D12X-DEMO68-V1-2 / `D12X-Demo68-nor`
- 应用：`app/velacare`
- 构建配置：`vendor/artinchip/boards/d12x/demo68-nor/configs/nsh_lvgl`
- 烧录工具：AiBurn
- 本队不在本仓库提交 `.img`。评委如需复现，请按 README 从源码构建。

注意：

- Makefile 不得链接 `velacare_tts.c` / `velacare_voice_pcm.c`
- `velacare_skill.c`、`velacare_cron.c` 是占位，链接进去只为保持现有构建，不代表运行时 Skill 已加载

## ESP32-S3

- 源码目录：`gateway/esp32_velacare_gateway/`
- 版本：`kFirmwareVersion = "0.15.1"`
- 必须包含 `voice_prompts.h`，否则语音无法编译
- Flash 8MB，PartitionScheme `default_8MB`

串口调试命令（节选）：`imu status`、`alarm ack`、`alarm mute`、`alarm unmute`。

## 验收顺序

1. 双方上电，D12X 3 秒内应看到网关心跳，不再显示离线。
2. 门磁开合、湿润漏水探头、MQ-2 告警脚，分别看设备页是否跟随。
3. 跌倒姿态 -> 语音 + 严重告警；安全姿态稳定 2.5 秒 -> 恢复。
4. 网页点确认：蜂鸣器停，告警文案仍在。
5. 断网产生事件后恢复网络，看是否出现待补传/已补传。