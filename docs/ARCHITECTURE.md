# VelaCare 系统架构说明

## 1. 总体架构

```text
外部环境/活动传感器（模拟或真实）
            │
            ▼
    D12x + openvela（NuttX）
            │
            ├── velacare_core（本地规则引擎）
            │     ├── 传感器适配层（模拟源 / 真实驱动钩子）
            │     ├── 风险状态机 NORMAL/ATTENTION/WARNING/EMERGENCY/OFFLINE
            │     ├── 提醒调度器（定时 + 稍后升级）
            │     ├── 事件存储（cJSON -> /data/velacare/*.json）
            │     └── 告警输出（GPIO/LED 预留，日志兜底）
            │
            ├── velacare_ui（LVGL 480×272 五页界面 + 通知弹窗）
            │
            └── velacare_agent（openvelaClaw Client，可选）
                  └── ai_agent 框架 -> Xiaomi MiMo（断网自动降级）
```

## 2. 模块职责

| 模块 | 职责 | 关键接口 |
| --- | --- | --- |
| `velacare_main` | LVGL/显示初始化、事件循环、参数解析 | `--sim`、`--selftest` |
| `velacare_core` | 数据层与业务规则，与 UI 解耦 | `vc_core_tick()`、`vc_event_log()` |
| `velacare_ui` | 表现层：五页 UI、弹窗、1s 定时刷新 | `vc_ui_init()`、`vc_ui_navigate_to()` |
| `velacare_skills` | 内置 Skill 内容，启动写入 `/data/agent/skills/` | `vc_skills_install()` |

## 3. 风险状态机

```text
            ┌────────────┐
            ▼            │
      ┌──────────┐  阈值恢复
      │  NORMAL  │────────────┐
      └────┬─────┘            │
           │ 低温/湿度/无人活动 │
           ▼                  │
      ┌───────────┐           │
      │ ATTENTION │───────────┤
      └────┬──────┘           │
           │ 高温/空气预警      │
           ▼                  │
      ┌──────────┐            │
      │ WARNING  │────────────┤
      └────┬─────┘            │
           │ 极端高温/空气紧急   │
           ▼                  │
      ┌────────────┐          │
      │ EMERGENCY  │──────────┤
      └────────────┘          │

  OFFLINE：传感器无数据（模拟关闭且无真实驱动）时进入，
          恢复数据后回到正常评估流程
```

升级规则（按严重度取最高）：

1. 温度 ≥ 高温阈值 + 5℃ 或 空气异常 ≥ 紧急阈值 → EMERGENCY
2. 温度 ≥ 高温阈值 或 空气异常 ≥ 预警阈值 或 无人活动 ≥ 2×阈值 → WARNING
3. 低温、湿度超标、无人活动 ≥ 阈值 → ATTENTION
4. 全部正常 → NORMAL；传感器无数据 → OFFLINE

风险升级时自动弹窗 + 启动告警（WARNING 及以上）；回落后解除告警并记录
「风险解除」事件。所有状态迁移写入事件记录，可在「事件」页回看。

## 4. 数据流

```text
1s 定时器（UI）──► vc_core_tick()
                      ├─ 传感器轮询（默认 5s）：模拟/真实 -> 采样
                      ├─ 风险评估：采样 -> 状态机 -> 事件记录/告警
                      ├─ 提醒调度：到点 -> 事件记录 + 待通知队列
                      └─ 脏数据定时落盘（30s 合并）
UI 每 tick 检查待通知队列 -> 弹窗（确认/稍后5分）-> 事件标记已确认
```

## 5. 持久化

数据目录 `/data/velacare/`（Kconfig `VELACARE_DATA_DIR` 可改）：

| 文件 | 内容 |
| --- | --- |
| `events.json` | 事件环形记录（最多 64 条），含时间/风险/类别/消息/确认状态 |
| `reminders.json` | 提醒日程（最多 12 条），含时间/标签/启用状态 |
| `settings.json` | 模拟开关、活动模拟、阈值参数 |

写入采用「临时文件 + rename」原子替换，避免断电损坏。修改置脏标记，30 秒合并落盘。

## 6. 传感器扩展

当前内置确定性模拟源（温度/湿度/空气异常/活动），并在代码中预留弱符号钩子
`vc_sensor_hw_read()`：接入真实传感器（I²C 温湿度、烟雾/燃气模拟量、人体存在）
时，在应用中实现同名函数返回真实采样即可，风险链路无需改动。

## 7. AI 能力与降级

- 编译期：`VELACARE_ENABLE_AGENT=y` 时链接 `velaclaw` Client；可关闭做 UI-only 固件
- 运行期：`velaclaw_client_open("velacare")` 失败或 LLM 超时 → 回调返回本地建议
- Skill：`home-safety-guard`（阈值主动守护）、`elder-care-reminder`（定时主动提醒），
  首次启动自动写入 `/data/agent/skills/`

## 8. 内存估算（参考 mini_memo）

| 项目 | 估算 |
| --- | --- |
| LVGL 五页 UI | ~50-60 KB |
| 事件/提醒/设置存储（cJSON） | ~30-40 KB |
| openvelaClaw Client | ~20 KB |
| 字体（Montserrat24 + Simsun16） | 视编译选项 |
| 应用栈（STACKSIZE） | 40960 |

受 8 MB PSRAM / 16 MB NOR 约束，日志与事件数量均有上限，避免无界增长。
