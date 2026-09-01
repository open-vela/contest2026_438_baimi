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

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <syslog.h>
#include <time.h>
#include <unistd.h>

#include <netutils/cJSON.h>

#include "velacare_core.h"
#include "velacare_skills.h"

#ifdef CONFIG_VELACARE_ENABLE_AGENT
#include <velaclaw/client.h>
#endif

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define VC_TAG              "velacare"
#if defined(__GNUC__) || defined(__clang__)
#define VC_WEAK             __attribute__((weak))
#else
#define VC_WEAK
#endif
#define VC_PATH_MAX         256
#define VC_DATA_DIR_MAX     64
#define VC_JSON_VERSION     1
#define VC_EVENTS_FILE      "events.json"
#define VC_REMINDERS_FILE   "reminders.json"
#define VC_SETTINGS_FILE    "settings.json"
#define VC_TMP_SUFFIX       ".tmp"
#define VC_SKILLS_DIR       "/data/agent/skills"

#ifndef CONFIG_VELACARE_DATA_DIR
#define CONFIG_VELACARE_DATA_DIR "/data/velacare"
#endif

#ifndef CONFIG_VELACARE_POLL_MS
#define CONFIG_VELACARE_POLL_MS 5000
#endif

#ifndef CONFIG_VELACARE_SIM_SENSOR
#define CONFIG_VELACARE_SIM_SENSOR 1
#endif

/* 无 localtime 时回退到 gmtime（功能等价，时区固定 UTC） */
#if defined(CONFIG_LIBC_LOCALTIME) || defined(CONFIG_LIBC_LOCALTIME_R)
#define VC_LOCALTIME_R localtime_r
#else
#define VC_LOCALTIME_R gmtime_r
#endif

/****************************************************************************
 * Private Types
 ****************************************************************************/

typedef struct
{
  uint32_t id;
  int64_t  at_epoch;   /* 稍后提醒触发时间 */
} vc_snooze_t;

typedef struct
{
  vc_event_t  events[VC_MAX_EVENTS];
  int         count;
  uint32_t    next_id;
  char        data_dir[VC_DATA_DIR_MAX];
  char        events_path[VC_PATH_MAX];
  char        reminders_path[VC_PATH_MAX];
  char        settings_path[VC_PATH_MAX];
  pthread_mutex_t lock;
  bool        initialized;
  bool        dirty;
} vc_store_t;

/****************************************************************************
 * Private Data
 ****************************************************************************/

static vc_store_t g_store;

static vc_reminder_t g_reminders[VC_MAX_REMINDERS];
static int           g_reminder_count;
static int           g_last_fire_min[VC_MAX_REMINDERS];
static vc_snooze_t   g_snoozes[VC_MAX_REMINDERS];
static int           g_snooze_count;

static vc_event_t    g_pending[VC_MAX_PENDING];
static int           g_pending_head;
static int           g_pending_tail;

static vc_thresholds_t g_thresholds =
{
  .temp_high_c10     = 320,   /* 32.0 C */
  .temp_low_c10      = 100,   /* 10.0 C */
  .humidity_high_p10 = 750,   /* 75% */
  .gas_warn_p10      = 500,   /* 50% */
  .gas_alarm_p10     = 800,   /* 80% */
  .no_motion_min     = 10
};

static bool     g_sim_enabled = true;
static bool     g_motion_sim  = true;
static bool     g_alarm_active;
static vc_risk_t g_risk       = VC_RISK_NORMAL;
static vc_env_sample_t g_env;
static bool     g_env_valid;

static uint32_t g_rng = 0x20260825u;
static int64_t  g_last_poll_epoch;
static int64_t  g_last_motion_epoch;
static int      g_anomaly_kind;    /* 0 无, 1 燃气, 2 高温 */
static int64_t  g_anomaly_until;

#ifdef CONFIG_VELACARE_ENABLE_AGENT
static velaclaw_client_t* g_client;
static bool     g_agent_connected;
#endif

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static int  ensure_directory(const char* path);
static void store_lock(void);
static void store_unlock(void);
static int  store_save_events(void);
static int  store_save_reminders(void);
static int  store_save_settings(void);
static void store_load(void);
static void pending_push(const vc_event_t* ev);
static vc_risk_t evaluate_risk(const vc_env_sample_t* s, int64_t now);
static void risk_on_change(vc_risk_t old_level, vc_risk_t new_level,
                           const char* reason);
static void reminder_check(int64_t now);
static int  build_event_message(vc_risk_t risk, char* buf, size_t cap);
static int  vc_event_log_src(vc_event_cat_t cat, vc_risk_t risk,
                             const char* message, uint32_t src_id);

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static int ensure_directory(const char* path)
{
  struct stat st;

  if (stat(path, &st) == 0)
    {
      if (S_ISDIR(st.st_mode))
        {
          return 0;
        }

      return -ENOTDIR;
    }

  if (mkdir(path, 0777) < 0)
    {
      return -errno;
    }

  return 0;
}

static void store_lock(void)
{
  if (g_store.initialized)
    {
      pthread_mutex_lock(&g_store.lock);
    }
}

static void store_unlock(void)
{
  if (g_store.initialized)
    {
      pthread_mutex_unlock(&g_store.lock);
    }
}

/* 原子写：先写 .tmp 再 rename，避免断电损坏主文件 */
static int atomic_write_json(const char* path, cJSON* root)
{
  char tmp[VC_PATH_MAX];
  char* text;
  int fd;
  int len;
  int wlen;
  int ret = 0;

  text = cJSON_PrintUnformatted(root);
  if (text == NULL)
    {
      return -ENOMEM;
    }

  len = strlen(text);
  snprintf(tmp, sizeof(tmp), "%s%s", path, VC_TMP_SUFFIX);

  fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC, 0666);
  if (fd < 0)
    {
      free(text);
      return -errno;
    }

  wlen = write(fd, text, len);
  close(fd);
  free(text);

  if (wlen != len)
    {
      unlink(tmp);
      return -EIO;
    }

  if (rename(tmp, path) < 0)
    {
      ret = -errno;
      unlink(tmp);
    }

  return ret;
}

static int store_save_events(void)
{
  cJSON* root;
  cJSON* arr;
  int i;
  int ret;

  root = cJSON_CreateObject();
  if (root == NULL)
    {
      return -ENOMEM;
    }

  cJSON_AddNumberToObject(root, "version", VC_JSON_VERSION);
  cJSON_AddNumberToObject(root, "next_id", g_store.next_id);

  arr = cJSON_CreateArray();
  for (i = 0; i < g_store.count; i++)
    {
      cJSON* item = cJSON_CreateObject();

      cJSON_AddNumberToObject(item, "id", g_store.events[i].id);
      cJSON_AddNumberToObject(item, "src_id", g_store.events[i].src_id);
      cJSON_AddNumberToObject(item, "timestamp", g_store.events[i].timestamp);
      cJSON_AddNumberToObject(item, "risk", g_store.events[i].risk);
      cJSON_AddNumberToObject(item, "cat", g_store.events[i].cat);
      cJSON_AddStringToObject(item, "message", g_store.events[i].message);
      cJSON_AddNumberToObject(item, "confirmed",
                              g_store.events[i].confirmed ? 1 : 0);
      cJSON_AddItemToArray(arr, item);
    }

  cJSON_AddItemToObject(root, "events", arr);
  ret = atomic_write_json(g_store.events_path, root);
  cJSON_Delete(root);
  return ret;
}

static int store_save_reminders(void)
{
  cJSON* root;
  cJSON* arr;
  int i;
  int ret;

  root = cJSON_CreateObject();
  if (root == NULL)
    {
      return -ENOMEM;
    }

  cJSON_AddNumberToObject(root, "version", VC_JSON_VERSION);

  arr = cJSON_CreateArray();
  for (i = 0; i < g_reminder_count; i++)
    {
      cJSON* item = cJSON_CreateObject();

      cJSON_AddNumberToObject(item, "id", g_reminders[i].id);
      cJSON_AddStringToObject(item, "label", g_reminders[i].label);
      cJSON_AddNumberToObject(item, "hour", g_reminders[i].hour);
      cJSON_AddNumberToObject(item, "minute", g_reminders[i].minute);
      cJSON_AddNumberToObject(item, "enabled",
                              g_reminders[i].enabled ? 1 : 0);
      cJSON_AddItemToArray(arr, item);
    }

  cJSON_AddItemToObject(root, "reminders", arr);
  ret = atomic_write_json(g_store.reminders_path, root);
  cJSON_Delete(root);
  return ret;
}

static int store_save_settings(void)
{
  cJSON* root;
  cJSON* th;
  int ret;

  root = cJSON_CreateObject();
  if (root == NULL)
    {
      return -ENOMEM;
    }

  cJSON_AddNumberToObject(root, "version", VC_JSON_VERSION);
  cJSON_AddNumberToObject(root, "sim_enabled", g_sim_enabled ? 1 : 0);
  cJSON_AddNumberToObject(root, "motion_sim", g_motion_sim ? 1 : 0);

  th = cJSON_CreateObject();
  cJSON_AddNumberToObject(th, "temp_high_c10", g_thresholds.temp_high_c10);
  cJSON_AddNumberToObject(th, "temp_low_c10", g_thresholds.temp_low_c10);
  cJSON_AddNumberToObject(th, "humidity_high_p10",
                          g_thresholds.humidity_high_p10);
  cJSON_AddNumberToObject(th, "gas_warn_p10", g_thresholds.gas_warn_p10);
  cJSON_AddNumberToObject(th, "gas_alarm_p10", g_thresholds.gas_alarm_p10);
  cJSON_AddNumberToObject(th, "no_motion_min", g_thresholds.no_motion_min);
  cJSON_AddItemToObject(root, "thresholds", th);

  ret = atomic_write_json(g_store.settings_path, root);
  cJSON_Delete(root);
  return ret;
}

static void store_load_events(cJSON* root)
{
  cJSON* arr;
  cJSON* item;
  cJSON* v;
  int i;

  arr = cJSON_GetObjectItem(root, "events");
  if (arr == NULL || !cJSON_IsArray(arr))
    {
      return;
    }

  v = cJSON_GetObjectItem(root, "next_id");
  if (v != NULL)
    {
      g_store.next_id = (uint32_t)v->valuedouble;
    }

  for (i = 0; i < cJSON_GetArraySize(arr) && g_store.count < VC_MAX_EVENTS; i++)
    {
      vc_event_t* ev = &g_store.events[g_store.count];
      cJSON* obj = cJSON_GetArrayItem(arr, i);

      if (obj == NULL)
        {
          continue;
        }

      v = cJSON_GetObjectItem(obj, "id");
      if (v != NULL)
        {
          ev->id = (uint32_t)v->valuedouble;
        }

      v = cJSON_GetObjectItem(obj, "src_id");
      if (v != NULL)
        {
          ev->src_id = (uint32_t)v->valuedouble;
        }

      v = cJSON_GetObjectItem(obj, "timestamp");
      if (v != NULL)
        {
          ev->timestamp = (int64_t)v->valuedouble;
        }

      v = cJSON_GetObjectItem(obj, "risk");
      if (v != NULL)
        {
          ev->risk = (vc_risk_t)v->valuedouble;
        }

      v = cJSON_GetObjectItem(obj, "cat");
      if (v != NULL)
        {
          ev->cat = (vc_event_cat_t)v->valuedouble;
        }

      v = cJSON_GetObjectItem(obj, "confirmed");
      if (v != NULL)
        {
          ev->confirmed = v->valueint != 0;
        }

      item = cJSON_GetObjectItem(obj, "message");
      if (item != NULL && cJSON_IsString(item))
        {
          strncpy(ev->message, item->valuestring, sizeof(ev->message) - 1);
        }

      g_store.count++;
    }
}

static void store_load_reminders(cJSON* root)
{
  cJSON* arr;
  int i;

  arr = cJSON_GetObjectItem(root, "reminders");
  if (arr == NULL || !cJSON_IsArray(arr))
    {
      return;
    }

  for (i = 0; i < cJSON_GetArraySize(arr) && g_reminder_count < VC_MAX_REMINDERS;
       i++)
    {
      vc_reminder_t* r = &g_reminders[g_reminder_count];
      cJSON* obj = cJSON_GetArrayItem(arr, i);
      cJSON* label;

      if (obj == NULL)
        {
          continue;
        }

      r->id = (uint32_t)cJSON_GetObjectItem(obj, "id")->valuedouble;
      r->hour = (int)cJSON_GetObjectItem(obj, "hour")->valuedouble;
      r->minute = (int)cJSON_GetObjectItem(obj, "minute")->valuedouble;
      r->enabled = cJSON_GetObjectItem(obj, "enabled")->valueint != 0;

      label = cJSON_GetObjectItem(obj, "label");
      if (label != NULL && cJSON_IsString(label))
        {
          strncpy(r->label, label->valuestring, sizeof(r->label) - 1);
        }

      g_last_fire_min[g_reminder_count] = -1;
      g_reminder_count++;
    }
}

static void store_load_settings(cJSON* root)
{
  cJSON* th;
  cJSON* v;

  v = cJSON_GetObjectItem(root, "sim_enabled");
  if (v != NULL)
    {
      g_sim_enabled = v->valueint != 0;
    }

  v = cJSON_GetObjectItem(root, "motion_sim");
  if (v != NULL)
    {
      g_motion_sim = v->valueint != 0;
    }

  th = cJSON_GetObjectItem(root, "thresholds");
  if (th != NULL && cJSON_IsObject(th))
    {
      v = cJSON_GetObjectItem(th, "temp_high_c10");
      if (v != NULL)
        {
          g_thresholds.temp_high_c10 = (int32_t)v->valuedouble;
        }

      v = cJSON_GetObjectItem(th, "temp_low_c10");
      if (v != NULL)
        {
          g_thresholds.temp_low_c10 = (int32_t)v->valuedouble;
        }

      v = cJSON_GetObjectItem(th, "humidity_high_p10");
      if (v != NULL)
        {
          g_thresholds.humidity_high_p10 = (int32_t)v->valuedouble;
        }

      v = cJSON_GetObjectItem(th, "gas_warn_p10");
      if (v != NULL)
        {
          g_thresholds.gas_warn_p10 = (int32_t)v->valuedouble;
        }

      v = cJSON_GetObjectItem(th, "gas_alarm_p10");
      if (v != NULL)
        {
          g_thresholds.gas_alarm_p10 = (int32_t)v->valuedouble;
        }

      v = cJSON_GetObjectItem(th, "no_motion_min");
      if (v != NULL)
        {
          g_thresholds.no_motion_min = (uint32_t)v->valuedouble;
        }
    }
}

static int store_load_file(const char* path, cJSON** out)
{
  struct stat st;
  char* buf;
  int fd;
  int nread;

  *out = NULL;

  if (stat(path, &st) != 0)
    {
      return -ENOENT;
    }

  if (st.st_size <= 0 || st.st_size > 65536)
    {
      return -EINVAL;
    }

  buf = malloc(st.st_size + 1);
  if (buf == NULL)
    {
      return -ENOMEM;
    }

  fd = open(path, O_RDONLY);
  if (fd < 0)
    {
      free(buf);
      return -errno;
    }

  nread = read(fd, buf, st.st_size);
  close(fd);
  if (nread != st.st_size)
    {
      free(buf);
      return -EIO;
    }

  buf[nread] = '\0';
  *out = cJSON_Parse(buf);
  free(buf);

  return *out != NULL ? 0 : -EINVAL;
}

static void store_load(void)
{
  cJSON* root;
  int ret;

  ret = store_load_file(g_store.events_path, &root);
  if (ret == 0)
    {
      store_load_events(root);
      cJSON_Delete(root);
    }

  ret = store_load_file(g_store.reminders_path, &root);
  if (ret == 0)
    {
      store_load_reminders(root);
      cJSON_Delete(root);
    }

  ret = store_load_file(g_store.settings_path, &root);
  if (ret == 0)
    {
      store_load_settings(root);
      cJSON_Delete(root);
    }

  if (g_store.next_id == 0)
    {
      g_store.next_id = 1;
    }
}

static void pending_push(const vc_event_t* ev)
{
  int next;

  next = (g_pending_tail + 1) % VC_MAX_PENDING;
  if (next == g_pending_head)
    {
      return;   /* 队列满，丢弃最旧通知（事件已入库） */
    }

  g_pending[g_pending_tail] = *ev;
  g_pending_tail = next;
}

static uint32_t next_rng(void)
{
  g_rng = g_rng * 1103515245u + 12345u;
  return (g_rng >> 16) & 0x7fffffffu;
}

/* 真实传感器驱动钩子：默认无（返回 -1 表示离线）。
 * 接入真实传感器时，在应用中实现同名非 weak 函数即可。 */
VC_WEAK int vc_sensor_hw_read(vc_env_sample_t* out)
{
  (void)out;
  return -1;
}

static void sensor_sim_fill(vc_env_sample_t* s, int64_t now)
{
  uint32_t r;

  s->timestamp = now;
  s->seq++;

  if (g_anomaly_kind == 2 && now < g_anomaly_until)
    {
      s->temperature_c10 = 380 + (int32_t)(next_rng() % 40);   /* 38-42 C */
      s->gas_p10 = 60 + (int32_t)(next_rng() % 80);
    }
  else if (g_anomaly_kind == 1 && now < g_anomaly_until)
    {
      s->gas_p10 = 820 + (int32_t)(next_rng() % 140);          /* 82-96% */
      s->temperature_c10 = 260 + (int32_t)(next_rng() % 60);
    }
  else
    {
      g_anomaly_kind = 0;
      s->temperature_c10 = 230 + (int32_t)(next_rng() % 60);   /* 23-29 C */
      s->gas_p10 = 50 + (int32_t)(next_rng() % 100);           /* 5-15% */
    }

  s->humidity_p10 = 420 + (int32_t)(next_rng() % 180);         /* 42-60% */

  if (g_motion_sim)
    {
      /* 模拟活动：每 8 秒左右出现一次活动信号 */
      s->motion = (now % 8) < 3;
    }
  else
    {
      s->motion = false;
    }

  r = next_rng();
  (void)r;
}

static vc_risk_t evaluate_risk(const vc_env_sample_t* s, int64_t now)
{
  vc_risk_t level = VC_RISK_NORMAL;
  int64_t idle_min;

  if (s == NULL || !g_env_valid)
    {
      return VC_RISK_OFFLINE;
    }

  if (s->temperature_c10 >= g_thresholds.temp_high_c10 + 50)
    {
      level = VC_RISK_EMERGENCY;
    }
  else if (s->temperature_c10 >= g_thresholds.temp_high_c10)
    {
      level = VC_RISK_WARNING;
    }
  else if (s->temperature_c10 <= g_thresholds.temp_low_c10)
    {
      level = VC_RISK_ATTENTION;
    }

  if (s->gas_p10 >= g_thresholds.gas_alarm_p10)
    {
      level = VC_RISK_EMERGENCY;
    }
  else if (s->gas_p10 >= g_thresholds.gas_warn_p10 && level < VC_RISK_WARNING)
    {
      level = VC_RISK_WARNING;
    }

  if (s->humidity_p10 >= g_thresholds.humidity_high_p10 &&
      level < VC_RISK_ATTENTION)
    {
      level = VC_RISK_ATTENTION;
    }

  if (g_motion_sim && s->motion)
    {
      g_last_motion_epoch = now;
    }

  /* 未开启活动模拟时视为无人活动，按阈值升级 */
  if (!g_motion_sim)
    {
      idle_min = (now - g_last_motion_epoch) / 60;
      if (idle_min >= (int64_t)(g_thresholds.no_motion_min * 2))
        {
          if (level < VC_RISK_WARNING)
            {
              level = VC_RISK_WARNING;
            }
        }
      else if (idle_min >= (int64_t)g_thresholds.no_motion_min)
        {
          if (level < VC_RISK_ATTENTION)
            {
              level = VC_RISK_ATTENTION;
            }
        }
    }

  return level;
}

static int build_event_message(vc_risk_t risk, char* buf, size_t cap)
{
  switch (risk)
    {
      case VC_RISK_ATTENTION:
        snprintf(buf, cap, "环境进入关注状态，请留意温湿度与活动情况");
        break;

      case VC_RISK_WARNING:
        snprintf(buf, cap, "环境异常预警：请检查通风、燃气和老人状态");
        break;

      case VC_RISK_EMERGENCY:
        snprintf(buf, cap, "紧急！请立即确认老人安全并检查燃气/烟雾");
        break;

      case VC_RISK_OFFLINE:
        snprintf(buf, cap, "传感器离线，请检查电源和连接");
        break;

      default:
        snprintf(buf, cap, "环境恢复正常");
        break;
    }

  return 0;
}

static void risk_on_change(vc_risk_t old_level, vc_risk_t new_level,
                           const char* reason)
{
  char msg[VC_MSG_LEN];

  if (new_level == old_level)
    {
      return;
    }

  if (new_level == VC_RISK_NORMAL)
    {
      snprintf(msg, sizeof(msg), "风险解除：%s", reason != NULL ? reason : "");
      vc_event_log(VC_EVENT_ENV, VC_RISK_NORMAL, msg);
      vc_alarm_set(false);
      return;
    }

  build_event_message(new_level, msg, sizeof(msg));
  vc_event_log(VC_EVENT_ENV, new_level, msg);

  if (new_level == VC_RISK_WARNING || new_level == VC_RISK_EMERGENCY)
    {
      vc_alarm_set(true);
    }
}

static void reminder_check(int64_t now)
{
  struct tm tm_now;
  time_t t = (time_t)now;
  int minute_of_day;
  int i;
  char msg[VC_MSG_LEN];
  vc_event_t ev;

  VC_LOCALTIME_R(&t, &tm_now);
  minute_of_day = tm_now.tm_hour * 60 + tm_now.tm_min;

  for (i = 0; i < g_reminder_count; i++)
    {
      vc_reminder_t* r = &g_reminders[i];
      int rm = r->hour * 60 + r->minute;

      if (!r->enabled)
        {
          continue;
        }

      /* 过了触发分钟即复位，保证次日同一时间再次触发 */
      if (rm != minute_of_day)
        {
          if (g_last_fire_min[i] != -1)
            {
              g_last_fire_min[i] = -1;
            }

          continue;
        }

      if (g_last_fire_min[i] == rm)
        {
          continue;
        }

      g_last_fire_min[i] = rm;
      snprintf(msg, sizeof(msg), "生活提醒：%s 时间到了", r->label);

      memset(&ev, 0, sizeof(ev));
      ev.src_id = r->id;
      ev.cat = VC_EVENT_REMINDER;
      ev.risk = VC_RISK_ATTENTION;
      strncpy(ev.message, msg, sizeof(ev.message) - 1);
      vc_event_log_src(ev.cat, ev.risk, ev.message, ev.src_id);
    }

  /* 稍后提醒队列 */
  for (i = 0; i < g_snooze_count; )
    {
      if (now >= g_snoozes[i].at_epoch)
        {
          uint32_t rid = g_snoozes[i].id;
          int j;
          const char* label = "事项";

          for (j = 0; j < g_reminder_count; j++)
            {
              if (g_reminders[j].id == rid)
                {
                  label = g_reminders[j].label;
                  break;
                }
            }

          snprintf(msg, sizeof(msg), "稍后提醒：%s，请及时确认", label);
          vc_event_log_src(VC_EVENT_REMINDER, VC_RISK_ATTENTION, msg, rid);

          g_snoozes[i] = g_snoozes[g_snooze_count - 1];
          g_snooze_count--;
        }
      else
        {
          i++;
        }
    }
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int vc_core_init(const char* data_dir)
{
  int ret;

  if (g_store.initialized)
    {
      return 0;
    }

  memset(&g_store, 0, sizeof(g_store));
  g_reminder_count = 0;
  g_snooze_count = 0;
  g_pending_head = 0;
  g_pending_tail = 0;
  g_risk = VC_RISK_NORMAL;
  g_alarm_active = false;
  g_env_valid = false;
  memset(g_last_fire_min, 0, sizeof(g_last_fire_min));

  if (data_dir == NULL)
    {
      data_dir = CONFIG_VELACARE_DATA_DIR;
    }

  snprintf(g_store.data_dir, sizeof(g_store.data_dir), "%s", data_dir);
  snprintf(g_store.events_path, sizeof(g_store.events_path),
           "%s/%s", g_store.data_dir, VC_EVENTS_FILE);
  snprintf(g_store.reminders_path, sizeof(g_store.reminders_path),
           "%s/%s", g_store.data_dir, VC_REMINDERS_FILE);
  snprintf(g_store.settings_path, sizeof(g_store.settings_path),
           "%s/%s", g_store.data_dir, VC_SETTINGS_FILE);

  ret = ensure_directory(g_store.data_dir);
  if (ret < 0)
    {
      syslog(LOG_ERR, "%s: cannot create data dir %s: %d\n",
             VC_TAG, g_store.data_dir, ret);
      return ret;
    }

  pthread_mutex_init(&g_store.lock, NULL);
  g_store.initialized = true;
  g_store.next_id = 1;

#if CONFIG_VELACARE_SIM_SENSOR
  g_sim_enabled = true;
#else
  g_sim_enabled = false;
#endif

  store_load();

  g_last_motion_epoch = time(NULL);
  g_last_poll_epoch = 0;
  g_rng = (uint32_t)time(NULL) ^ 0x9e3779b9u;

  /* 安装内置 Skill（失败不影响核心功能） */
  ret = vc_skills_install();
  if (ret < 0)
    {
      syslog(LOG_WARNING, "%s: skills install failed: %d\n", VC_TAG, ret);
    }

  syslog(LOG_INFO, "%s: core initialized (dir=%s, events=%d, reminders=%d)\n",
         VC_TAG, g_store.data_dir, g_store.count, g_reminder_count);
  return 0;
}

void vc_core_deinit(void)
{
  if (!g_store.initialized)
    {
      return;
    }

  store_lock();
  if (g_store.dirty)
    {
      store_save_events();
      store_save_reminders();
      store_save_settings();
      g_store.dirty = false;
    }
  store_unlock();

  pthread_mutex_destroy(&g_store.lock);
  g_store.initialized = false;
}

void vc_core_flush(void)
{
  store_lock();
  if (g_store.dirty)
    {
      store_save_events();
      store_save_reminders();
      store_save_settings();
      g_store.dirty = false;
    }
  store_unlock();
}

void vc_core_tick(void)
{
  int64_t now;

  if (!g_store.initialized)
    {
      return;
    }

  now = time(NULL);

  /* 传感器轮询 */
  if (now - g_last_poll_epoch >= CONFIG_VELACARE_POLL_MS / 1000)
    {
      vc_env_sample_t sample;
      vc_risk_t old_level;
      vc_risk_t new_level;
      bool have = false;

      memset(&sample, 0, sizeof(sample));

      if (g_sim_enabled)
        {
          sensor_sim_fill(&sample, now);
          have = true;
        }
      else if (vc_sensor_hw_read(&sample) == 0)
        {
          have = true;
          sample.timestamp = now;
        }

      if (have)
        {
          g_env = sample;
          g_env_valid = true;
        }
      else
        {
          g_env_valid = false;
        }

      old_level = g_risk;
      new_level = evaluate_risk(&g_env, now);
      g_risk = new_level;
      risk_on_change(old_level, new_level, NULL);
      g_last_poll_epoch = now;
    }

  reminder_check(now);

  /* 告警自动解除：等级回落后关闭声光 */
  if (g_alarm_active && g_risk < VC_RISK_WARNING)
    {
      vc_alarm_set(false);
    }

  /* 每 30 秒落盘一次（脏标记合并写入） */
  if (g_store.dirty && (now % 30) == 0)
    {
      vc_core_flush();
    }
}

void vc_sensor_set_sim(bool enabled)
{
  g_sim_enabled = enabled;
  if (enabled)
    {
      g_env_valid = true;
    }

  store_lock();
  g_store.dirty = true;
  store_unlock();
}

bool vc_sensor_sim_enabled(void)
{
  return g_sim_enabled;
}

bool vc_sensor_motion_sim(void)
{
  return g_motion_sim;
}

void vc_sensor_set_motion(bool present)
{
  g_motion_sim = present;
  g_last_motion_epoch = time(NULL);

  store_lock();
  g_store.dirty = true;
  store_unlock();
}

void vc_sensor_trigger_demo_anomaly(void)
{
  int64_t now = time(NULL);

  /* 交替触发燃气异常与高温异常，便于演示两种风险 */
  g_anomaly_kind = (g_anomaly_kind == 1) ? 2 : 1;
  g_anomaly_until = now + 60;
  g_last_poll_epoch = 0;
  syslog(LOG_INFO, "%s: demo anomaly triggered (kind=%d, until=%lld)\n",
         VC_TAG, g_anomaly_kind, (long long)g_anomaly_until);
}

bool vc_sensor_read(vc_env_sample_t* out)
{
  if (out == NULL || !g_env_valid)
    {
      return false;
    }

  *out = g_env;
  return true;
}

vc_risk_t vc_risk_get_level(void)
{
  return g_risk;
}

const char* vc_risk_name(vc_risk_t risk)
{
  static const char* names[VC_RISK_MAX] =
  {
    "NORMAL", "ATTENTION", "WARNING", "EMERGENCY", "OFFLINE"
  };

  if (risk >= VC_RISK_MAX)
    {
      risk = VC_RISK_NORMAL;
    }

  return names[risk];
}

const char* vc_risk_label(vc_risk_t risk)
{
  static const char* labels[VC_RISK_MAX] =
  {
    "正常", "关注", "预警", "紧急", "离线"
  };

  if (risk >= VC_RISK_MAX)
    {
      risk = VC_RISK_NORMAL;
    }

  return labels[risk];
}

void vc_risk_get_thresholds(vc_thresholds_t* out)
{
  if (out != NULL)
    {
      *out = g_thresholds;
    }
}

void vc_risk_set_thresholds(const vc_thresholds_t* in)
{
  if (in != NULL)
    {
      g_thresholds = *in;
      store_lock();
      g_store.dirty = true;
      store_unlock();
    }
}

int vc_reminder_add(const vc_reminder_t* r)
{
  int ret;

  if (r == NULL || g_reminder_count >= VC_MAX_REMINDERS)
    {
      return -ENOSPC;
    }

  store_lock();
  g_reminders[g_reminder_count] = *r;
  g_reminders[g_reminder_count].id = g_store.next_id++;
  g_last_fire_min[g_reminder_count] = -1;
  ret = (int)g_reminders[g_reminder_count].id;
  g_reminder_count++;
  g_store.dirty = true;
  store_unlock();

  return ret;
}

int vc_reminder_remove(uint32_t id)
{
  int i;

  store_lock();
  for (i = 0; i < g_reminder_count; i++)
    {
      if (g_reminders[i].id == id)
        {
          g_reminders[i] = g_reminders[g_reminder_count - 1];
          g_last_fire_min[i] = g_last_fire_min[g_reminder_count - 1];
          g_reminder_count--;
          g_store.dirty = true;
          store_unlock();
          return 0;
        }
    }

  store_unlock();
  return -ENOENT;
}

int vc_reminder_set_enabled(uint32_t id, bool enabled)
{
  int i;

  store_lock();
  for (i = 0; i < g_reminder_count; i++)
    {
      if (g_reminders[i].id == id)
        {
          g_reminders[i].enabled = enabled;
          g_last_fire_min[i] = -1;
          g_store.dirty = true;
          store_unlock();
          return 0;
        }
    }

  store_unlock();
  return -ENOENT;
}

int vc_reminder_count(void)
{
  return g_reminder_count;
}

int vc_reminder_get_all(vc_reminder_t* out, int max)
{
  int n = g_reminder_count < max ? g_reminder_count : max;

  if (out != NULL)
    {
      memcpy(out, g_reminders, sizeof(vc_reminder_t) * n);
    }

  return n;
}

int vc_reminder_snooze(uint32_t id, int minutes)
{
  int i;

  if (minutes <= 0 || minutes > 120)
    {
      return -EINVAL;
    }

  store_lock();
  for (i = 0; i < g_reminder_count; i++)
    {
      if (g_reminders[i].id == id)
        {
          if (g_snooze_count < VC_MAX_REMINDERS)
            {
              g_snoozes[g_snooze_count].id = id;
              g_snoozes[g_snooze_count].at_epoch =
                time(NULL) + (int64_t)minutes * 60;
              g_snooze_count++;
            }

          store_unlock();
          return 0;
        }
    }

  store_unlock();
  return -ENOENT;
}

static int vc_event_log_src(vc_event_cat_t cat, vc_risk_t risk,
                            const char* message, uint32_t src_id)
{
  vc_event_t ev;

  if (!g_store.initialized)
    {
      return -EPERM;
    }

  memset(&ev, 0, sizeof(ev));
  ev.id = g_store.next_id++;
  ev.src_id = src_id;
  ev.timestamp = time(NULL);
  ev.cat = cat;
  ev.risk = risk;

  if (message != NULL)
    {
      strncpy(ev.message, message, sizeof(ev.message) - 1);
    }

  store_lock();
  if (g_store.count < VC_MAX_EVENTS)
    {
      g_store.events[g_store.count++] = ev;
    }
  else
    {
      memmove(&g_store.events[0], &g_store.events[1],
              sizeof(vc_event_t) * (VC_MAX_EVENTS - 1));
      g_store.events[VC_MAX_EVENTS - 1] = ev;
    }

  g_store.dirty = true;

  /* 风险与提醒事件进入待通知队列 */
  if (cat == VC_EVENT_ENV || cat == VC_EVENT_REMINDER)
    {
      pending_push(&ev);
    }

  store_unlock();

  syslog(LOG_INFO, "%s: event #%lu [%s] %s\n",
         VC_TAG, (unsigned long)ev.id, vc_risk_name(risk), ev.message);
  return (int)ev.id;
}

int vc_event_log(vc_event_cat_t cat, vc_risk_t risk, const char* message)
{
  return vc_event_log_src(cat, risk, message, 0);
}

int vc_event_count(void)
{
  return g_store.count;
}

int vc_event_get_recent(vc_event_t* out, int max)
{
  int n;
  int i;

  store_lock();
  n = g_store.count < max ? g_store.count : max;
  for (i = 0; i < n; i++)
    {
      out[i] = g_store.events[g_store.count - 1 - i];
    }
  store_unlock();

  return n;
}

int vc_event_confirm(uint32_t id)
{
  int i;

  store_lock();
  for (i = 0; i < g_store.count; i++)
    {
      if (g_store.events[i].id == id)
        {
          g_store.events[i].confirmed = true;
          g_store.dirty = true;
          store_unlock();
          return 0;
        }
    }

  store_unlock();
  return -ENOENT;
}

void vc_event_clear_all(void)
{
  store_lock();
  g_store.count = 0;
  g_store.dirty = true;
  store_unlock();
}

bool vc_core_pending_has(void)
{
  return g_pending_head != g_pending_tail;
}

int vc_core_pending_pop(vc_event_t* out)
{
  store_lock();
  if (g_pending_head == g_pending_tail)
    {
      store_unlock();
      return -ENOENT;
    }

  if (out != NULL)
    {
      *out = g_pending[g_pending_head];
    }

  g_pending_head = (g_pending_head + 1) % VC_MAX_PENDING;
  store_unlock();
  return 0;
}

int vc_agent_init(void)
{
#ifdef CONFIG_VELACARE_ENABLE_AGENT
  g_client = velaclaw_client_open("velacare");
  if (g_client == NULL)
    {
      syslog(LOG_WARNING, "%s: velaclaw_client_open failed, offline mode\n",
             VC_TAG);
      g_agent_connected = false;
      return 0;
    }

  g_agent_connected = true;
  syslog(LOG_INFO, "%s: agent connected\n", VC_TAG);
  return 0;
#else
  syslog(LOG_INFO, "%s: agent integration disabled at build time\n", VC_TAG);
  return 0;
#endif
}

void vc_agent_deinit(void)
{
#ifdef CONFIG_VELACARE_ENABLE_AGENT
  if (g_client != NULL)
    {
      velaclaw_client_close(g_client);
      g_client = NULL;
    }

  g_agent_connected = false;
#endif
}

bool vc_agent_connected(void)
{
#ifdef CONFIG_VELACARE_ENABLE_AGENT
  return g_agent_connected;
#else
  return false;
#endif
}

const char* vc_agent_local_advice(vc_risk_t risk, vc_event_cat_t cat)
{
  if (cat == VC_EVENT_REMINDER)
    {
      return "到提醒时间了，请确认老人已完成该事项；若暂不方便，可稍后再次提醒。";
    }

  switch (risk)
    {
      case VC_RISK_ATTENTION:
        return "环境出现轻微异常：建议检查温湿度和通风，并留意老人的活动状态。";

      case VC_RISK_WARNING:
        return "环境存在风险：请尽快查看现场，开窗通风、检查燃气/烟雾，并确认老人状态。";

      case VC_RISK_EMERGENCY:
        return "环境风险紧急：请立即确认老人安全，必要时联系家属或急救；先关闭燃气阀门并通风。";

      case VC_RISK_OFFLINE:
        return "传感器离线或设备异常：请检查电源、线缆和传感器连接，并确认设备是否正常运行。";

      default:
        return "当前环境正常，无需特别处理；建议保持室内通风和温度适宜。";
    }
}

int vc_agent_explain(const vc_event_t* ev, vc_agent_cb cb, void* cookie)
{
  if (ev == NULL || cb == NULL)
    {
      return -EINVAL;
    }

#ifdef CONFIG_VELACARE_ENABLE_AGENT
  char prompt[512];

  if (!g_agent_connected || g_client == NULL)
    {
      cb(-1, vc_agent_local_advice(ev->risk, ev->cat), cookie);
      return 0;
    }

  snprintf(prompt, sizeof(prompt),
           "你是 VelaCare 居家安全与老人看护助手。请针对下面的事件，"
           "用中文简要说明风险原因，并给出 1-3 条可执行的照护建议，"
           "控制在 100 字以内。\n\n事件：%s\n风险等级：%s",
           ev->message, vc_risk_name(ev->risk));

  {
    velaclaw_ask_req_t req;

    memset(&req, 0, sizeof(req));
    req.text = prompt;
    req.timeout_ms = 20000;
    return velaclaw_ask(g_client, &req, cb, cookie);
  }
#else
  cb(-1, vc_agent_local_advice(ev->risk, ev->cat), cookie);
  return 0;
#endif
}

void vc_alarm_set(bool on)
{
  g_alarm_active = on;

#ifdef CONFIG_VELACARE_ALARM_NODE
  if (strlen(CONFIG_VELACARE_ALARM_NODE) > 0)
    {
      int fd = open(CONFIG_VELACARE_ALARM_NODE, O_WRONLY);

      if (fd >= 0)
        {
          write(fd, on ? "1" : "0", 1);
          close(fd);
        }
      else
        {
          syslog(LOG_WARNING, "%s: cannot open alarm node %s: %d\n",
                 VC_TAG, CONFIG_VELACARE_ALARM_NODE, errno);
        }
    }
#endif

  syslog(LOG_INFO, "%s: alarm %s\n", VC_TAG, on ? "ON" : "OFF");
}

bool vc_alarm_active(void)
{
  return g_alarm_active;
}

int vc_skills_install(void)
{
  static const struct
  {
    const char* name;
    const char* content;
  } skills[] =
  {
    { "home-safety-guard.md",   VC_SKILL_HOME_SAFETY_GUARD },
    { "elder-care-reminder.md", VC_SKILL_ELDER_CARE_REMINDER }
  };

  char path[VC_PATH_MAX];
  int i;
  int ret;

  ret = ensure_directory("/data/agent");
  if (ret < 0)
    {
      return ret;
    }

  ret = ensure_directory(VC_SKILLS_DIR);
  if (ret < 0)
    {
      return ret;
    }

  for (i = 0; i < 2; i++)
    {
      int fd;
      int len = strlen(skills[i].content);
      int wlen;

      snprintf(path, sizeof(path), "%s/%s", VC_SKILLS_DIR, skills[i].name);
      fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
      if (fd < 0)
        {
          return -errno;
        }

      wlen = write(fd, skills[i].content, len);
      close(fd);
      if (wlen != len)
        {
          return -EIO;
        }
    }

  syslog(LOG_INFO, "%s: installed 2 skills to %s\n", VC_TAG, VC_SKILLS_DIR);
  return 0;
}

int vc_selftest(void)
{
  vc_event_t ev;
  vc_reminder_t r;
  vc_thresholds_t th;
  int reminder_id = -1;
  int ret = 0;

  syslog(LOG_INFO, "%s: selftest start\n", VC_TAG);

  vc_risk_get_thresholds(&th);
  if (th.temp_high_c10 != 320)
    {
      syslog(LOG_ERR, "%s: selftest FAIL threshold default\n", VC_TAG);
      ret = -1;
    }

  memset(&r, 0, sizeof(r));
  snprintf(r.label, sizeof(r.label), "自检提醒");
  r.hour = 12;
  r.minute = 30;
  r.enabled = true;
  reminder_id = vc_reminder_add(&r);
  if (reminder_id < 0)
    {
      syslog(LOG_ERR, "%s: selftest FAIL reminder add\n", VC_TAG);
      ret = -1;
    }

  if (vc_event_log(VC_EVENT_ENV, VC_RISK_NORMAL, "selftest event") < 0)
    {
      syslog(LOG_ERR, "%s: selftest FAIL event log\n", VC_TAG);
      ret = -1;
    }

  if (!vc_core_pending_has())
    {
      syslog(LOG_ERR, "%s: selftest FAIL pending queue\n", VC_TAG);
      ret = -1;
    }

  if (vc_core_pending_pop(&ev) != 0)
    {
      syslog(LOG_ERR, "%s: selftest FAIL pending pop\n", VC_TAG);
      ret = -1;
    }

  if (reminder_id >= 0)
    {
      vc_reminder_remove((uint32_t)reminder_id);
    }

  if (vc_agent_local_advice(VC_RISK_WARNING, VC_EVENT_ENV) == NULL)
    {
      syslog(LOG_ERR, "%s: selftest FAIL local advice\n", VC_TAG);
      ret = -1;
    }

  vc_alarm_set(true);
  if (!vc_alarm_active())
    {
      syslog(LOG_ERR, "%s: selftest FAIL alarm\n", VC_TAG);
      ret = -1;
    }

  vc_alarm_set(false);
  vc_core_flush();

  syslog(LOG_INFO, "%s: selftest %s\n", VC_TAG, ret == 0 ? "PASS" : "FAIL");
  return ret;
}
