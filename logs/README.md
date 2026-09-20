# logs/ — AI Coding 日志

这里放开发过程中与 AI 工具的对话日志和开发记录，与源码一起提交。目录中不包含官方模板的 example 占位日志。

- `gouzhongfei/`：队员 **gouzhongfei** 从 Codex Desktop 原始会话中导出、截断大块输出并脱敏后的真实会话事件，具体导出方式见其 `manifest.json`。
- `IdlebBack/`：队长 **IdlebBack** 根据实际开发过程人工整理的 10 条精简记录，用于补充个人工作轨迹；它不是 Codex 原始 rollout，也不冒充自动采集结果。含凭据的原始会话未提交。

## 目录结构

```text
logs/
├── gouzhongfei/
│   ├── manifest.json
│   └── <date>/
│       └── codex__<sid>.jsonl
└── IdlebBack/
    ├── manifest.json
    └── <date>/
        └── codex__<sid>.jsonl
```

- 工具名：`codex`
- 每个 `.jsonl` 每行一个事件
- 每个成员目录均提供清单；来源与整理方式以对应 `manifest.json` 为准

导出说明见[《AI Coding 日志归集与提交手册》](https://github.com/open-vela/docs/blob/dev-ai-contest-2026/zh-cn/contest_2026/ai_coding_log_guide.md)。
