# VelaCare D12X v0.8.0 / ESP32 v0.15.0 跨板协议审计

- 审计日期：2026-09-18
- 方法：业务源码只读静态检查 + 主机侧确定性状态机模拟
- 结果：**19 项，PASS 19，FAIL 0**
- D12X 运行状态持久化识别：**是**（`api_defined=True; sensor_load_calls=1; sensor_save_calls=1; state_complete=True; fam_safe=True (SOS=persist-before-FACK; CARE=persist-before-FACK)`）

## 协议矩阵

| 方向 | 请求/通知 | 回执 | 校验 |
|---|---|---|---|
| D12X → ESP32 | `SOS1,<id>` | `SACK1,<id>,<status>` | id；status 1..2 |
| D12X → ESP32 | `SOSX1,<id>` | `SXACK1,<id>,<status>` | 活动 id；status 0..2 |
| D12X → ESP32 | `CARE1,<id>,<type>` | `CACK1,<id>,<type>,<status>` | id/type；status 1..2 |
| D12X → ESP32 | `CAREX1,<id>,<type>` | `CXACK1,<id>,<type>,<status>` | 活动 id/type；status 0..2 |
| ESP32 → D12X | `FAM1,<id>,<type>,<reply>` | `FACK1,<id>,<type>` | 精确 id/type；reply 1..4 |

统一封装：`<payload>*<CRC16四位十六进制>\n`。CRC16-CCITT-FALSE 初值 `0xFFFF`、多项式 `0x1021`，标准向量 `123456789 → 0x29B1`。

## 用例结果

| # | 结果 | 用例 | 说明 |
|---:|:---:|---|---|
| 1 | PASS | CRC vector, framing and bad-CRC rejection | contract satisfied |
| 2 | PASS | Static frame/validation/persistence contracts | contract satisfied |
| 3 | PASS | Only durable request IDs; no ephemeral fallback | contract satisfied |
| 4 | PASS | Settings ENOENT-only init and fail-closed reads | contract satisfied |
| 5 | PASS | D12X FAM history covers ESP32's 12-entry queue | contract satisfied |
| 6 | PASS | SOS and CARE reply codes are separate and durable | contract satisfied |
| 7 | PASS | SOS UI renders the caregiver's specific reply | contract satisfied |
| 8 | PASS | SOS, stale ID rejection and correct FAM close | contract satisfied |
| 9 | PASS | SOS and care cancellation roundtrips | contract satisfied |
| 10 | PASS | Contact-me and wellbeing flows | contract satisfied |
| 11 | PASS | Wrong ack/FAM ID and type rejected | contract satisfied |
| 12 | PASS | Duplicate FAM produces duplicate FACK | contract satisfied |
| 13 | PASS | Multiple replies and exact FACK deletion | contract satisfied |
| 14 | PASS | ESP32 reboot restores queue and activity | contract satisfied |
| 15 | PASS | D12X request IDs not reused on reboot | contract satisfied |
| 16 | PASS | Mute/confirm/cancel preserve sensor truth | contract satisfied |
| 17 | PASS | Full reply queue refuses overwrite | contract satisfied |
| 18 | PASS | FAM state persisted before FACK on SOS and care paths | contract satisfied |
| 19 | PASS | D12X reboot restores request/FAM history | contract satisfied |

## 源文件 SHA-256

| 文件 | SHA-256 |
|---|---|
| `tmp/v070-freeze-fix-src/app/velacare/velacare_sensor.c` (D12X sensor) | `053e1e0654366328931b9e098496f15c4bd3b6f1507473847e482d15afea29f4` |
| `tmp/v070-freeze-fix-src/app/velacare/velacare_sensor.h` (D12X header) | `8760f96927202a8b4db4963faf58966c4f03a39c8980721ebec84e1030f3caf6` |
| `tmp/v070-freeze-fix-src/app/velacare/velacare_settings.c` (D12X settings) | `56688b85aa5c65c5b55fe20896e09eddfe82f2f9ba0f7cc5e8872e52ab30cd08` |
| `tmp/v070-freeze-fix-src/app/velacare/velacare_settings.h` (D12X settings header) | `9397e25795397c44b089a78860491bda1fbf2db6767f529c8bcebf58f73621f2` |
| `tmp/v070-freeze-fix-src/app/velacare/velacare_lvgl.c` (D12X LVGL) | `c1c3d7158c7051c0f494405805b7d02079d8063d6792dd123cb8d191b15c5696` |
| `tmp/v070-freeze-fix-src/app/velacare/velacare_lvgl.h` (D12X LVGL header) | `779871d246b3a30ea4230a41f26ff62fd8953741ac9aee40c757c3349df7794e` |
| `tmp/v070-freeze-fix-src/app/velacare/velacare_main.c` (D12X main) | `b69bb52f607c1e777eef93898173c40cff7e5a6f8b150925baae757c339b7230` |
| `esp32_velacare_gateway/esp32_velacare_gateway.ino` (ESP32 gateway) | `e5e48c0e365f7e2074e87c34de0968cd57a4bb17db6dce052bea64239e8ab314` |

## 重启恢复判定

检测到 D12X 对 SOS/关怀/FAM 运行状态同时具有保存与加载接口；模拟重启后会恢复活动请求和已处理 FAM 键，并重新回复 FACK。

## 重跑命令

```powershell
python .\tmp\protocol_simulation_v080\test_velacare_protocol_v080.py
python .\tmp\protocol_simulation_v080\test_velacare_protocol_v080.py --strict
```
