# 串口协议约定

连线：ESP32 GPIO17 TX 到 D12X RX1，ESP32 GPIO18 RX 接 D12X TX1，只共地。115200 8N1，3.3V。两块板不要并 3V3/5V。

D12X 口：`/dev/ttyS1`。连上后 D12X 会写 `HELLO,VC1\n`。

CRC16/CCITT-FALSE 覆盖 `*` 前面的载荷：初值 `0xFFFF`，多项式 `0x1021`。帧形如 `PAYLOAD*XXXX\n`，CRC 是四位十六进制。

## 心跳（传感器真值）

```text
VC1,<seq>,<smoke>,<water>,<door>,<fall>,<wifi>*CRC
```

传感器字段：`0` 离线，`1` 正常，`2` 告警。Wi-Fi：`0` 离线，`1` 在线，必须来自真实 `WiFi.status()`，不能用假命令。周期 1 秒。发出去后从“现在”重新排下一帧，不要把积压的旧帧补发一串。

这些值由 ESP32 根据 GPIO/IMU 产生，D12X 只显示。静音、确认、老人 OK、家人回复都不得改这些字段。

## 蜂鸣器控制（不是传感器真值）

```text
VCB1,<muted>,<acknowledged>,<severity>*CRC
```

`muted`/`acknowledged` 是 0/1。`severity` 是 0..3。D12X 只用它停板载蜂鸣器。门还开着，两端界面都必须继续显示门窗告警。

## 老人确认安全（不能清除跌倒）

```text
OK1,<id>*CRC
OACK1,<id>*CRC
```

老人点「确认安全」后 D12X 发 `OK1`。ESP32 记下 `elderConfirmed` 并回 `OACK1`。跌倒保持告警，直到 IMU 回到安全锚点附近并稳定 2500ms。当 `VC1` 里的 `fall` 离开告警，D12X 再清本地“老人已确认”锁存。

不要拿 `OK1` 去取消 SOS 或取消关怀请求。

## 家人、SOS、关怀

请求号要持久写在 D12X 的 `request-state.bin`。当前 id 的重复 `FAM1` 仍回 `FACK1`。旧 id 忽略。

| 方向 | 帧 |
| --- | --- |
| D12X -> ESP32 | `SOS1`、`SOSX1`、`CARE1,<id>,<type>`、`CAREX1`、`OK1` |
| ESP32 -> D12X | `SACK1`、`SXACK1`、`CACK1`、`CXACK1`、`FAM1,<id>,<type>,<reply>`、`OACK1`、`CFG1` |
