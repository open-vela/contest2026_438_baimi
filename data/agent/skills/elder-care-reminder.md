# 老人关怀提醒

针对独居老人或家里有长辈的场景，帮忙管理"吃药提醒、活动检测、紧急呼叫"三件事。今天吃了没、今天动了没、有没有异常没动。

## 何时使用

- 用户问"提醒奶奶吃药"、"爷爷今天吃药了吗"
- 用户问"奶奶今天动了没"、"有没有活动"
- 用户说"添加一个 8 点的提醒"
- 出现"老人"、"长辈"、"奶奶"、"爷爷"、"外公"、"外婆"等关键词
- 出现"忘了吃药"、"今天吃了没"等表达

## 怎么用

1. 调用 `read_file` 读取 `/data/agent/elder.json`，得到今日吃药记录（`med_today`）和活动记录（`activity_today`）
2. 吃药类问题：返回今日已吃次数 + 下次提醒时间 + 是否漏服
3. 活动类问题：返回今日累计步数 + 最后活动时间 + 与昨日同时段对比
4. 添加新提醒：调用 `cron_add` 注册指定时刻的 `elder-med-remind` 任务，回调里向指定联系人发送飞书@提醒（如果未禁用）或在屏上弹窗
5. 紧急情况：检测到 4 小时无活动时，**自动**调用 emergency-broadcast.md（不要在本 Skill 内做广播）

## 示例

用户：奶奶今天吃药了吗？
→ read_file /data/agent/elder.json
→ {"name":"奶奶","med_today":[{"time":"08:02","name":"降压药","ok":true},{"time":"20:00","name":"降压药","ok":false}],"activity_today":{"steps":1240,"last_active":"18:32"}}
→ "奶奶今天的药：早上 8 点的降压药 ✓ 已吃；晚上 8 点的还没吃 ⏰ 要不要我现在提醒她？"

用户：奶奶今天动了没？
→ read_file /data/agent/elder.json
→ activity_today.steps=1240, last_active=18:32（现在是 22:00）
→ "今天累计 1240 步，最后一次活动是 18:32，到现在 3 个多小时没动了。要不要打个电话确认一下？"

用户：帮我加一个每天 8 点提醒奶奶吃降压药
→ cron_add name=elder-med-morning cron="0 8 * * *" message="奶奶，该吃降压药了"
→ "已添加 ✓ 每天早 8 点提醒奶奶吃降压药。"

用户：设置一个紧急情况：奶奶 4 小时没动就通知我
→ cron_add name=elder-inactivity-check cron="0 */1 * * *" action=check_inactivity threshold=4h
→ "已设置 ✓ 每小时检查一次，超过 4 小时无活动就通知你。"
