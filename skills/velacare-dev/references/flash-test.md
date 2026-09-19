# 烧录和台架测试

## 端口规则

- D12X 用匠芯创 AiBurn 烧录。不要用 Windows 脚本对 COM4 自动烧，避免抢占烧录工具。
- ESP32-S3 最近调试口是 COM8（CP210x）。烧录前重新确认端口，不要写死。
- 两块板 UART 只共地，不要并电源。

## D12X

1. 在 WSL 树 `/root/openvela-work/contest2026_438_baimi` 用 `tmp/rebuild-v080-family-care.sh` 构建。
2. 确认最终 ELF / Makefile 没有 `velacare_tts`、`velacare_voice_pcm`。
3. 用 AiBurn 烧到 D12X-DEMO68-V1-2。
4. 上电后应看到「家庭健康守护」四页，3 秒内出现网关心跳。

## ESP32

1. FQBN：`esp32:esp32:esp32s3:FlashSize=8M,PartitionScheme=default_8MB`
2. 源码：`esp32_velacare_gateway/esp32_velacare_gateway.ino`，必须带上 `voice_prompts.h`
3. 应用更新写 `0x10000`，避免清掉 NVS 里的 Wi-Fi。
4. 无已保存 Wi-Fi 时开热点 `VelaCare-Setup-xxxx`，配置页 `http://192.168.4.1`。不要记录热点密码。
5. 连上后打开 `http://<网关IP>/dashboard`，自检页 `/selftest`。

## 烧完后的台架顺序

必须上真机，不能只靠编译通过。

1. 空闲：四页能切，设置/自检能开，`VC1` 每秒一帧，心跳不变时不要整屏重绘。
2. 门磁：拿开 -> 设备页和网页出门窗告警。点静音。门还开着，告警必须还在。合上后恢复。
3. 跌倒：放到跌倒锚点。ESP32 播「检测到疑似跌倒」。播报时继续点 D12X，界面不能卡死。
4. 老人确认安全：点按钮后，网页可以显示老人已确认。`VC1` 的跌倒字段在 IMU 恢复前保持告警。
5. 静音/确认：线上出现 `VCB1`；烟雾/漏水/门窗/跌倒字段不变。
6. 恢复：放回安全姿态，等 2.5 秒，跌倒恢复，老人已确认锁存清除。
7. 只播语音：`audio play fall` 不得制造跌倒告警。
8. 网络：短暂断网再恢复，不要突然补发一串旧 UART 帧。

如果 D12X 还链着 TTS/PCM，或者文档还写 MPU6050 / v0.7.0 / 运行时 MiMo，应在构建阶段失败，而不是等到台架才发现。
