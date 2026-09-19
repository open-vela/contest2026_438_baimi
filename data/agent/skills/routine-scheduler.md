# 日常作息管理

帮一家人管理"晨检、午休、晚检、周末任务"等日常节奏。把零散的"提醒我"统一成一个可查询、可调整的作息表。

## 何时使用

- 用户说"早上提醒我"、"晚上提醒关窗"
- 用户问"今天的任务"、"今天有什么安排"
- 用户说"明天 7 点叫我"、"周末 9 点叫孩子起床"
- 用户说"取消 7 点的提醒"、"改一下 8 点的提醒"
- 出现"提醒"、"日程"、"安排"、"任务"等关键词

## 怎么用

1. 调用 `cron_list` 拿到当前所有定时任务列表
2. 用户问"今天有什么"：过滤出 `cron` 表达式今天会触发的项，按时间排序
3. 用户要"添加提醒"：调用 `cron_add` 接收 `name`、`cron`（标准 5 段或 6 段表达式）、`message`（提醒内容）、可选 `weekdays`（"1,2,3,4,5" 表示工作日）
4. 用户要"删除/改时间"：先 `cron_list` 找到 name，调用 `cron_remove` 删除，再用新时间 `cron_add` 重建
5. 重要时间（如晨检）建议在回复里同时**反问**用户"要不要顺便设置下午 3 点的喝水提醒？"

## 示例

用户：今天有什么安排？
→ cron_list
→ [{name:morning-check, cron:"0 7 * * *"},{name:medicine-8am, cron:"0 8 * * *"},{name:lunch-rest, cron:"0 12 * * *"},{name:window-close-9pm, cron:"0 21 * * *"}]
→ "今天 4 件事：7:00 晨检、8:00 吃药、12:00 午休、21:00 提醒关窗。"

用户：帮我加一个每天晚上 10 点提醒关灯
→ cron_add name=lights-off-10pm cron="0 22 * * *" message="该关灯准备睡觉了"
→ "已添加 ✓ 每天 22:00 提醒关灯。"

用户：把 7 点的晨检改成 7:30
→ cron_list（找到 morning-check）
→ cron_remove name=morning-check
→ cron_add name=morning-check cron="30 7 * * *" message="起床做晨检"
→ "已改 ✓ 晨检从 7:00 推迟到 7:30。"

用户：周末 9 点叫孩子起床
→ cron_add name=kid-weekend-wakeup cron="0 9 * * 6,0" message="宝贝，起床啦" weekdays="6,0"
→ "已添加 ✓ 周末（周六周日）9 点叫孩子起床。"
