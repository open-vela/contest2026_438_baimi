# 紧急情况广播

在出现烟雾、燃气泄漏、跌倒、4 小时无活动等紧急情况时，统一负责"蜂鸣 + 屏闪 + 屏显红字 + 通知紧急联系人"四件大事。是 4 级状态机 EMERGENCY 的执行层。

## 何时使用

- 状态机变成 EMERGENCY 时**自动**调用（由 cron 触发的 `velacare-emergency-watch` 任务调用本 Skill）
- 用户直接说"救命"、"出事了"、"快来人"
- home-safety-guard.md 或 elder-care-reminder.md 检测到 EMERGENCY 时**跳转**过来
- 任何用户消息含"紧急"、"救命"、"快"

## 怎么用

1. 调用 `read_file` 读 `/data/agent/alerts.json` 拿到当前报警详情（来源、严重度、位置、时间）
2. 蜂鸣：调用 `vibrate` 持续 5 秒（短振-长振-短振，间隔 200ms）
3. 屏闪：调用 `run_shell` 执行 `velacare ui alarm` （在屏上全屏红闪 + 显示"紧急情况"），白名单已开
4. 屏显文字：用 `write_file` 写 `/data/agent/ui/emergency.txt`，UI 进程会读取并大字号显示
5. 通知联系人：
   - 飞书未禁用时（演示默认禁用）：调用 `feishu_send_mention` @ 紧急联系人
   - 飞书禁用时（**演示场景**）：调用 `vibrate` + 屏闪 + 写一个 `/data/agent/contacts/notification.log` 文件作为"已通知"凭证
6. 等用户回复"我知道了"或"已处理"后，调用 `write_file` 把 `alerts.json` 的 `acknowledged` 置 true

## 示例

（自动触发场景）velacare-emergency-watch 检测到 EMERGENCY
→ read_file /data/agent/alerts.json → {"source":"厨房烟雾","level":"EMERGENCY","ts":"2026-08-30T09:01:23","acknowledged":false}
→ vibrate (5 秒)
→ run_shell "velacare ui alarm"
→ write_file /data/agent/ui/emergency.txt "厨房烟雾报警 - 9:01 - 请立即撤离"
→ write_file /data/agent/contacts/notification.log "[2026-08-30 09:01:23] 已通知紧急联系人 张三 (138****1234)"
→ 对用户说："🚨 紧急广播已启动 ✓ 蜂鸣中、屏闪红灯、已尝试联系紧急联系人。请先确保人员安全！"

用户：救命！
→ 不读任何文件，立即全流程触发（与上例相同）
→ 额外调用 `get_current_time` 记录触发时间
→ "🚨 紧急广播已启动 ✓ ..."

用户：我知道了
→ write_file /data/agent/alerts.json 把 acknowledged=true
→ "好的，已标记为'已处理'。需要我帮你做后续清理吗？比如联系物业、记录事件？"

## 边界
- 如果 `vibrate` 工具不可用（穿戴设备场景），只做屏闪 + 通知
- 如果 `run_shell` 处于拒绝策略，跳过屏闪，改为 `write_file` 让 UI 进程自行监听 `/data/agent/ui/emergency.txt`
- 通知联系人失败时不要重试超过 2 次，避免阻塞
