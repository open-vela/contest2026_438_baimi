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

#ifndef __VELACARE_CORE_H
#define __VELACARE_CORE_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define VC_MAX_EVENTS        64
#define VC_MAX_REMINDERS     12
#define VC_MAX_PENDING       8
#define VC_NAME_LEN          32
#define VC_MSG_LEN           128

/* 数值均放大 10 倍存储，避免浮点运算 */
#define VC_TEMP_SCALE        10
#define VC_PCT_SCALE         10

/****************************************************************************
 * Public Types
 ****************************************************************************/

/* 风险等级状态机 */
typedef enum
{
  VC_RISK_NORMAL = 0,      /* 正常 */
  VC_RISK_ATTENTION,       /* 关注 */
  VC_RISK_WARNING,         /* 预警 */
  VC_RISK_EMERGENCY,       /* 紧急 */
  VC_RISK_OFFLINE,         /* 设备离线/传感器异常 */
  VC_RISK_MAX
} vc_risk_t;

/* 事件类别 */
typedef enum
{
  VC_EVENT_ENV = 0,        /* 环境风险 */
  VC_EVENT_REMINDER,       /* 生活提醒 */
  VC_EVENT_ALARM,          /* 告警动作 */
  VC_EVENT_AGENT,          /* AI 分析 */
  VC_EVENT_SYSTEM,         /* 系统/传感器状态 */
  VC_EVENT_MAX
} vc_event_cat_t;

/* 环境采样数据 */
typedef struct
{
  int32_t  temperature_c10; /* 温度，摄氏 x10 */
  int32_t  humidity_p10;    /* 湿度百分比 x10 */
  int32_t  gas_p10;         /* 空气异常模拟量 0-1000（x10） */
  bool     motion;          /* 是否检测到人员活动 */
  int64_t  timestamp;       /* Unix 秒 */
  uint32_t seq;             /* 采样序号 */
} vc_env_sample_t;

/* 定时提醒（生活日程） */
typedef struct
{
  uint32_t id;
  char     label[VC_NAME_LEN];
  int      hour;            /* 0-23 */
  int      minute;          /* 0-59 */
  bool     enabled;
} vc_reminder_t;

/* 事件记录 */
typedef struct
{
  uint32_t     id;
  uint32_t     src_id;      /* 来源：提醒 ID（REMINDER 事件），环境事件为 0 */
  int64_t      timestamp;
  vc_risk_t    risk;
  vc_event_cat_t cat;
  char         message[VC_MSG_LEN];
  bool         confirmed;   /* 用户/家属已确认处置 */
} vc_event_t;

/* 阈值配置 */
typedef struct
{
  int32_t temp_high_c10;    /* 高温阈值 x10 */
  int32_t temp_low_c10;     /* 低温阈值 x10 */
  int32_t humidity_high_p10;/* 湿度上限 x10 */
  int32_t gas_warn_p10;     /* 空气异常预警 x10 */
  int32_t gas_alarm_p10;    /* 空气异常紧急 x10 */
  uint32_t no_motion_min;   /* 长时间无人活动阈值（分钟） */
} vc_thresholds_t;

/* AI 解释回调 */
typedef void (*vc_agent_cb)(int status, const char* reply, void* cookie);

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

/* ---- 核心初始化 ---- */

int  vc_core_init(const char* data_dir);
void vc_core_deinit(void);
void vc_core_tick(void);      /* 每秒调用一次（UI timer 或独立线程） */
void vc_core_flush(void);     /* 立即落盘 */

/* ---- 传感器 ---- */

void vc_sensor_set_sim(bool enabled);
bool vc_sensor_sim_enabled(void);
bool vc_sensor_motion_sim(void);
void vc_sensor_set_motion(bool present);
void vc_sensor_trigger_demo_anomaly(void);
bool vc_sensor_read(vc_env_sample_t* out);

/* ---- 风险状态机 ---- */

vc_risk_t vc_risk_get_level(void);
const char* vc_risk_name(vc_risk_t risk);
const char* vc_risk_label(vc_risk_t risk);
void vc_risk_get_thresholds(vc_thresholds_t* out);
void vc_risk_set_thresholds(const vc_thresholds_t* in);

/* ---- 生活提醒 ---- */

int  vc_reminder_add(const vc_reminder_t* r);
int  vc_reminder_remove(uint32_t id);
int  vc_reminder_set_enabled(uint32_t id, bool enabled);
int  vc_reminder_count(void);
int  vc_reminder_get_all(vc_reminder_t* out, int max);
int  vc_reminder_snooze(uint32_t id, int minutes);

/* ---- 事件记录 ---- */

int  vc_event_log(vc_event_cat_t cat, vc_risk_t risk, const char* message);
int  vc_event_count(void);
int  vc_event_get_recent(vc_event_t* out, int max);
int  vc_event_confirm(uint32_t id);
void vc_event_clear_all(void);

/* 待通知事件队列（供 UI 轮询弹窗） */
bool vc_core_pending_has(void);
int  vc_core_pending_pop(vc_event_t* out);

/* ---- AI Agent（可选，失败自动降级） ---- */

int  vc_agent_init(void);
void vc_agent_deinit(void);
bool vc_agent_connected(void);
int  vc_agent_explain(const vc_event_t* ev, vc_agent_cb cb, void* cookie);
const char* vc_agent_local_advice(vc_risk_t risk, vc_event_cat_t cat);

/* ---- 声光告警（GPIO/蜂鸣器/LED，未接线时仅记录） ---- */

void vc_alarm_set(bool on);
bool vc_alarm_active(void);

/* ---- 内置 Skill 安装到 /data/agent/skills/ ---- */

int vc_skills_install(void);

/* ---- 自检 ---- */

int vc_selftest(void);

#endif /* __VELACARE_CORE_H */
