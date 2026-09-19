# 源码地图和重建分工

Windows 工作区根目录：`C:\Users\gzf\Desktop\open-vela`。
WSL 工程：`/root/openvela-work/contest2026_438_baimi`。

## D12X 文件

当前交付版本是 **v0.8.0 family-care**。工作副本仍在 `tmp/v070-freeze-fix-src/app/velacare/`，因为 family-care 重建脚本是从这棵树拷文件。

每次重建 D12X 时从 Windows 拷贝：

| 文件 | 负责什么改动 |
| --- | --- |
| `velacare_main.c` | 启动横幅、主循环、VCB1 只停蜂鸣器 |
| `velacare_sensor.c` / `.h` | UART `/dev/ttyS1`、CRC、`VC1`/`VCB1`/`FAM1`/`OK1`/`SOS`/`CARE` |
| `velacare_settings.c` / `.h` | `request-state.bin`、请求号高水位 |
| `velacare_lvgl.c` / `.h` | 480x272 界面；触摸请求不要在 LVGL 回调里做阻塞 IO |
| `lv_font_velacare_cn_18.c`、`lv_font_velacare_cn_28.c` | 新增非 ASCII 界面文字 |
| `Makefile` | 链接单元；必须有 settings 和两套字体；不得有 TTS/PCM |

WSL `app/velacare/` 里继承、本次拷贝不覆盖、但要确认存在：

- `velacare_state.c`
- `velacare_buzzer.c`
- `velacare_skill.c`
- `velacare_cron.c`

除非任务就是改状态机/蜂鸣器/skill/cron，否则不要动这些继承文件。不要把 `velacare_tts.c` 或 `velacare_voice_pcm.c` 重新加进 `CSRCS`。

D12X 重建：WSL 里的 `tmp/rebuild-v080-family-care.sh`。它会：

1. 拷贝上面的 Windows 工作文件。
2. 新界面汉字缺字就失败。
3. Makefile 链接了 TTS/PCM 就失败。
4. 删掉旧的 `app/velacare/*.o` 和 `apps/libapps.a`。
5. 构建 `vendor/artinchip/boards/d12x/demo68-nor/configs/nsh_lvgl`。
6. 打包 `d12x_demo68-nor_velacare-v080-family-care_20260918.img`。

如果在 `velacare_lvgl.c` 里加了中文字符串，重建前先重新生成两套字体。ASCII 已经覆盖。

## ESP32 文件

改 `esp32_velacare_gateway/`：

| 文件 | 负责什么改动 |
| --- | --- |
| `esp32_velacare_gateway.ino` | GPIO、`VC1`/`VCB1`/`FAM1`、IMU 双锚点、Wi-Fi、网页、NVS |
| `voice_prompts.h` | 四条中文语音；wav 变了就用 `tools/` 重新生成 |
| `tools/synthesize_voice_prompts.py`、`tools/wav_to_voice_header.py` | 语音素材流水线 |

本项目用的 FQBN：

`esp32:esp32:esp32s3:FlashSize=8M,PartitionScheme=default_8MB`

8MB 分区和 app0 槽不变时，应用更新写 `0x10000`，这样 NVS 里的 Wi-Fi、联系人和事件还能留着。从 `0x0` 烧整包会清 NVS。

当前版本常量：`kFirmwareVersion[] = "0.15.1"`。如果改了这个字符串，网页页脚里如果还写着 `Gateway v0.15.0` 也要一起改。

## 重建判断

| 改动 | 重建 |
| --- | --- |
| 跌倒锚点、恢复、GPIO 极性、语音、网页、Wi-Fi 配网 | 只重建 ESP32 |
| LVGL 文案、字体、蜂鸣器 PWM、D12X 串口解析、请求号、卡死 | 只重建 D12X |
| 帧名、字段顺序、CRC 多项式、请求号含义 | 两块都重建 |
| 比赛文档、Skill 草稿、日志 | 不用烧板 |

D12X 重建后，不要用 Windows 自动脚本去抢 COM4。在 WSL 构建，再用 AiBurn 按 `flash-test.md` 烧。
