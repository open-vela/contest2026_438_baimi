# VelaCare 测试记录

## T0 宿主冒烟测试（无需开发板）

```bash
cd app/velacare
gcc -Wall -I tests/stubs -I tests/cjson \
    tests/host_smoke.c velacare_core.c tests/cjson/cJSON.c \
    -o /tmp/velacare_smoke && /tmp/velacare_smoke
```

覆盖：核心初始化/重初始化、风险状态机（NORMAL→EMERGENCY）、告警、待通知队列、
提醒调度（src_id/确认/稍后/停用）、采样、事件与提醒持久化 round-trip、
离线 AI 降级、自检、阈值接口、事件清空。

- [x] 当前基线：ALL PASS（0 failures）

## 测试计划

### T1 启动与界面

- [ ] `velacare --sim` 正常启动，无崩溃
- [ ] 首页显示时间、日期、风险等级、四项环境值
- [ ] 五页导航（首页/环境/提醒/事件/设置）切换正常

### T2 风险状态机

- [ ] 正常状态（绿）→ 触发燃气异常 → 预警/紧急（橙/红）
- [ ] 高温异常触发紧急
- [ ] 异常恢复后回到正常并记录「风险解除」
- [ ] 关闭模拟数据且无真实驱动时进入 OFFLINE
- [ ] 长时间无人活动（关闭模拟活动）触发关注/预警

### T3 提醒调度

- [ ] 添加提醒（演示 2 分钟后）到点弹窗
- [ ] 「确认」后事件标记已确认
- [ ] 「稍后5分」再次触发
- [ ] 提醒停用后不再触发
- [ ] 重启后提醒仍在（persistence）

### T4 事件与持久化

- [ ] 风险/提醒/确认事件写入 `/data/velacare/events.json`
- [ ] 事件页按时间倒序显示
- [ ] 「清空事件」生效
- [ ] 重启后数据恢复

### T5 AI 与降级

- [ ] agent 未连接时应用正常运行，本地建议可用
- [ ] agent 在线时 `vc_agent_explain` 返回 LLM 回复
- [ ] LLM 超时/失败自动降级
- [ ] Skill 文件安装到 `/data/agent/skills/` 且可被 agent 加载

### T6 告警

- [ ] 测试告警按钮 3 秒后自动关闭
- [ ] 配置 `VELACARE_ALARM_NODE` 后 GPIO 输出正确

### T7 稳定性

- [ ] 连续运行 2 小时无崩溃、内存无持续增长
- [ ] 传感器轮询 5s、落盘 30s 无异常

## 实机记录

| 日期 | 测试项 | 结果 | 备注 |
| --- | --- | --- | --- |
|  |  |  |  |
|  |  |  |  |
|  |  |  |  |

## 验证命令

```bash
nsh> velacare --selftest
nsh> ls /data/velacare
nsh> ls /data/agent/skills
nsh> cat /data/velacare/events.json
```
