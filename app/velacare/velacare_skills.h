/****************************************************************************
 * Copyright (C) 2026 Team baimi (contest2026_438)
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 ****************************************************************************/

#ifndef __VELACARE_SKILLS_H
#define __VELACARE_SKILLS_H

/* VelaCare 内置 Skill。
 * 与仓库内 skills 目录下的 .md 文件保持一致；应用首次启动时由 vc_skills_install()
 * 写入 /data/agent/skills/，ai_agent 启动后即可加载使用。 */

#define VC_SKILL_HOME_SAFETY_GUARD \
  "# home-safety-guard\n\n" \
  "主动式居家安全守护：根据温湿度、空气异常和人员活动数据主动发现风险并给出处置建议。\n\n" \
  "## When to use\n" \
  "- 用户询问家中环境状态或风险\n" \
  "- 检测到高温/低温、湿度异常、燃气/烟雾模拟量超标\n" \
  "- 长时间未检测到人员活动\n" \
  "- 风险等级达到 ATTENTION / WARNING / EMERGENCY\n\n" \
  "## How to use\n" \
  "1. 读取当前环境采样与风险等级（温度、湿度、空气异常、活动状态）\n" \
  "2. 判断触发条件，输出风险等级 NORMAL/ATTENTION/WARNING/EMERGENCY/OFFLINE\n" \
  "3. 对 WARNING 及以上主动调用告警或通知工具\n" \
  "4. 用中文输出 1-3 条可执行建议（通风、关燃气、确认老人状态、联系家属等）\n" \
  "5. 断网或 LLM 不可用时使用本地规则与本地建议，保证离线可用\n\n" \
  "## Example\n" \
  "User: 家里温度怎么样？\n" \
  "→ 读取环境数据：温度 38.5°C，空气异常 82%\n" \
  "→ 风险等级：EMERGENCY\n" \
  "→ 建议：请立即确认老人安全，检查燃气阀门并通风，必要时联系家属或急救。\n"

#define VC_SKILL_ELDER_CARE_REMINDER \
  "# elder-care-reminder\n\n" \
  "主动式老人生活提醒：按预设时间执行饮水、服药、作息提醒，并支持确认与升级。\n\n" \
  "## When to use\n" \
  "- 到达预设的饮水/服药/作息时间\n" \
  "- 用户询问今天有哪些提醒\n" \
  "- 提醒未被确认时进行升级或稍后提醒\n\n" \
  "## How to use\n" \
  "1. 查询预设提醒清单（时间、内容、启用状态）\n" \
  "2. 到点主动推送提醒，等待确认\n" \
  "3. 未确认时支持稍后提醒（默认 5 分钟）与超时升级\n" \
  "4. 记录确认结果与处置过程到事件记录\n" \
  "5. 断网时同样按时推送，不依赖云端\n\n" \
  "## Example\n" \
  "User: 提醒我每天下午3点喝水\n" \
  "→ 创建提醒：喝水 15:00 每天\n" \
  "→ 15:00 主动推送：生活提醒：喝水时间到了\n" \
  "→ 用户确认或稍后5分钟\n"

#endif /* __VELACARE_SKILLS_H */
