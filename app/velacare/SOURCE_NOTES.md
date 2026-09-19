# D12X 源码说明

`app/velacare` 对应 2026-09-18 的 D12X v0.8.0 family-care。

当前工作文件（Windows 联调后同步进 WSL 构建）：

- `velacare_main.c`
- `velacare_sensor.c/.h`
- `velacare_settings.c/.h`
- `velacare_lvgl.c/.h`
- `lv_font_velacare_cn_18.c` / `lv_font_velacare_cn_28.c`
- `Makefile`

WSL 构建树继承、本次一并提交的文件：

- `velacare_state.c/.h`（2026-09-03）
- `velacare_buzzer.c/.h`
- `velacare_skill.c/.h`（stub）
- `velacare_cron.c/.h`（stub）
- `Kconfig` / `Make.defs`

未提交、也不应再链接：

- `velacare_tts.c/.h`
- `velacare_voice_pcm.c/.h`
- `velacare_display.c/.h`（未进当前 Makefile）