# logs/ — VelaCare AI Coding 日志

本目录存放本队使用 AI 工具（Claude Code / OpenCode / Codex 等）开发 VelaCare
时产生的对话日志，与作品代码一并提交，供评委评估 AI 赋能开发过程。

## 目录结构

```text
logs/
└── <github_login>/              # 每位成员自己的 GitHub 用户名
    ├── manifest.json            # 会话清单
    └── <date>/                  # 日期 YYYY-MM-DD
        └── <tool>__<sid>.jsonl  # 一个会话一个文件
```

## 归集方式

在 openvela 工作区内（存在 `.repo/` 目录）使用官方支持的 AI 工具开发，
会话结束时会自动写入本目录；提交时：

```bash
git add logs/
git commit -s -m "logs: capture AI sessions"
git push
```

也可用组委会提供的脚本手动导出：

```bash
contest-snapshot --list
contest-snapshot --session <session-id> --confirm
```

## 注意

- 请把示例文件替换为真实导出日志，未提交前可删除不想公开的会话
- 只提交 JSONL 原文，不要修改内容（校验脚本会检测篡改）
- 本目录当前为空，等待首次在工作区内结束 AI 会话后自动生成

详细规范见组委会《AI Coding 日志归集与提交手册》：
<https://github.com/open-vela/docs/blob/dev-ai-contest-2026/zh-cn/contest_2026/ai_coding_log_guide.md>
