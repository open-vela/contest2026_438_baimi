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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <time.h>

#include <lvgl/lvgl.h>

#include "velacare_core.h"
#include "velacare_ui.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define VC_UI_TAG       "velacare_ui"
#define VC_NAV_H        48
#define VC_REMIND_MAX_ROWS VC_MAX_REMINDERS
#define VC_EVENT_MAX_ROWS  8

LV_FONT_DECLARE(velacare_cn_16);
#define VC_UI_CN_FONT   (&velacare_cn_16)

#if defined(CONFIG_LV_FONT_MONTSERRAT_28)
#define VC_UI_NUM_FONT  (&lv_font_montserrat_28)
#else
#define VC_UI_NUM_FONT  (&lv_font_montserrat_24)
#endif

/* 风险等级配色：正常绿 / 关注黄 / 预警橙 / 紧急红 / 离线灰 */
static const lv_color_t g_risk_colors[VC_RISK_MAX] =
{
  { .red = 46,   .green = 204, .blue = 64  },  /* NORMAL */
  { .red = 255,  .green = 220, .blue = 0   },  /* ATTENTION */
  { .red = 255,  .green = 133, .blue = 27  },  /* WARNING */
  { .red = 255,  .green = 65,  .blue = 54  },  /* EMERGENCY */
  { .red = 170,  .green = 170, .blue = 170 }   /* OFFLINE */
};

/****************************************************************************
 * Private Types
 ****************************************************************************/

/****************************************************************************
 * Private Data
 ****************************************************************************/

static bool     g_ui_ready;
static lv_obj_t* g_pages[VC_PAGE_MAX];
static lv_obj_t* g_nav_btns[VC_PAGE_MAX];
static lv_obj_t* g_home_risk;
static lv_obj_t* g_home_time;
static lv_obj_t* g_home_date;
static lv_obj_t* g_home_temp;
static lv_obj_t* g_home_hum;
static lv_obj_t* g_home_gas;
static lv_obj_t* g_home_motion;
static lv_obj_t* g_home_next;
static lv_obj_t* g_home_status;

static lv_obj_t* g_env_temp_bar;
static lv_obj_t* g_env_hum_bar;
static lv_obj_t* g_env_gas_bar;
static lv_obj_t* g_env_temp_val;
static lv_obj_t* g_env_hum_val;
static lv_obj_t* g_env_gas_val;
static lv_obj_t* g_env_threshold;
static lv_obj_t* g_env_motion;

static lv_obj_t* g_remind_list;
static lv_obj_t* g_remind_hint;

static lv_obj_t* g_events_list;
static lv_obj_t* g_events_hint;

static lv_obj_t* g_set_sim_btn;
static lv_obj_t* g_set_motion_btn;
static lv_obj_t* g_set_agent;
static lv_obj_t* g_set_info;
static lv_obj_t* g_status_text;

static lv_obj_t*  g_msgbox;
static vc_event_t g_msgbox_ev;
static lv_timer_t* g_tick_timer;
static lv_timer_t* g_alarm_off_timer;
static char       g_agent_reply[VC_MSG_LEN];
static volatile bool g_agent_reply_ready;
static int g_current_page;

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static void page_create_home(lv_obj_t* parent);
static void page_create_env(lv_obj_t* parent);
static void page_create_remind(lv_obj_t* parent);
static void page_create_events(lv_obj_t* parent);
static void page_create_settings(lv_obj_t* parent);
static void ui_refresh_home(void);
static void ui_refresh_env(void);
static void ui_refresh_remind(void);
static void ui_refresh_events(void);
static void ui_refresh_settings(void);
static void ui_switch_page(int page);
static void ui_show_notification(const vc_event_t* ev);
static void tick_timer_cb(lv_timer_t* timer);
static void nav_event_cb(lv_event_t* e);
static void msgbox_event_cb(lv_event_t* e);
static void remind_row_event_cb(lv_event_t* e);
static void add_reminder_event_cb(lv_event_t* e);
static void settings_event_cb(lv_event_t* e);
static void alarm_off_cb(lv_timer_t* timer);
static void agent_reply_cb(int status, const char* reply, void* cookie);

static lv_obj_t* make_label(lv_obj_t* parent, const char* text,
                            const lv_font_t* font, lv_color_t color);
static lv_obj_t* make_button(lv_obj_t* parent, const char* text,
                             lv_event_cb_t cb, void* user_data);
static void format_time(char* buf, size_t cap, int64_t ts);

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static lv_obj_t* make_label(lv_obj_t* parent, const char* text,
                            const lv_font_t* font, lv_color_t color)
{
  lv_obj_t* label = lv_label_create(parent);

  lv_label_set_text(label, text != NULL ? text : "");
  lv_obj_set_style_text_font(label, font, 0);
  lv_obj_set_style_text_color(label, color, 0);
  return label;
}

static lv_obj_t* make_button(lv_obj_t* parent, const char* text,
                             lv_event_cb_t cb, void* user_data)
{
  lv_obj_t* btn = lv_button_create(parent);

  lv_obj_set_style_pad_all(btn, 4, 0);
  lv_obj_set_style_radius(btn, 6, 0);
  lv_obj_set_style_bg_color(btn, lv_color_hex(0x2f3542), 0);
  lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(btn, 1, 0);
  lv_obj_set_style_border_color(btn, lv_color_hex(0x57606f), 0);
  lv_obj_set_style_text_color(btn, lv_color_white(), 0);
  lv_obj_set_style_text_font(btn, VC_UI_CN_FONT, 0);

  lv_obj_t* label = lv_label_create(btn);
  lv_label_set_text(label, text != NULL ? text : "");

  if (cb != NULL)
    {
      lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, user_data);
    }

  return btn;
}

static void format_time(char* buf, size_t cap, int64_t ts)
{
  struct tm tm;
  time_t t = (time_t)ts;

#if defined(CONFIG_LIBC_LOCALTIME) || defined(CONFIG_LIBC_LOCALTIME_R)
  localtime_r(&t, &tm);
#else
  gmtime_r(&t, &tm);
#endif

  snprintf(buf, cap, "%02d:%02d:%02d",
           tm.tm_hour, tm.tm_min, tm.tm_sec);
}

static void page_create_home(lv_obj_t* parent)
{
  lv_obj_t* box;
  lv_obj_t* row;
  lv_obj_t* label;

  lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_all(parent, 8, 0);
  lv_obj_set_style_pad_row(parent, 6, 0);

  g_home_risk = make_label(parent, "正常", VC_UI_NUM_FONT,
                           g_risk_colors[VC_RISK_NORMAL]);
  lv_obj_set_style_text_font(g_home_risk, VC_UI_CN_FONT, 0);
  lv_obj_set_style_text_align(g_home_risk, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_width(g_home_risk, lv_pct(100));

  g_home_time = make_label(parent, "--:--:--", VC_UI_NUM_FONT,
                           lv_color_white());
  lv_obj_set_style_text_align(g_home_time, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_width(g_home_time, lv_pct(100));

  g_home_date = make_label(parent, "", VC_UI_CN_FONT, lv_color_hex(0xa4b0be));
  lv_obj_set_style_text_align(g_home_date, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_width(g_home_date, lv_pct(100));

  box = lv_obj_create(parent);
  lv_obj_set_width(box, lv_pct(100));
  lv_obj_set_flex_grow(box, 1);
  lv_obj_set_style_pad_all(box, 6, 0);
  lv_obj_set_style_border_width(box, 0, 0);
  lv_obj_set_style_bg_opa(box, LV_OPA_TRANSP, 0);
  lv_obj_set_flex_flow(box, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(box, 4, 0);

  row = lv_obj_create(box);
  lv_obj_set_width(row, lv_pct(100));
  lv_obj_set_style_border_width(row, 0, 0);
  lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  label = make_label(row, "温度", VC_UI_CN_FONT, lv_color_hex(0xa4b0be));
  lv_obj_set_flex_grow(label, 1);
  g_home_temp = make_label(row, "--", VC_UI_NUM_FONT, lv_color_white());

  row = lv_obj_create(box);
  lv_obj_set_width(row, lv_pct(100));
  lv_obj_set_style_border_width(row, 0, 0);
  lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  label = make_label(row, "湿度", VC_UI_CN_FONT, lv_color_hex(0xa4b0be));
  lv_obj_set_flex_grow(label, 1);
  g_home_hum = make_label(row, "--", VC_UI_NUM_FONT, lv_color_white());

  row = lv_obj_create(box);
  lv_obj_set_width(row, lv_pct(100));
  lv_obj_set_style_border_width(row, 0, 0);
  lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  label = make_label(row, "空气", VC_UI_CN_FONT, lv_color_hex(0xa4b0be));
  lv_obj_set_flex_grow(label, 1);
  g_home_gas = make_label(row, "--", VC_UI_NUM_FONT, lv_color_white());

  row = lv_obj_create(box);
  lv_obj_set_width(row, lv_pct(100));
  lv_obj_set_style_border_width(row, 0, 0);
  lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  label = make_label(row, "活动", VC_UI_CN_FONT, lv_color_hex(0xa4b0be));
  lv_obj_set_flex_grow(label, 1);
  g_home_motion = make_label(row, "--", VC_UI_CN_FONT, lv_color_white());

  g_home_next = make_label(parent, "无待办提醒", VC_UI_CN_FONT,
                           lv_color_hex(0xfeca57));
  lv_obj_set_style_text_align(g_home_next, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_width(g_home_next, lv_pct(100));

  g_home_status = make_label(parent, "AI 分析：离线（本地规则）", VC_UI_CN_FONT,
                             lv_color_hex(0xa4b0be));
  lv_obj_set_style_text_align(g_home_status, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_width(g_home_status, lv_pct(100));
}

static void page_create_env(lv_obj_t* parent)
{
  lv_obj_t* bar;

  lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_all(parent, 10, 0);
  lv_obj_set_style_pad_row(parent, 10, 0);

  make_label(parent, "温度", VC_UI_CN_FONT, lv_color_hex(0xa4b0be));
  g_env_temp_val = make_label(parent, "--", VC_UI_NUM_FONT, lv_color_white());
  bar = lv_bar_create(parent);
  lv_obj_set_width(bar, lv_pct(100));
  lv_obj_set_height(bar, 14);
  lv_bar_set_range(bar, 0, 500);   /* 0-50 C */
  g_env_temp_bar = bar;

  make_label(parent, "湿度", VC_UI_CN_FONT, lv_color_hex(0xa4b0be));
  g_env_hum_val = make_label(parent, "--", VC_UI_NUM_FONT, lv_color_white());
  bar = lv_bar_create(parent);
  lv_obj_set_width(bar, lv_pct(100));
  lv_obj_set_height(bar, 14);
  lv_bar_set_range(bar, 0, 1000);  /* 0-100% */
  g_env_hum_bar = bar;

  make_label(parent, "空气异常", VC_UI_CN_FONT, lv_color_hex(0xa4b0be));
  g_env_gas_val = make_label(parent, "--", VC_UI_NUM_FONT, lv_color_white());
  bar = lv_bar_create(parent);
  lv_obj_set_width(bar, lv_pct(100));
  lv_obj_set_height(bar, 14);
  lv_bar_set_range(bar, 0, 1000);  /* 0-100% */
  g_env_gas_bar = bar;

  g_env_motion = make_label(parent, "活动：--", VC_UI_CN_FONT,
                            lv_color_hex(0xfeca57));
  g_env_threshold = make_label(parent, "阈值：高温32℃ / 空气预警50% / 紧急80%",
                               VC_UI_CN_FONT, lv_color_hex(0xa4b0be));
  lv_label_set_long_mode(g_env_threshold, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(g_env_threshold, lv_pct(100));
}

static void page_create_remind(lv_obj_t* parent)
{
  lv_obj_t* head;
  lv_obj_t* label;

  lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_all(parent, 8, 0);
  lv_obj_set_style_pad_row(parent, 6, 0);

  g_remind_hint = make_label(parent, "生活提醒（饮水/服药/作息）",
                             VC_UI_CN_FONT, lv_color_hex(0xa4b0be));

  head = lv_obj_create(parent);
  lv_obj_set_width(head, lv_pct(100));
  lv_obj_set_style_border_width(head, 0, 0);
  lv_obj_set_style_bg_opa(head, LV_OPA_TRANSP, 0);
  lv_obj_set_flex_flow(head, LV_FLEX_FLOW_ROW);
  lv_obj_set_style_pad_column(head, 6, 0);

  make_button(head, "喝水", add_reminder_event_cb, (void*)1);
  make_button(head, "服药", add_reminder_event_cb, (void*)2);
  make_button(head, "休息", add_reminder_event_cb, (void*)3);

  label = make_label(parent, "演示：新增提醒默认设为 2 分钟后",
                     VC_UI_CN_FONT, lv_color_hex(0x747d8c));
  (void)label;

  g_remind_list = lv_obj_create(parent);
  lv_obj_set_width(g_remind_list, lv_pct(100));
  lv_obj_set_flex_grow(g_remind_list, 1);
  lv_obj_set_style_border_width(g_remind_list, 0, 0);
  lv_obj_set_style_bg_opa(g_remind_list, LV_OPA_TRANSP, 0);
  lv_obj_set_flex_flow(g_remind_list, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(g_remind_list, 4, 0);
  lv_obj_set_scroll_dir(g_remind_list, LV_DIR_VER);
}

static void page_create_events(lv_obj_t* parent)
{
  lv_obj_t* label;

  lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_all(parent, 8, 0);
  lv_obj_set_style_pad_row(parent, 6, 0);

  g_events_hint = make_label(parent, "事件记录（风险/提醒/确认）",
                             VC_UI_CN_FONT, lv_color_hex(0xa4b0be));

  g_events_list = lv_obj_create(parent);
  lv_obj_set_width(g_events_list, lv_pct(100));
  lv_obj_set_flex_grow(g_events_list, 1);
  lv_obj_set_style_border_width(g_events_list, 0, 0);
  lv_obj_set_style_bg_opa(g_events_list, LV_OPA_TRANSP, 0);
  lv_obj_set_flex_flow(g_events_list, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(g_events_list, 4, 0);
  lv_obj_set_scroll_dir(g_events_list, LV_DIR_VER);

  label = make_label(parent, "确认事件可在弹窗或本页查看时标记",
                     VC_UI_CN_FONT, lv_color_hex(0x747d8c));
  (void)label;
}

static void page_create_settings(lv_obj_t* parent)
{
  lv_obj_t* box;

  lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_all(parent, 10, 0);
  lv_obj_set_style_pad_row(parent, 8, 0);

  g_set_sim_btn = make_button(parent, "模拟数据：开", settings_event_cb,
                              (void*)10);
  lv_obj_set_width(g_set_sim_btn, lv_pct(100));

  g_set_motion_btn = make_button(parent, "模拟活动：开", settings_event_cb,
                                 (void*)11);
  lv_obj_set_width(g_set_motion_btn, lv_pct(100));

  make_button(parent, "触发演示异常（燃气/高温）", settings_event_cb, (void*)12);
  make_button(parent, "测试告警（3 秒）", settings_event_cb, (void*)13);
  make_button(parent, "清空事件记录", settings_event_cb, (void*)14);

  box = lv_obj_create(parent);
  lv_obj_set_width(box, lv_pct(100));
  lv_obj_set_flex_grow(box, 1);
  lv_obj_set_style_border_width(box, 0, 0);
  lv_obj_set_style_bg_opa(box, LV_OPA_TRANSP, 0);
  lv_obj_set_flex_flow(box, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(box, 4, 0);

  g_set_agent = make_label(box, "AI 分析：离线（本地规则）",
                           VC_UI_CN_FONT, lv_color_hex(0xa4b0be));
  g_status_text = make_label(box, "", VC_UI_CN_FONT,
                             lv_color_hex(0xfeca57));
  g_set_info = make_label(box, "VelaCare v1.0  |  /data/velacare",
                          VC_UI_CN_FONT, lv_color_hex(0x747d8c));
}

static void ui_refresh_home(void)
{
  vc_env_sample_t sample;
  vc_risk_t risk = vc_risk_get_level();
  char buf[64];
  char buf2[128];
  time_t now = time(NULL);
  struct tm tm;
  static const char* week[7] =
  {
    "周日", "周一", "周二", "周三", "周四", "周五", "周六"
  };
  int i;
  int n;
  vc_reminder_t reminders[VC_MAX_REMINDERS];
  const char* next_label = NULL;

#if defined(CONFIG_LIBC_LOCALTIME) || defined(CONFIG_LIBC_LOCALTIME_R)
  localtime_r(&now, &tm);
#else
  gmtime_r(&now, &tm);
#endif

  lv_label_set_text(g_home_risk, vc_risk_label(risk));
  lv_obj_set_style_text_color(g_home_risk, g_risk_colors[risk], 0);

  snprintf(buf, sizeof(buf), "%02d:%02d:%02d",
           tm.tm_hour, tm.tm_min, tm.tm_sec);
  lv_label_set_text(g_home_time, buf);
  snprintf(buf, sizeof(buf), "%d-%02d-%02d %s",
           tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
           week[tm.tm_wday]);
  lv_label_set_text(g_home_date, buf);

  if (vc_sensor_read(&sample))
    {
      snprintf(buf, sizeof(buf), "%.1f ℃",
               (double)sample.temperature_c10 / VC_TEMP_SCALE);
      lv_label_set_text(g_home_temp, buf);

      snprintf(buf, sizeof(buf), "%.0f %%",
               (double)sample.humidity_p10 / VC_PCT_SCALE);
      lv_label_set_text(g_home_hum, buf);

      snprintf(buf, sizeof(buf), "%.0f %%",
               (double)sample.gas_p10 / VC_PCT_SCALE);
      lv_label_set_text(g_home_gas, buf);

      lv_label_set_text(g_home_motion,
                        sample.motion ? "检测到活动" : "暂无活动");
    }
  else
    {
      lv_label_set_text(g_home_temp, "--");
      lv_label_set_text(g_home_hum, "--");
      lv_label_set_text(g_home_gas, "--");
      lv_label_set_text(g_home_motion, "离线");
    }

  n = vc_reminder_get_all(reminders, VC_MAX_REMINDERS);
  for (i = 0; i < n; i++)
    {
      if (reminders[i].enabled)
        {
          next_label = reminders[i].label;
          break;
        }
    }

  if (next_label != NULL)
    {
      snprintf(buf2, sizeof(buf2), "下次提醒：%s", next_label);
    }
  else
    {
      snprintf(buf2, sizeof(buf2), "无待办提醒");
    }

  lv_label_set_text(g_home_next, buf2);

  if (vc_agent_connected())
    {
      lv_label_set_text(g_home_status, "AI 分析：在线");
    }
  else
    {
      lv_label_set_text(g_home_status, "AI 分析：离线（本地规则）");
    }
}

static void ui_refresh_env(void)
{
  vc_env_sample_t sample;
  vc_thresholds_t th;
  char buf[192];

  vc_risk_get_thresholds(&th);

  if (vc_sensor_read(&sample))
    {
      snprintf(buf, sizeof(buf), "%.1f ℃",
               (double)sample.temperature_c10 / VC_TEMP_SCALE);
      lv_label_set_text(g_env_temp_val, buf);
      lv_bar_set_value(g_env_temp_bar, sample.temperature_c10, LV_ANIM_OFF);

      snprintf(buf, sizeof(buf), "%.0f %%",
               (double)sample.humidity_p10 / VC_PCT_SCALE);
      lv_label_set_text(g_env_hum_val, buf);
      lv_bar_set_value(g_env_hum_bar, sample.humidity_p10, LV_ANIM_OFF);

      snprintf(buf, sizeof(buf), "%.0f %%",
               (double)sample.gas_p10 / VC_PCT_SCALE);
      lv_label_set_text(g_env_gas_val, buf);
      lv_bar_set_value(g_env_gas_bar, sample.gas_p10, LV_ANIM_OFF);

      lv_label_set_text(g_env_motion,
                        sample.motion ? "活动：检测到活动" : "活动：暂无活动");
    }
  else
    {
      lv_label_set_text(g_env_temp_val, "--");
      lv_label_set_text(g_env_hum_val, "--");
      lv_label_set_text(g_env_gas_val, "--");
      lv_label_set_text(g_env_motion, "活动：离线");
      lv_bar_set_value(g_env_temp_bar, 0, LV_ANIM_OFF);
      lv_bar_set_value(g_env_hum_bar, 0, LV_ANIM_OFF);
      lv_bar_set_value(g_env_gas_bar, 0, LV_ANIM_OFF);
    }

  snprintf(buf, sizeof(buf),
           "阈值：高温%.0f℃ / 低温%.0f℃ / 湿度%.0f%% / 空气预警%.0f%% 紧急%.0f%% / 无人%lu分钟",
           (double)th.temp_high_c10 / VC_TEMP_SCALE,
           (double)th.temp_low_c10 / VC_TEMP_SCALE,
           (double)th.humidity_high_p10 / VC_PCT_SCALE,
           (double)th.gas_warn_p10 / VC_PCT_SCALE,
           (double)th.gas_alarm_p10 / VC_PCT_SCALE,
           (unsigned long)th.no_motion_min);
  lv_label_set_text(g_env_threshold, buf);
}

static void remind_row_event_cb(lv_event_t* e)
{
  uint32_t id = (uint32_t)(uintptr_t)lv_event_get_user_data(e);
  vc_reminder_t reminders[VC_MAX_REMINDERS];
  int n;
  int i;

  n = vc_reminder_get_all(reminders, VC_MAX_REMINDERS);
  for (i = 0; i < n; i++)
    {
      if (reminders[i].id == id)
        {
          vc_reminder_set_enabled(id, !reminders[i].enabled);
          break;
        }
    }

  ui_refresh_remind();
}

static void add_reminder_event_cb(lv_event_t* e)
{
  int kind = (int)(uintptr_t)lv_event_get_user_data(e);
  vc_reminder_t r;
  time_t now = time(NULL);
  struct tm tm;
  const char* label;

  label = kind == 1 ? "喝水" : (kind == 2 ? "服药" : "休息");

#if defined(CONFIG_LIBC_LOCALTIME) || defined(CONFIG_LIBC_LOCALTIME_R)
  localtime_r(&now, &tm);
#else
  gmtime_r(&now, &tm);
#endif

  memset(&r, 0, sizeof(r));
  snprintf(r.label, sizeof(r.label), "%s", label);
  r.hour = tm.tm_hour;
  r.minute = tm.tm_min + 2;      /* 演示：2 分钟后触发 */
  if (r.minute >= 60)
    {
      r.minute -= 60;
      r.hour = (r.hour + 1) % 24;
    }

  r.enabled = true;
  if (vc_reminder_add(&r) < 0)
    {
      syslog(LOG_WARNING, "%s: reminder add failed (full?)\n", VC_UI_TAG);
      if (g_status_text != NULL)
        {
          lv_label_set_text(g_status_text, "提醒列表已满");
        }
    }
  else if (g_status_text != NULL)
    {
      char msg[128];

      snprintf(msg, sizeof(msg), "已添加 %s 提醒（%02d:%02d）",
               label, r.hour, r.minute);
      lv_label_set_text(g_status_text, msg);
    }

  ui_refresh_remind();
}

static void ui_refresh_remind(void)
{
  vc_reminder_t reminders[VC_MAX_REMINDERS];
  int n;
  int i;

  lv_obj_clean(g_remind_list);
  n = vc_reminder_get_all(reminders, VC_MAX_REMINDERS);

  if (n == 0)
    {
      lv_obj_t* empty = make_label(g_remind_list, "暂无提醒，点上方按钮添加",
                                   VC_UI_CN_FONT, lv_color_hex(0x747d8c));
      (void)empty;
      return;
    }

  for (i = 0; i < n; i++)
    {
      lv_obj_t* row = lv_obj_create(g_remind_list);
      lv_obj_t* label;
      lv_obj_t* btn;
      char buf[128];

      lv_obj_set_width(row, lv_pct(100));
      lv_obj_set_style_border_width(row, 1, 0);
      lv_obj_set_style_border_color(row, lv_color_hex(0x2f3542), 0);
      lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
      lv_obj_set_style_bg_color(row, lv_color_hex(0x1e272e), 0);
      lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
      lv_obj_set_style_pad_all(row, 6, 0);

      snprintf(buf, sizeof(buf), "%s %02d:%02d%s",
               reminders[i].label, reminders[i].hour, reminders[i].minute,
               reminders[i].enabled ? "" : "（停）");
      label = make_label(row, buf, VC_UI_CN_FONT, lv_color_white());
      lv_obj_set_flex_grow(label, 1);

      btn = make_button(row, reminders[i].enabled ? "停用" : "启用",
                        remind_row_event_cb,
                        (void*)(uintptr_t)reminders[i].id);
      (void)btn;
    }
}

static void ui_refresh_events(void)
{
  vc_event_t events[VC_EVENT_MAX_ROWS];
  int n;
  int i;

  lv_obj_clean(g_events_list);
  n = vc_event_get_recent(events, VC_EVENT_MAX_ROWS);

  if (n == 0)
    {
      lv_obj_t* empty = make_label(g_events_list, "暂无事件记录",
                                   VC_UI_CN_FONT, lv_color_hex(0x747d8c));
      (void)empty;
      return;
    }

  for (i = 0; i < n; i++)
    {
      lv_obj_t* row = lv_obj_create(g_events_list);
      lv_obj_t* label;
      lv_obj_t* time_label;
      char buf[VC_MSG_LEN + 32];
      char tbuf[32];

      lv_obj_set_width(row, lv_pct(100));
      lv_obj_set_style_border_width(row, 1, 0);
      lv_obj_set_style_border_color(row, lv_color_hex(0x2f3542), 0);
      lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
      lv_obj_set_style_bg_color(row, lv_color_hex(0x1e272e), 0);
      lv_obj_set_flex_flow(row, LV_FLEX_FLOW_COLUMN);
      lv_obj_set_style_pad_all(row, 5, 0);

      format_time(tbuf, sizeof(tbuf), events[i].timestamp);
      snprintf(buf, sizeof(buf), "[%s] %s%s",
               vc_risk_label(events[i].risk), events[i].message,
               events[i].confirmed ? "（已确认）" : "");
      label = make_label(row, buf, VC_UI_CN_FONT, lv_color_white());
      lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
      lv_obj_set_width(label, lv_pct(100));

      time_label = make_label(row, tbuf, VC_UI_CN_FONT,
                              lv_color_hex(0x747d8c));
      (void)time_label;
    }
}

static void ui_refresh_settings(void)
{
  char buf[128];

  lv_label_set_text(lv_obj_get_child(g_set_sim_btn, 0),
                    vc_sensor_sim_enabled() ? "模拟数据：开" : "模拟数据：关");
  lv_label_set_text(lv_obj_get_child(g_set_motion_btn, 0),
                    vc_sensor_motion_sim() ? "模拟活动：开" : "模拟活动：关");

  if (vc_agent_connected())
    {
      snprintf(buf, sizeof(buf), "AI 分析：在线（MiMo）");
    }
  else
    {
      snprintf(buf, sizeof(buf), "AI 分析：离线（本地规则）");
    }

  lv_label_set_text(g_set_agent, buf);
}

static void settings_event_cb(lv_event_t* e)
{
  int action = (int)(uintptr_t)lv_event_get_user_data(e);
  static bool alarm_test_on;

  switch (action)
    {
      case 10:
        vc_sensor_set_sim(!vc_sensor_sim_enabled());
        break;

      case 11:
        vc_sensor_set_motion(!vc_sensor_motion_sim());
        break;

      case 12:
        vc_sensor_trigger_demo_anomaly();
        if (g_status_text != NULL)
          {
            lv_label_set_text(g_status_text, "已触发演示异常，观察首页风险变化");
          }
        break;

      case 13:
        alarm_test_on = !alarm_test_on;
        vc_alarm_set(alarm_test_on);
        if (g_alarm_off_timer != NULL)
          {
            lv_timer_delete(g_alarm_off_timer);
            g_alarm_off_timer = NULL;
          }

        if (alarm_test_on)
          {
            g_alarm_off_timer = lv_timer_create(alarm_off_cb, 3000, NULL);
            lv_timer_set_repeat_count(g_alarm_off_timer, 1);
          }
        break;

      case 14:
        vc_event_clear_all();
        if (g_status_text != NULL)
          {
            lv_label_set_text(g_status_text, "事件记录已清空");
          }
        break;

      default:
        break;
    }

  ui_refresh_settings();
}

static void alarm_off_cb(lv_timer_t* timer)
{
  (void)timer;
  vc_alarm_set(false);
  g_alarm_off_timer = NULL;
  if (g_status_text != NULL)
    {
      lv_label_set_text(g_status_text, "告警测试完成");
    }
}

static void ui_switch_page(int page)
{
  int i;

  if (page < 0 || page >= VC_PAGE_MAX)
    {
      return;
    }

  g_current_page = page;
  for (i = 0; i < VC_PAGE_MAX; i++)
    {
      if (i == page)
        {
          lv_obj_clear_flag(g_pages[i], LV_OBJ_FLAG_HIDDEN);
        }
      else
        {
          lv_obj_add_flag(g_pages[i], LV_OBJ_FLAG_HIDDEN);
        }
    }
}

static void nav_event_cb(lv_event_t* e)
{
  int page = (int)(uintptr_t)lv_event_get_user_data(e);

  ui_switch_page(page);

  switch (page)
    {
      case VC_PAGE_HOME:
        ui_refresh_home();
        break;

      case VC_PAGE_ENV:
        ui_refresh_env();
        break;

      case VC_PAGE_REMIND:
        ui_refresh_remind();
        break;

      case VC_PAGE_EVENTS:
        ui_refresh_events();
        break;

      case VC_PAGE_SETTINGS:
        ui_refresh_settings();
        break;

      default:
        break;
    }
}

static void ui_show_notification(const vc_event_t* ev)
{
  lv_obj_t* btn;
  char buf[VC_MSG_LEN + 32];

  if (g_msgbox != NULL)
    {
      lv_msgbox_close_async(g_msgbox);
      g_msgbox = NULL;
    }

  g_msgbox_ev = *ev;
  g_msgbox = lv_msgbox_create(NULL);

  if (ev->cat == VC_EVENT_REMINDER)
    {
      lv_msgbox_add_title(g_msgbox, "生活提醒");
    }
  else
    {
      snprintf(buf, sizeof(buf), "风险：%s",
               vc_risk_label(ev->risk));
      lv_msgbox_add_title(g_msgbox, buf);
    }

  lv_msgbox_add_text(g_msgbox, ev->message);

  btn = lv_msgbox_add_footer_button(g_msgbox, "确认");
  lv_obj_add_event_cb(btn, msgbox_event_cb, LV_EVENT_CLICKED, (void*)1);

  if (ev->cat == VC_EVENT_REMINDER && ev->src_id != 0)
    {
      btn = lv_msgbox_add_footer_button(g_msgbox, "稍后5分");
      lv_obj_add_event_cb(btn, msgbox_event_cb, LV_EVENT_CLICKED, (void*)2);
    }
}

static void msgbox_event_cb(lv_event_t* e)
{
  int action = (int)(uintptr_t)lv_event_get_user_data(e);

  if (action == 1)
    {
      vc_event_confirm(g_msgbox_ev.id);
    }
  else if (action == 2)
    {
      vc_reminder_snooze(g_msgbox_ev.src_id, 5);
      vc_event_confirm(g_msgbox_ev.id);
    }

  if (g_msgbox != NULL)
    {
      lv_msgbox_close_async(g_msgbox);
      g_msgbox = NULL;
    }

  ui_refresh_home();
}

static void tick_timer_cb(lv_timer_t* timer)
{
  vc_event_t ev;

  (void)timer;

  vc_core_tick();

  /* 弹出待通知事件（每 tick 最多处理一个，避免弹窗叠加） */
  if (g_msgbox == NULL && vc_core_pending_has())
    {
      if (vc_core_pending_pop(&ev) == 0)
        {
          ui_show_notification(&ev);

          /* 环境风险事件异步请求 AI 解释（失败自动降级为本地建议） */
          if (ev.cat == VC_EVENT_ENV)
            {
              vc_agent_explain(&ev, agent_reply_cb, NULL);
            }
        }
    }

  /* AI 回复就绪且弹窗仍在时，追加显示 */
  if (g_agent_reply_ready && g_msgbox != NULL)
    {
      lv_msgbox_add_text(g_msgbox, g_agent_reply);
      g_agent_reply_ready = false;
    }

  ui_refresh_home();

  if (g_current_page == VC_PAGE_ENV)
    {
      ui_refresh_env();
    }
}

static void agent_reply_cb(int status, const char* reply, void* cookie)
{
  (void)status;
  (void)cookie;

  if (reply == NULL)
    {
      return;
    }

  strncpy(g_agent_reply, reply, sizeof(g_agent_reply) - 1);
  g_agent_reply[sizeof(g_agent_reply) - 1] = '\0';
  g_agent_reply_ready = true;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int vc_ui_init(void)
{
  lv_obj_t* scr;
  lv_obj_t* content;
  lv_obj_t* nav;
  static const char* nav_names[VC_PAGE_MAX] =
  {
    "首页", "环境", "提醒", "事件", "设置"
  };
  int i;

  if (g_ui_ready)
    {
      return 0;
    }

  scr = lv_screen_active();
  lv_obj_set_style_bg_color(scr, lv_color_hex(0x11151c), 0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
  lv_obj_set_flex_flow(scr, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_all(scr, 0, 0);

  content = lv_obj_create(scr);
  lv_obj_set_width(content, lv_pct(100));
  lv_obj_set_flex_grow(content, 1);
  lv_obj_set_style_border_width(content, 0, 0);
  lv_obj_set_style_bg_opa(content, LV_OPA_TRANSP, 0);

  for (i = 0; i < VC_PAGE_MAX; i++)
    {
      g_pages[i] = lv_obj_create(content);
      lv_obj_set_width(g_pages[i], lv_pct(100));
      lv_obj_set_flex_grow(g_pages[i], 1);
      lv_obj_set_style_border_width(g_pages[i], 0, 0);
      lv_obj_set_style_bg_opa(g_pages[i], LV_OPA_TRANSP, 0);
    }

  page_create_home(g_pages[VC_PAGE_HOME]);
  page_create_env(g_pages[VC_PAGE_ENV]);
  page_create_remind(g_pages[VC_PAGE_REMIND]);
  page_create_events(g_pages[VC_PAGE_EVENTS]);
  page_create_settings(g_pages[VC_PAGE_SETTINGS]);

  nav = lv_obj_create(scr);
  lv_obj_set_width(nav, lv_pct(100));
  lv_obj_set_height(nav, VC_NAV_H);
  lv_obj_set_style_border_width(nav, 0, 0);
  lv_obj_set_style_bg_color(nav, lv_color_hex(0x1e272e), 0);
  lv_obj_set_style_bg_opa(nav, LV_OPA_COVER, 0);
  lv_obj_set_flex_flow(nav, LV_FLEX_FLOW_ROW);
  lv_obj_set_style_pad_all(nav, 4, 0);
  lv_obj_set_style_pad_column(nav, 4, 0);

  for (i = 0; i < VC_PAGE_MAX; i++)
    {
      g_nav_btns[i] = make_button(nav, nav_names[i], nav_event_cb,
                                  (void*)(uintptr_t)i);
      lv_obj_set_flex_grow(g_nav_btns[i], 1);
      lv_obj_set_height(g_nav_btns[i], lv_pct(100));
    }

  g_tick_timer = lv_timer_create(tick_timer_cb, 1000, NULL);

  g_current_page = VC_PAGE_HOME;
  ui_switch_page(VC_PAGE_HOME);
  ui_refresh_home();
  ui_refresh_settings();

  g_ui_ready = true;
  syslog(LOG_INFO, "%s: UI initialized\n", VC_UI_TAG);
  return 0;
}

void vc_ui_deinit(void)
{
  if (!g_ui_ready)
    {
      return;
    }

  if (g_tick_timer != NULL)
    {
      lv_timer_delete(g_tick_timer);
      g_tick_timer = NULL;
    }

  if (g_alarm_off_timer != NULL)
    {
      lv_timer_delete(g_alarm_off_timer);
      g_alarm_off_timer = NULL;
    }

  if (g_msgbox != NULL)
    {
      lv_msgbox_close_async(g_msgbox);
      g_msgbox = NULL;
    }

  g_ui_ready = false;
}

void vc_ui_navigate_to(int page)
{
  ui_switch_page(page);

  switch (page)
    {
      case VC_PAGE_HOME:
        ui_refresh_home();
        break;

      case VC_PAGE_ENV:
        ui_refresh_env();
        break;

      case VC_PAGE_REMIND:
        ui_refresh_remind();
        break;

      case VC_PAGE_EVENTS:
        ui_refresh_events();
        break;

      case VC_PAGE_SETTINGS:
        ui_refresh_settings();
        break;

      default:
        break;
    }
}
