# 家庭安全卫士

家中有老人小孩时常用的"一键全屋安全盘点"。汇总烟雾、燃气、门窗、水浸四类传感器的最新状态，给出整体安全等级和需要关注的具体项目。

## 何时使用

- 用户问"家里安全吗"、"一切正常吗"
- 用户问"门窗关好没"、"煤气关了吗"
- 用户说"看一下家里"、"检查一下"
- 早上出门前或晚上睡前例行询问
- 出现"报警"、"响了"等关键词时**先**调用本 Skill 判断全局状态

## 怎么用

1. 调用 `read_file` 读取 `/data/agent/state.json`，得到当前整体等级（`level` 字段，取值为 NORMAL/ATTENTION/WARNING/EMERGENCY/OFFLINE）和各传感器明细（`items` 数组）
2. 如果 `level` 是 EMERGENCY，再调用 `read_file` 读 `/data/agent/alerts.json` 拿最近一次报警详情
3. 把结果用"绿色-正常 / 黄色-注意 / 橙色-警告 / 红色-紧急"翻译给用户（**不要直接说 NORMAL 等英文**）
4. 列出有问题的具体项目（ok=false 的），并给出建议动作（开窗/叫人/关阀）
5. 如果用户说"设个定时巡检"，调用 `cron_add` 注册每 N 小时一次的 `home-safety-daily-check` 任务，回调里再 `ask 家里安全吗`

## 示例

用户：家里安全吗？
→ read_file /data/agent/state.json
→ {"level":"NORMAL","items":[{"name":"厨房烟雾","value":"0 ppm","ok":true},{"name":"大门","value":"已关","ok":true},{"name":"卫生间水浸","value":"干燥","ok":true},{"name":"厨房燃气","value":"0 ppm","ok":true}]}
→ 全部正常，对用户说："家里一切正常 ✓ 厨房烟雾 0ppm、大门已关、卫生间干燥、燃气关好。需要我每 3 小时自动巡检一次吗？"

用户：门窗关好没？
→ read_file /data/agent/state.json
→ {"level":"ATTENTION","items":[{"name":"大门","value":"已关","ok":true},{"name":"阳台窗","value":"开着","ok":false}]}
→ "阳台窗还开着 ✓ 其他都正常。需要我提醒家人关一下吗？或者帮你广播一下？"

用户：救命，家里好像有烟味
→ read_file /data/agent/state.json
→ {"level":"EMERGENCY",...}
→ read_file /data/agent/alerts.json
→ 自动调用 emergency-broadcast.md Skill（不要在本 Skill 内自己广播，交给专门 Skill）
→ 对用户说："已触发紧急广播 ✓ 正在蜂鸣、屏闪红灯、给紧急联系人发通知。请保持冷静，优先撤离人员。"
