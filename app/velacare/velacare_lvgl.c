/****************************************************************************
 * apps/velacare/velacare_lvgl.c - VelaCare Chinese touch interface V2
 ****************************************************************************/

#include <nuttx/config.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>
#include <lvgl/lvgl.h>

#include "velacare_lvgl.h"
#include "velacare_sensor.h"
#include "velacare_settings.h"
#include "velacare_state.h"

#define UI_STACK_SIZE 327680
#define SENSOR_COUNT 4
#define PAGE_COUNT 4
#define PAGE_HOME 0
#define PAGE_DEVICES 1
#define PAGE_HELP 2
#define PAGE_FAMILY 3
#define UI_RESULT_NONE (-2)
#define UI_RESULT_PENDING (-3)

LV_FONT_DECLARE(lv_font_velacare_cn_18);
LV_FONT_DECLARE(lv_font_velacare_cn_28);

struct ui_snapshot_s
{
  int state;
  int alarm_countdown;
  bool online[SENSOR_COUNT];
  bool alarm[SENSOR_COUNT];
  bool gateway_online;
  bool wifi_online;
  bool caregiver_configured;
  char caregiver_masked[9];
  int sos_status;
  int sos_family_reply;
  bool family_seen;
  bool elder_confirmed;
  int care_kind;
  int care_status;
  int family_reply;
  int volume;
  int volume_result;
  int buzzer_test_result;
  uint32_t revision;
};

struct ui_objects_s
{
  lv_obj_t *header;
  lv_obj_t *badge;
  lv_obj_t *page[PAGE_COUNT];
  lv_obj_t *nav_btn[PAGE_COUNT];
  lv_obj_t *nav_label[PAGE_COUNT];

  lv_obj_t *state_title;
  lv_obj_t *state_detail;
  lv_obj_t *home_card[3];
  lv_obj_t *home_device_value;
  lv_obj_t *home_risk_value;
  lv_obj_t *home_network_value;
  lv_obj_t *safe_button;
  lv_obj_t *safe_btn_label;

  lv_obj_t *device_card[SENSOR_COUNT];
  lv_obj_t *device_status[SENSOR_COUNT];
  lv_obj_t *device_time[SENSOR_COUNT];

  lv_obj_t *help_button;
  lv_obj_t *help_btn_label;
  lv_obj_t *help_notice;
  lv_timer_t *help_hold_timer;
  bool help_pressed;
  bool ignore_release_click;

  lv_obj_t *volume_label;
  lv_obj_t *contact_label;
  lv_obj_t *family_status;
  lv_obj_t *family_reply;
  lv_obj_t *family_button[2];
  lv_obj_t *family_button_label[2];
  lv_obj_t *volume_button[3];
  lv_obj_t *setting_notice;
};

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_t g_thread;
static bool g_started;
static struct ui_objects_s *g_ui;
static struct velacare_ui_requests_s g_requests;
static struct ui_snapshot_s g_snapshot =
{
  .state = VELACARE_STATE_NORMAL,
  .alarm_countdown = 0,
  .online = {false, false, false, false},
  .alarm = {false, false, false, false},
  .gateway_online = false,
  .wifi_online = false,
  .caregiver_configured = false,
  .caregiver_masked = "",
  .sos_status = 0,
  .sos_family_reply = 0,
  .care_kind = VELACARE_CARE_NONE,
  .care_status = VELACARE_CARE_IDLE,
  .family_reply = 0,
  .volume = 70,
  .volume_result = UI_RESULT_NONE,
  .buzzer_test_result = UI_RESULT_NONE,
  .revision = 1,
};

static const char *g_sensor_names[SENSOR_COUNT] =
{
  "烟雾", "漏水", "门窗", "跌倒"
};

static const char *g_nav_names[PAGE_COUNT] =
{
  "首页", "设备", "求助", "家人"
};

static lv_obj_t *make_label(lv_obj_t *parent, const char *text,
                            int x, int y, const lv_font_t *font,
                            uint32_t color)
{
  lv_obj_t *label = lv_label_create(parent);

  lv_label_set_text(label, text);
  lv_obj_set_pos(label, x, y);
  lv_obj_set_style_text_font(label, font, 0);
  lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
  return label;
}

static lv_obj_t *make_panel(lv_obj_t *parent, int x, int y,
                            int width, int height,
                            uint32_t color, int radius)
{
  lv_obj_t *panel = lv_obj_create(parent);

  lv_obj_set_pos(panel, x, y);
  lv_obj_set_size(panel, width, height);
  lv_obj_set_style_radius(panel, radius, 0);
  lv_obj_set_style_bg_color(panel, lv_color_hex(color), 0);
  lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(panel, 0, 0);
  lv_obj_set_style_pad_all(panel, 0, 0);
  lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
  return panel;
}

static lv_obj_t *make_summary_card(lv_obj_t *parent, int x,
                                   const char *title, const char *value,
                                   lv_obj_t **value_label)
{
  lv_obj_t *card = make_panel(parent, x, 76, 146, 80, 0xffffff, 10);

  lv_obj_set_style_border_width(card, 2, 0);
  lv_obj_set_style_border_color(card, lv_color_hex(0xd7e3dc), 0);
  make_label(card, title, 12, 8, &lv_font_velacare_cn_18, 0x475569);
  *value_label = make_label(card, value, 12, 39,
                            &lv_font_velacare_cn_28, 0x0f172a);
  return card;
}

static uint32_t state_color(int state)
{
  switch (state)
    {
      case VELACARE_STATE_ATTENTION: return 0xd99a00;
      case VELACARE_STATE_WARNING: return 0xf97316;
      case VELACARE_STATE_EMERGENCY: return 0xdc2626;
      case VELACARE_STATE_OFFLINE: return 0x64748b;
      default: return 0x15803d;
    }
}

static const char *state_title(int state)
{
  switch (state)
    {
      case VELACARE_STATE_ATTENTION: return "设备尚未接入";
      case VELACARE_STATE_WARNING: return "发现家庭风险";
      case VELACARE_STATE_EMERGENCY: return "请立即处理";
      case VELACARE_STATE_OFFLINE: return "网络尚未连接";
      default: return "系统运行正常";
    }
}

static const char *state_detail(int state)
{
  switch (state)
    {
      case VELACARE_STATE_ATTENTION:
        return "请先连接传感器，当前没有监测数据";
      case VELACARE_STATE_WARNING:
        return "系统检测到异常，请及时查看设备";
      case VELACARE_STATE_EMERGENCY:
        return "紧急状态已启动，请立即联系照护人";
      case VELACARE_STATE_OFFLINE:
        return "网络离线，本地守护仍在运行";
      default:
        return "所有设备已连接，家庭守护正在运行";
    }
}

static const char *state_badge(int state)
{
  switch (state)
    {
      case VELACARE_STATE_ATTENTION: return "未接入";
      case VELACARE_STATE_WARNING: return "有风险";
      case VELACARE_STATE_EMERGENCY: return "紧急";
      case VELACARE_STATE_OFFLINE: return "离线";
      default: return "守护中";
    }
}

static void show_page(int selected)
{
  int i;

  if (g_ui == NULL || selected < 0 || selected >= PAGE_COUNT)
    {
      return;
    }

  for (i = 0; i < PAGE_COUNT; i++)
    {
      if (i == selected)
        {
          lv_obj_remove_flag(g_ui->page[i], LV_OBJ_FLAG_HIDDEN);
        }
      else
        {
          lv_obj_add_flag(g_ui->page[i], LV_OBJ_FLAG_HIDDEN);
        }

      lv_obj_set_style_bg_color(g_ui->nav_btn[i],
        lv_color_hex(i == selected ? 0x15803d : 0xe2e8f0), 0);
      lv_obj_set_style_text_color(g_ui->nav_label[i],
        lv_color_hex(i == selected ? 0xffffff : 0x334155), 0);
    }
}

static void nav_event(lv_event_t *event)
{
  if (lv_event_get_code(event) == LV_EVENT_CLICKED)
    {
      show_page((int)(intptr_t)lv_event_get_user_data(event));
    }
}

static bool sos_can_cancel(int status)
{
  return status == VELACARE_SOS_SENDING ||
         status == VELACARE_SOS_WAIT_GATEWAY ||
         status == VELACARE_SOS_NO_CAREGIVER ||
         status == VELACARE_SOS_WAIT_FAMILY ||
         status == VELACARE_SOS_RETRYING;
}

static bool care_is_active(int status)
{
  return status == VELACARE_CARE_SENDING ||
         status == VELACARE_CARE_NO_CAREGIVER ||
         status == VELACARE_CARE_WAIT_FAMILY ||
         status == VELACARE_CARE_CANCELLING ||
         status == VELACARE_CARE_RETRYING;
}

static void help_hold_event(lv_timer_t *timer)
{
  struct ui_objects_s *ui = lv_timer_get_user_data(timer);

  lv_timer_delete(timer);
  ui->help_hold_timer = NULL;
  if (!ui->help_pressed)
    {
      return;
    }

  ui->ignore_release_click = true;
  pthread_mutex_lock(&g_lock);
  g_requests.send_sos = true;
  g_snapshot.sos_status = VELACARE_SOS_SENDING;
  g_snapshot.sos_family_reply = 0;
  g_snapshot.revision++;
  pthread_mutex_unlock(&g_lock);
  lv_label_set_text(ui->help_btn_label, "撤销求助");
  lv_label_set_text(ui->help_notice, "正在通知家庭设备\n请安心等待");
  lv_obj_set_style_text_color(ui->help_notice,
                              lv_color_hex(0xb45309), 0);
  lv_obj_set_style_bg_color(ui->help_button,
                            lv_color_hex(0xea580c), 0);
}

static void help_event(lv_event_t *event)
{
  lv_event_code_t code = lv_event_get_code(event);
  int status;

  if (g_ui == NULL)
    {
      return;
    }

  pthread_mutex_lock(&g_lock);
  status = g_snapshot.sos_status;
  pthread_mutex_unlock(&g_lock);

  if (code == LV_EVENT_PRESSED)
    {
      g_ui->help_pressed = true;
      if (!sos_can_cancel(status) &&
          status != VELACARE_SOS_CANCELLING &&
          g_ui->help_hold_timer == NULL)
        {
          lv_label_set_text(g_ui->help_btn_label, "继续按住...");
          g_ui->help_hold_timer = lv_timer_create(help_hold_event, 1000,
                                                   g_ui);
        }
    }
  else if (code == LV_EVENT_RELEASED)
    {
      g_ui->help_pressed = false;
      if (g_ui->help_hold_timer != NULL)
        {
          lv_timer_delete(g_ui->help_hold_timer);
          g_ui->help_hold_timer = NULL;
          lv_label_set_text(g_ui->help_btn_label, "按住1秒求助");
          pthread_mutex_lock(&g_lock);
          g_snapshot.revision++;
          pthread_mutex_unlock(&g_lock);
        }
    }
  else if (code == LV_EVENT_CLICKED)
    {
      if (g_ui->ignore_release_click)
        {
          g_ui->ignore_release_click = false;
          return;
        }

      if (sos_can_cancel(status))
        {
          pthread_mutex_lock(&g_lock);
          g_requests.cancel_sos = true;
          g_snapshot.sos_status = VELACARE_SOS_CANCELLING;
          g_snapshot.revision++;
          pthread_mutex_unlock(&g_lock);
          lv_label_set_text(g_ui->help_btn_label, "正在撤销...");
          lv_label_set_text(g_ui->help_notice,
                            "正在通知家庭设备撤销求助");
          lv_obj_set_style_text_color(g_ui->help_notice,
                                      lv_color_hex(0xb45309), 0);
        }
    }
}

static void family_request_event(lv_event_t *event)
{
  int kind;
  int current_kind;
  int current_status;
  bool active;

  if (lv_event_get_code(event) != LV_EVENT_CLICKED || g_ui == NULL)
    {
      return;
    }

  kind = (int)(intptr_t)lv_event_get_user_data(event);
  pthread_mutex_lock(&g_lock);
  current_kind = g_snapshot.care_kind;
  current_status = g_snapshot.care_status;
  active = care_is_active(current_status);

  if (active)
    {
      if (current_kind == kind &&
          current_status != VELACARE_CARE_CANCELLING)
        {
          g_requests.cancel_care = true;
          g_snapshot.care_status = VELACARE_CARE_CANCELLING;
          g_snapshot.revision++;
        }

      pthread_mutex_unlock(&g_lock);
      return;
    }

  if (kind == VELACARE_CARE_CONTACT_ME)
    {
      g_requests.request_contact = true;
    }
  else if (kind == VELACARE_CARE_CHECKIN_OK)
    {
      g_requests.send_checkin = true;
    }
  else
    {
      pthread_mutex_unlock(&g_lock);
      return;
    }

  g_snapshot.care_kind = kind;
  g_snapshot.care_status = VELACARE_CARE_SENDING;
  g_snapshot.family_reply = 0;
  g_snapshot.revision++;
  pthread_mutex_unlock(&g_lock);
}

static void volume_event(lv_event_t *event)
{
  int value;

  if (lv_event_get_code(event) != LV_EVENT_CLICKED || g_ui == NULL)
    {
      return;
    }

  value = (int)(intptr_t)lv_event_get_user_data(event);
  pthread_mutex_lock(&g_lock);
  g_requests.apply_volume = true;
  g_requests.volume = value;
  g_snapshot.volume_result = UI_RESULT_PENDING;
  g_snapshot.buzzer_test_result = UI_RESULT_NONE;
  g_snapshot.revision++;
  pthread_mutex_unlock(&g_lock);
  lv_label_set_text(g_ui->setting_notice, "正在调整声音...");
  lv_obj_set_style_text_color(g_ui->setting_notice,
                              lv_color_hex(0xb45309), 0);
}

static void buzzer_test_event(lv_event_t *event)
{
  if (lv_event_get_code(event) != LV_EVENT_CLICKED || g_ui == NULL)
    {
      return;
    }

  pthread_mutex_lock(&g_lock);
  g_requests.test_buzzer = true;
  g_snapshot.buzzer_test_result = UI_RESULT_PENDING;
  g_snapshot.volume_result = UI_RESULT_NONE;
  g_snapshot.revision++;
  pthread_mutex_unlock(&g_lock);
  lv_label_set_text(g_ui->setting_notice, "正在试听提示音...");
  lv_obj_set_style_text_color(g_ui->setting_notice,
                              lv_color_hex(0xb45309), 0);
}

static void safe_event(lv_event_t *event)
{
  if (lv_event_get_code(event) != LV_EVENT_CLICKED || g_ui == NULL)
    {
      return;
    }

  pthread_mutex_lock(&g_lock);
  g_requests.confirm_safe = true;
  pthread_mutex_unlock(&g_lock);
  lv_label_set_text(g_ui->safe_btn_label, "正在确认...");
  lv_obj_remove_flag(g_ui->safe_button, LV_OBJ_FLAG_CLICKABLE);
}

static void create_home_page(struct ui_objects_s *ui)
{
  lv_obj_t *page = ui->page[PAGE_HOME];

  ui->state_title = make_label(page, "设备尚未接入", 14, 5,
                               &lv_font_velacare_cn_28, 0x0f172a);
  ui->state_detail = make_label(page,
                                "请先连接传感器，当前没有监测数据",
                                15, 42, &lv_font_velacare_cn_18, 0x475569);

  ui->home_card[0] = make_summary_card(page, 12, "监测设备", "0 / 4",
                    &ui->home_device_value);
  ui->home_card[1] = make_summary_card(page, 167, "当前风险", "无数据",
                    &ui->home_risk_value);
  ui->home_card[2] = make_summary_card(page, 322, "网络服务", "未配置",
                    &ui->home_network_value);

  ui->safe_button = lv_button_create(page);
  lv_obj_set_pos(ui->safe_button, 12, 76);
  lv_obj_set_size(ui->safe_button, 456, 80);
  lv_obj_set_style_radius(ui->safe_button, 12, 0);
  lv_obj_set_style_bg_color(ui->safe_button, lv_color_hex(0xdc2626), 0);
  lv_obj_set_style_shadow_width(ui->safe_button, 0, 0);
  lv_obj_add_event_cb(ui->safe_button, safe_event, LV_EVENT_CLICKED, NULL);
  lv_obj_add_flag(ui->safe_button, LV_OBJ_FLAG_HIDDEN);

  ui->safe_btn_label = lv_label_create(ui->safe_button);
  lv_label_set_text(ui->safe_btn_label, "确认安全");
  lv_obj_set_style_text_font(ui->safe_btn_label,
                             &lv_font_velacare_cn_28, 0);
  lv_obj_set_style_text_color(ui->safe_btn_label,
                              lv_color_hex(0xffffff), 0);
  lv_obj_center(ui->safe_btn_label);
}

static void create_devices_page(struct ui_objects_s *ui)
{
  lv_obj_t *page = ui->page[PAGE_DEVICES];
  int i;

  make_label(page, "设备状态", 14, 5,
             &lv_font_velacare_cn_28, 0x0f172a);
  make_label(page, "连接真实传感器后，这里将显示实时数据",
             15, 42, &lv_font_velacare_cn_18, 0x475569);

  for (i = 0; i < SENSOR_COUNT; i++)
    {
      int x = 7 + i * 118;

      ui->device_card[i] = make_panel(page, x, 74, 111, 84,
                                      0xf8fafc, 10);
      lv_obj_set_style_border_width(ui->device_card[i], 2, 0);
      lv_obj_set_style_border_color(ui->device_card[i],
                                    lv_color_hex(0xcbd5e1), 0);
      make_label(ui->device_card[i], g_sensor_names[i], 9, 5,
                 &lv_font_velacare_cn_18, 0x334155);
      ui->device_status[i] =
        make_label(ui->device_card[i], "未接入", 9, 32,
                   &lv_font_velacare_cn_18, 0xb45309);
      ui->device_time[i] =
        make_label(ui->device_card[i], "等待连接", 9, 58,
                   &lv_font_velacare_cn_18, 0x64748b);
    }
}

static void create_help_page(struct ui_objects_s *ui)
{
  lv_obj_t *page = ui->page[PAGE_HELP];

  make_label(page, "紧急求助", 14, 5,
             &lv_font_velacare_cn_28, 0x0f172a);
  make_label(page, "有危险时按住按钮，设备会持续联系家人",
              15, 42, &lv_font_velacare_cn_18, 0x475569);

  ui->help_button = lv_button_create(page);
  lv_obj_set_pos(ui->help_button, 15, 72);
  lv_obj_set_size(ui->help_button, 235, 70);
  lv_obj_set_style_radius(ui->help_button, 12, 0);
  lv_obj_set_style_bg_color(ui->help_button, lv_color_hex(0xdc2626), 0);
  lv_obj_set_style_shadow_width(ui->help_button, 0, 0);
  lv_obj_add_event_cb(ui->help_button, help_event, LV_EVENT_ALL, NULL);

  ui->help_btn_label = lv_label_create(ui->help_button);
  lv_label_set_text(ui->help_btn_label, "按住1秒求助");
  lv_obj_set_style_text_font(ui->help_btn_label,
                             &lv_font_velacare_cn_28, 0);
  lv_obj_set_style_text_color(ui->help_btn_label,
                              lv_color_hex(0xffffff), 0);
  lv_obj_center(ui->help_btn_label);

  ui->help_notice = make_label(page, "按住后将通知家庭设备\n并等待家人确认",
                               270, 75, &lv_font_velacare_cn_18,
                               0x64748b);
  lv_obj_set_width(ui->help_notice, 195);
  lv_label_set_long_mode(ui->help_notice, LV_LABEL_LONG_WRAP);
}

static lv_obj_t *create_family_button(lv_obj_t *page, const char *text,
                                      int x, int kind, uint32_t color,
                                      lv_obj_t **label_out)
{
  lv_obj_t *button = lv_button_create(page);
  lv_obj_t *label;

  lv_obj_set_pos(button, x, 39);
  lv_obj_set_size(button, 219, 51);
  lv_obj_set_style_radius(button, 11, 0);
  lv_obj_set_style_bg_color(button, lv_color_hex(color), 0);
  lv_obj_set_style_shadow_width(button, 0, 0);
  lv_obj_add_event_cb(button, family_request_event, LV_EVENT_CLICKED,
                      (void *)(intptr_t)kind);
  label = lv_label_create(button);
  lv_label_set_text(label, text);
  lv_obj_set_style_text_font(label, &lv_font_velacare_cn_18, 0);
  lv_obj_set_style_text_color(label, lv_color_hex(0xffffff), 0);
  lv_obj_center(label);
  *label_out = label;
  return button;
}

static lv_obj_t *create_volume_button(lv_obj_t *page, const char *text,
                                      int x, int value)
{
  lv_obj_t *button = lv_button_create(page);
  lv_obj_t *label;

  lv_obj_set_pos(button, x, 136);
  lv_obj_set_size(button, 58, 29);
  lv_obj_set_style_radius(button, 7, 0);
  lv_obj_set_style_bg_color(button, lv_color_hex(0xe2e8f0), 0);
  lv_obj_set_style_shadow_width(button, 0, 0);
  lv_obj_add_event_cb(button, volume_event, LV_EVENT_CLICKED,
                      (void *)(intptr_t)value);
  label = lv_label_create(button);
  lv_label_set_text(label, text);
  lv_obj_set_style_text_font(label, &lv_font_velacare_cn_18, 0);
  lv_obj_set_style_text_color(label, lv_color_hex(0x334155), 0);
  lv_obj_center(label);
  return button;
}

static void create_family_page(struct ui_objects_s *ui)
{
  lv_obj_t *page = ui->page[PAGE_FAMILY];
  lv_obj_t *test_button;
  lv_obj_t *test_label;

  make_label(page, "家人联系", 14, 3,
              &lv_font_velacare_cn_28, 0x0f172a);

  ui->contact_label = make_label(page, "家人尚未登记", 195, 10,
                                 &lv_font_velacare_cn_18, 0x64748b);
  lv_obj_set_width(ui->contact_label, 270);
  lv_obj_set_style_text_align(ui->contact_label, LV_TEXT_ALIGN_RIGHT, 0);

  ui->family_button[0] = create_family_button(page, "请家人联系我", 14,
    VELACARE_CARE_CONTACT_ME, 0x0f766e, &ui->family_button_label[0]);
  ui->family_button[1] = create_family_button(page, "我今天很好", 247,
    VELACARE_CARE_CHECKIN_OK, 0x15803d, &ui->family_button_label[1]);

  ui->family_status = make_label(page, "可以给家人留言或报平安",
                                 15, 94, &lv_font_velacare_cn_18,
                                 0x475569);
  ui->family_reply = make_label(page, "", 15, 116,
                                &lv_font_velacare_cn_18, 0x15803d);
  lv_obj_set_width(ui->family_reply, 450);

  ui->volume_label = make_label(page, "报警蜂鸣声", 15, 141,
                                 &lv_font_velacare_cn_18, 0x334155);
  ui->volume_button[0] = create_volume_button(page, "较小", 112, 40);
  ui->volume_button[1] = create_volume_button(page, "标准", 175, 70);
  ui->volume_button[2] = create_volume_button(page, "响亮", 238, 100);

  test_button = lv_button_create(page);
  lv_obj_set_pos(test_button, 301, 136);
  lv_obj_set_size(test_button, 62, 29);
  lv_obj_set_style_radius(test_button, 7, 0);
  lv_obj_set_style_bg_color(test_button, lv_color_hex(0x0f766e), 0);
  lv_obj_set_style_shadow_width(test_button, 0, 0);
  lv_obj_add_event_cb(test_button, buzzer_test_event,
                      LV_EVENT_CLICKED, NULL);
  test_label = lv_label_create(test_button);
  lv_label_set_text(test_label, "试听");
  lv_obj_set_style_text_font(test_label, &lv_font_velacare_cn_18, 0);
  lv_obj_set_style_text_color(test_label, lv_color_hex(0xffffff), 0);
  lv_obj_center(test_label);

  ui->setting_notice = make_label(page, "当前：标准",
                                  369, 141, &lv_font_velacare_cn_18,
                                  0x64748b);
  lv_obj_set_width(ui->setting_notice, 98);
}

static void create_ui(struct ui_objects_s *ui)
{
  lv_obj_t *screen = lv_screen_active();
  lv_obj_t *nav;
  int i;

  memset(ui, 0, sizeof(*ui));
  g_ui = ui;

  lv_obj_set_style_bg_color(screen, lv_color_hex(0xf1f5f9), 0);
  lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
  lv_obj_set_style_pad_all(screen, 0, 0);
  lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

  ui->header = make_panel(screen, 0, 0, 480, 52, 0xd99a00, 0);
  make_label(ui->header, "VelaCare", 12, 2,
             &lv_font_montserrat_24, 0xffffff);
  make_label(ui->header, "家庭健康守护", 140, 16,
             &lv_font_velacare_cn_18, 0xfff7d6);
  ui->badge = make_label(ui->header, "未接入", 380, 16,
                         &lv_font_velacare_cn_18, 0xffffff);
  lv_obj_set_width(ui->badge, 86);
  lv_obj_set_style_text_align(ui->badge, LV_TEXT_ALIGN_RIGHT, 0);

  for (i = 0; i < PAGE_COUNT; i++)
    {
      ui->page[i] = make_panel(screen, 0, 52, 480, 168, 0xf1f5f9, 0);
    }

  create_home_page(ui);
  create_devices_page(ui);
  create_help_page(ui);
  create_family_page(ui);

  nav = make_panel(screen, 0, 220, 480, 52, 0xffffff, 0);
  for (i = 0; i < PAGE_COUNT; i++)
    {
      ui->nav_btn[i] = lv_button_create(nav);
      lv_obj_set_pos(ui->nav_btn[i], 4 + i * 119, 4);
      lv_obj_set_size(ui->nav_btn[i], 115, 44);
      lv_obj_set_style_radius(ui->nav_btn[i], 9, 0);
      lv_obj_set_style_shadow_width(ui->nav_btn[i], 0, 0);
      lv_obj_add_event_cb(ui->nav_btn[i], nav_event, LV_EVENT_CLICKED,
                          (void *)(intptr_t)i);
      ui->nav_label[i] = lv_label_create(ui->nav_btn[i]);
      lv_label_set_text(ui->nav_label[i], g_nav_names[i]);
      lv_obj_set_style_text_font(ui->nav_label[i],
                                 &lv_font_velacare_cn_18, 0);
      lv_obj_center(ui->nav_label[i]);
    }

  show_page(PAGE_HOME);
}

static void apply_state(struct ui_objects_s *ui, int state)
{
  lv_obj_set_style_bg_color(ui->header, lv_color_hex(state_color(state)), 0);
  lv_label_set_text(ui->badge, state_badge(state));
  lv_label_set_text(ui->state_title, state_title(state));
  lv_label_set_text(ui->state_detail, state_detail(state));
}

static void apply_care_copy(struct ui_objects_s *ui,
                            const struct ui_snapshot_s *snapshot)
{
  int i;
  const bool fall_alarm = snapshot->alarm[3];

  if (fall_alarm)
    {
      lv_label_set_text(ui->state_title, "发现跌倒");
      if (snapshot->elder_confirmed)
        {
          lv_label_set_text(ui->state_detail, "已确认安全，请先等待");
        }
      else if (snapshot->family_seen)
        {
          lv_label_set_text(ui->state_detail, "家人已看到，请先等待");
        }
      else
        {
          lv_label_set_text(ui->state_detail, "请先等待");
        }

      for (i = 0; i < 3; i++)
        {
          lv_obj_add_flag(ui->home_card[i], LV_OBJ_FLAG_HIDDEN);
        }

      lv_obj_remove_flag(ui->safe_button, LV_OBJ_FLAG_HIDDEN);
      if (snapshot->elder_confirmed)
        {
          lv_label_set_text(ui->safe_btn_label, "已确认");
          lv_obj_set_style_bg_color(ui->safe_button,
                                    lv_color_hex(0x15803d), 0);
          lv_obj_remove_flag(ui->safe_button, LV_OBJ_FLAG_CLICKABLE);
        }
      else
        {
          lv_label_set_text(ui->safe_btn_label, "确认安全");
          lv_obj_set_style_bg_color(ui->safe_button,
                                    lv_color_hex(0xdc2626), 0);
          lv_obj_add_flag(ui->safe_button, LV_OBJ_FLAG_CLICKABLE);
        }

      return;
    }

  for (i = 0; i < 3; i++)
    {
      lv_obj_remove_flag(ui->home_card[i], LV_OBJ_FLAG_HIDDEN);
    }

  lv_obj_add_flag(ui->safe_button, LV_OBJ_FLAG_HIDDEN);
  lv_label_set_text(ui->safe_btn_label, "确认安全");
  lv_obj_set_style_bg_color(ui->safe_button, lv_color_hex(0xdc2626), 0);
  lv_obj_add_flag(ui->safe_button, LV_OBJ_FLAG_CLICKABLE);

  if (snapshot->alarm[0])
    {
      lv_label_set_text(ui->state_title, "烟雾告警");
      lv_label_set_text(ui->state_detail, "请离开");
    }
  else if (snapshot->alarm[1])
    {
      lv_label_set_text(ui->state_title, "漏水告警");
      lv_label_set_text(ui->state_detail, "请离开");
    }
  else if (snapshot->alarm[2])
    {
      lv_label_set_text(ui->state_title, "门窗打开");
      lv_label_set_text(ui->state_detail, "请先确认家里安全");
    }
}

static void apply_sensor(struct ui_objects_s *ui, int index,
                         bool online, bool alarm)
{
  const char *status;
  const char *time_text;
  uint32_t text_color;
  uint32_t border_color;
  uint32_t background;

  if (!online)
    {
      status = "未接入";
      time_text = "等待连接";
      text_color = 0xb45309;
      border_color = 0xcbd5e1;
      background = 0xf8fafc;
    }
  else if (alarm)
    {
      status = "发现异常";
      time_text = "刚刚更新";
      text_color = 0xb91c1c;
      border_color = 0xfca5a5;
      background = 0xfef2f2;
    }
  else
    {
      status = "状态正常";
      time_text = "刚刚更新";
      text_color = 0x15803d;
      border_color = 0xbbf7d0;
      background = 0xf0fdf4;
    }

  lv_label_set_text(ui->device_status[index], status);
  lv_label_set_text(ui->device_time[index], time_text);
  lv_obj_set_style_text_color(ui->device_status[index],
                              lv_color_hex(text_color), 0);
  lv_obj_set_style_border_color(ui->device_card[index],
                                lv_color_hex(border_color), 0);
  lv_obj_set_style_bg_color(ui->device_card[index],
                            lv_color_hex(background), 0);
}

static void set_help_button(struct ui_objects_s *ui, const char *text,
                            uint32_t color, bool clickable)
{
  lv_label_set_text(ui->help_btn_label, text);
  lv_obj_set_style_bg_color(ui->help_button, lv_color_hex(color), 0);
  if (clickable)
    {
      lv_obj_add_flag(ui->help_button, LV_OBJ_FLAG_CLICKABLE);
    }
  else
    {
      lv_obj_remove_flag(ui->help_button, LV_OBJ_FLAG_CLICKABLE);
    }
}

static const char *family_reply_text(int reply);

static void apply_help_flow(struct ui_objects_s *ui,
                            const struct ui_snapshot_s *snapshot)
{
  if (ui->help_pressed && ui->help_hold_timer != NULL)
    {
      return;
    }

  switch (snapshot->sos_status)
    {
      case VELACARE_SOS_SENDING:
      case VELACARE_SOS_WAIT_GATEWAY:
        set_help_button(ui, "撤销求助", 0xea580c, true);
        lv_label_set_text(ui->help_notice,
                          "正在发送求助……\n等待家庭设备确认");
        lv_obj_set_style_text_color(ui->help_notice,
                                    lv_color_hex(0xb45309), 0);
        break;

      case VELACARE_SOS_NO_CAREGIVER:
        set_help_button(ui, "撤销求助", 0xea580c, true);
        lv_label_set_text(ui->help_notice,
                          "家庭设备已收到求助\n家人尚未登记");
        lv_obj_set_style_text_color(ui->help_notice,
                                    lv_color_hex(0xb45309), 0);
        break;

      case VELACARE_SOS_WAIT_FAMILY:
        set_help_button(ui, "撤销求助", 0xea580c, true);
        lv_label_set_text(ui->help_notice,
                          "家庭设备已收到求助\n正在等待家人查看");
        lv_obj_set_style_text_color(ui->help_notice,
                                    lv_color_hex(0xb45309), 0);
        break;

      case VELACARE_SOS_FAMILY_SEEN:
        set_help_button(ui, "按住再次求助", 0x15803d, true);
        if (snapshot->sos_family_reply >= 1 &&
            snapshot->sos_family_reply <= 4)
          {
            lv_label_set_text_fmt(
              ui->help_notice, "%s\n请安心等待",
              family_reply_text(snapshot->sos_family_reply));
          }
        else
          {
            lv_label_set_text(ui->help_notice,
                              "家人已经看到\n请安心等待，家人会尽快联系您");
          }
        lv_obj_set_style_text_color(ui->help_notice,
                                    lv_color_hex(0x15803d), 0);
        break;

      case VELACARE_SOS_CANCELLING:
        set_help_button(ui, "正在撤销...", 0xea580c, false);
        lv_label_set_text(ui->help_notice,
                          "正在通知家庭设备撤销求助");
        lv_obj_set_style_text_color(ui->help_notice,
                                    lv_color_hex(0xb45309), 0);
        break;

      case VELACARE_SOS_CANCELLED:
        set_help_button(ui, "按住1秒求助", 0xdc2626, true);
        lv_label_set_text(ui->help_notice,
                          "求助已撤销\n如仍需要帮助，请再次按住");
        lv_obj_set_style_text_color(ui->help_notice,
                                    lv_color_hex(0x64748b), 0);
        break;

      case VELACARE_SOS_RETRYING:
        set_help_button(ui, "撤销求助", 0xea580c, true);
        lv_label_set_text(ui->help_notice,
                          "家庭设备暂时离线\n正在重试发送求助");
        lv_obj_set_style_text_color(ui->help_notice,
                                    lv_color_hex(0xb91c1c), 0);
        break;

      case VELACARE_SOS_IDLE:
      default:
        set_help_button(ui, "按住1秒求助", 0xdc2626, true);
        if (!snapshot->gateway_online)
          {
            lv_label_set_text(ui->help_notice,
                              "家庭设备暂未连接\n求助会在连接后自动发送");
            lv_obj_set_style_text_color(ui->help_notice,
                                        lv_color_hex(0xb45309), 0);
          }
        else if (snapshot->caregiver_configured)
          {
            lv_label_set_text_fmt(ui->help_notice,
                                  "家庭设备已连接\n家人已登记 · %s",
                                  snapshot->caregiver_masked);
            lv_obj_set_style_text_color(ui->help_notice,
                                        lv_color_hex(0x15803d), 0);
          }
        else
          {
            lv_label_set_text(ui->help_notice,
                              "家庭设备已连接\n家人尚未登记");
            lv_obj_set_style_text_color(ui->help_notice,
                                        lv_color_hex(0x64748b), 0);
          }
        break;
    }
}

static void set_family_button(struct ui_objects_s *ui, int index,
                              const char *text, uint32_t color,
                              bool clickable)
{
  lv_label_set_text(ui->family_button_label[index], text);
  lv_obj_set_style_bg_color(ui->family_button[index],
                            lv_color_hex(color), 0);
  if (clickable)
    {
      lv_obj_add_flag(ui->family_button[index], LV_OBJ_FLAG_CLICKABLE);
    }
  else
    {
      lv_obj_remove_flag(ui->family_button[index], LV_OBJ_FLAG_CLICKABLE);
    }
}

static const char *family_reply_text(int reply)
{
  switch (reply)
    {
      case 1: return "家人回复：已经看到";
      case 2: return "家人回复：会马上联系您";
      case 3: return "家人回复：正在赶来";
      case 4: return "家人回复：请您先休息";
      default: return "";
    }
}

static const char *volume_name(int volume)
{
  if (volume <= 40)
    {
      return "较小";
    }

  if (volume >= 100)
    {
      return "响亮";
    }

  return "标准";
}

static void apply_family_flow(struct ui_objects_s *ui,
                              const struct ui_snapshot_s *snapshot)
{
  bool active = care_is_active(snapshot->care_status);
  int active_index = snapshot->care_kind == VELACARE_CARE_CONTACT_ME ? 0 : 1;

  if (snapshot->caregiver_configured)
    {
      lv_label_set_text_fmt(ui->contact_label, "家人已登记 · %s",
                            snapshot->caregiver_masked);
      lv_obj_set_style_text_color(ui->contact_label,
                                  lv_color_hex(0x15803d), 0);
    }
  else
    {
      lv_label_set_text(ui->contact_label, "家人尚未登记");
      lv_obj_set_style_text_color(ui->contact_label,
                                  lv_color_hex(0x64748b), 0);
    }

  set_family_button(ui, 0, "请家人联系我", 0x0f766e, !active);
  set_family_button(ui, 1, "我今天很好", 0x15803d, !active);
  if (active && (snapshot->care_kind == VELACARE_CARE_CONTACT_ME ||
                 snapshot->care_kind == VELACARE_CARE_CHECKIN_OK))
    {
      set_family_button(ui, active_index,
                        snapshot->care_status == VELACARE_CARE_CANCELLING ?
                          "正在撤销..." : "撤销这条消息",
                        0xea580c,
                        snapshot->care_status != VELACARE_CARE_CANCELLING);
    }

  lv_label_set_text(ui->family_reply, family_reply_text(snapshot->family_reply));
  switch (snapshot->care_status)
    {
      case VELACARE_CARE_SENDING:
        lv_label_set_text(ui->family_status, "正在发送给家庭设备……");
        break;
      case VELACARE_CARE_NO_CAREGIVER:
        lv_label_set_text(ui->family_status,
                          "家庭设备已收到，家人尚未登记");
        break;
      case VELACARE_CARE_WAIT_FAMILY:
        lv_label_set_text(ui->family_status,
                          "家庭设备已收到，正在等待家人查看");
        break;
      case VELACARE_CARE_FAMILY_SEEN:
        lv_label_set_text(ui->family_status,
                          snapshot->care_kind == VELACARE_CARE_CHECKIN_OK ?
                            "家人已经看到您的平安消息" :
                            "家人已经看到您的联系请求");
        break;
      case VELACARE_CARE_CANCELLING:
        lv_label_set_text(ui->family_status, "正在撤销这条消息……");
        break;
      case VELACARE_CARE_CANCELLED:
        lv_label_set_text(ui->family_status, "消息已撤销，可以重新发送");
        lv_label_set_text(ui->family_reply, "");
        break;
      case VELACARE_CARE_RETRYING:
        lv_label_set_text(ui->family_status,
                          "家庭设备暂时离线，正在重试发送");
        break;
      case VELACARE_CARE_IDLE:
      default:
        lv_label_set_text(ui->family_status, "可以给家人留言或报平安");
        lv_label_set_text(ui->family_reply, "");
        break;
    }

  if (snapshot->buzzer_test_result != UI_RESULT_NONE)
    {
      if (snapshot->buzzer_test_result == UI_RESULT_PENDING)
        {
          lv_label_set_text(ui->setting_notice, "正在试听...");
          lv_obj_set_style_text_color(ui->setting_notice,
                                      lv_color_hex(0xb45309), 0);
        }
      else
        {
          lv_label_set_text(ui->setting_notice,
                            snapshot->buzzer_test_result == 0 ?
                              "试听完成" : "蜂鸣器不可用");
          lv_obj_set_style_text_color(ui->setting_notice,
                                      lv_color_hex(
                                        snapshot->buzzer_test_result == 0 ?
                                          0x15803d : 0xb91c1c), 0);
        }
    }
  else if (snapshot->volume_result != UI_RESULT_NONE)
    {
      if (snapshot->volume_result == UI_RESULT_PENDING)
        {
          lv_label_set_text(ui->setting_notice, "正在调整...");
          lv_obj_set_style_text_color(ui->setting_notice,
                                      lv_color_hex(0xb45309), 0);
        }
      else
        {
          lv_label_set_text(ui->setting_notice,
                            snapshot->volume_result == 0 ?
                              "音量已调整" : "调整失败");
          lv_obj_set_style_text_color(ui->setting_notice,
                                      lv_color_hex(snapshot->volume_result == 0 ?
                                        0x15803d : 0xb91c1c), 0);
        }
    }
  else
    {
      lv_label_set_text_fmt(ui->setting_notice, "当前：%s",
                            volume_name(snapshot->volume));
      lv_obj_set_style_text_color(ui->setting_notice,
                                  lv_color_hex(0x64748b), 0);
    }
}

static void apply_snapshot(struct ui_objects_s *ui,
                           const struct ui_snapshot_s *snapshot)
{
  bool any_alarm = false;
  int online_count = 0;
  int i;

  apply_state(ui, snapshot->state);
  if (snapshot->state == VELACARE_STATE_WARNING &&
      snapshot->alarm_countdown > 0 &&
      !snapshot->alarm[3])
    {
      lv_label_set_text_fmt(ui->state_detail, "发现异常，%d 秒后求助",
                            snapshot->alarm_countdown);
    }
  apply_care_copy(ui, snapshot);
  for (i = 0; i < SENSOR_COUNT; i++)
    {
      apply_sensor(ui, i, snapshot->online[i], snapshot->alarm[i]);
      if (snapshot->online[i]) online_count++;
      if (snapshot->alarm[i]) any_alarm = true;
    }

  lv_label_set_text_fmt(ui->home_device_value, "%d / 4", online_count);
  if (any_alarm)
    {
      lv_label_set_text(ui->home_risk_value, "有异常");
      lv_obj_set_style_text_color(ui->home_risk_value,
                                  lv_color_hex(0xb91c1c), 0);
    }
  else if (online_count == 0)
    {
      lv_label_set_text(ui->home_risk_value, "无数据");
      lv_obj_set_style_text_color(ui->home_risk_value,
                                  lv_color_hex(0xb45309), 0);
    }
  else
    {
      lv_label_set_text(ui->home_risk_value, "未发现");
      lv_obj_set_style_text_color(ui->home_risk_value,
                                  lv_color_hex(0x15803d), 0);
    }

  lv_label_set_text(ui->home_network_value,
                    snapshot->wifi_online ? "已连接" : "离线");
  apply_help_flow(ui, snapshot);
  apply_family_flow(ui, snapshot);
}

static void *ui_thread(void *arg)
{
  lv_nuttx_dsc_t info;
  lv_nuttx_result_t result;
  struct ui_objects_s ui;
  struct ui_snapshot_s snapshot;
  uint32_t applied_revision = 0;

  (void)arg;
  if (lv_is_initialized())
    {
      syslog(LOG_ERR, "[VelaCare] LVGL is already initialized\n");
      return NULL;
    }

  lv_init();
  lv_nuttx_dsc_init(&info);
#ifdef CONFIG_LV_USE_NUTTX_LCD
  info.fb_path = "/dev/lcd0";
#endif
  lv_nuttx_init(&info, &result);
  if (result.disp == NULL)
    {
      syslog(LOG_ERR, "[VelaCare] display initialization failed\n");
      lv_deinit();
      return NULL;
    }

  syslog(LOG_NOTICE, "[VelaCare] Chinese touch UI V2 ready: %ldx%ld\n",
         (long)lv_display_get_horizontal_resolution(result.disp),
         (long)lv_display_get_vertical_resolution(result.disp));
  create_ui(&ui);

  while (1)
    {
      uint32_t idle;

      pthread_mutex_lock(&g_lock);
      snapshot = g_snapshot;
      pthread_mutex_unlock(&g_lock);

      if (snapshot.revision != applied_revision)
        {
          apply_snapshot(&ui, &snapshot);
          applied_revision = snapshot.revision;
        }

      idle = lv_timer_handler();
      if (idle < 5) idle = 5;
      else if (idle > 33) idle = 33;
      usleep(idle * 1000);
    }

  return NULL;
}

void velacare_lvgl_init(void)
{
  pthread_attr_t attr;
  int ret;

  pthread_mutex_lock(&g_lock);
  if (g_started)
    {
      pthread_mutex_unlock(&g_lock);
      return;
    }

  g_started = true;
  pthread_mutex_unlock(&g_lock);

  pthread_attr_init(&attr);
  pthread_attr_setstacksize(&attr, UI_STACK_SIZE);
  ret = pthread_create(&g_thread, &attr, ui_thread, NULL);
  pthread_attr_destroy(&attr);
  if (ret != 0)
    {
      pthread_mutex_lock(&g_lock);
      g_started = false;
      pthread_mutex_unlock(&g_lock);
      syslog(LOG_ERR, "[VelaCare] UI thread creation failed: %d\n", ret);
      return;
    }

  pthread_detach(g_thread);
  syslog(LOG_NOTICE, "[VelaCare] Chinese touch UI V2 thread started\n");
}

void velacare_lvgl_update_state(int state)
{
  if (state < VELACARE_STATE_NORMAL || state > VELACARE_STATE_OFFLINE)
    {
      return;
    }

  pthread_mutex_lock(&g_lock);
  if (g_snapshot.state != state)
    {
      g_snapshot.state = state;
      g_snapshot.revision++;
    }
  pthread_mutex_unlock(&g_lock);
}

void velacare_lvgl_update_alarm_countdown(int seconds)
{
  if (seconds < 0)
    {
      seconds = 0;
    }

  pthread_mutex_lock(&g_lock);
  if (g_snapshot.alarm_countdown != seconds)
    {
      g_snapshot.alarm_countdown = seconds;
      g_snapshot.revision++;
    }
  pthread_mutex_unlock(&g_lock);
}

void velacare_lvgl_update_sensors(bool smoke_online, bool smoke_alarm,
                                  bool water_online, bool water_alarm,
                                  bool door_online, bool door_alarm,
                                  bool fall_online, bool fall_alarm,
                                  bool wifi_online)
{
  bool changed;

  pthread_mutex_lock(&g_lock);
  changed = g_snapshot.online[0] != smoke_online ||
            g_snapshot.alarm[0] != smoke_alarm ||
            g_snapshot.online[1] != water_online ||
            g_snapshot.alarm[1] != water_alarm ||
            g_snapshot.online[2] != door_online ||
            g_snapshot.alarm[2] != door_alarm ||
            g_snapshot.online[3] != fall_online ||
            g_snapshot.alarm[3] != fall_alarm ||
            g_snapshot.wifi_online != wifi_online;
  if (changed)
    {
      g_snapshot.online[0] = smoke_online;
      g_snapshot.alarm[0] = smoke_alarm;
      g_snapshot.online[1] = water_online;
      g_snapshot.alarm[1] = water_alarm;
      g_snapshot.online[2] = door_online;
      g_snapshot.alarm[2] = door_alarm;
      g_snapshot.online[3] = fall_online;
      g_snapshot.alarm[3] = fall_alarm;
      g_snapshot.wifi_online = wifi_online;
      g_snapshot.revision++;
    }
  pthread_mutex_unlock(&g_lock);
}

void velacare_lvgl_update_gateway_settings(bool gateway_online,
                                            bool caregiver_configured,
                                            const char *caregiver_masked,
                                            int sos_status,
                                            int sos_family_reply)
{
  char masked[sizeof(g_snapshot.caregiver_masked)];
  bool changed;

  if (caregiver_masked != NULL)
    {
      strlcpy(masked, caregiver_masked, sizeof(masked));
    }
  else
    {
      masked[0] = '\0';
    }

  pthread_mutex_lock(&g_lock);
  changed = g_snapshot.gateway_online != gateway_online ||
            g_snapshot.caregiver_configured != caregiver_configured ||
            strcmp(g_snapshot.caregiver_masked, masked) != 0 ||
            g_snapshot.sos_status != sos_status ||
            g_snapshot.sos_family_reply != sos_family_reply;
  if (changed)
    {
      g_snapshot.gateway_online = gateway_online;
      g_snapshot.caregiver_configured = caregiver_configured;
      strlcpy(g_snapshot.caregiver_masked, masked,
              sizeof(g_snapshot.caregiver_masked));
      g_snapshot.sos_status = sos_status;
      g_snapshot.sos_family_reply = sos_family_reply;
      g_snapshot.revision++;
    }
  pthread_mutex_unlock(&g_lock);
}

void velacare_lvgl_update_care(bool family_seen, bool elder_confirmed)
{
  pthread_mutex_lock(&g_lock);
  if (g_snapshot.family_seen != family_seen ||
      g_snapshot.elder_confirmed != elder_confirmed)
    {
      g_snapshot.family_seen = family_seen;
      g_snapshot.elder_confirmed = elder_confirmed;
      g_snapshot.revision++;
    }
  pthread_mutex_unlock(&g_lock);
}

void velacare_lvgl_update_family_flow(int care_kind, int care_status,
                                      int family_reply)
{
  pthread_mutex_lock(&g_lock);
  if (g_snapshot.care_kind != care_kind ||
      g_snapshot.care_status != care_status ||
      g_snapshot.family_reply != family_reply)
    {
      g_snapshot.care_kind = care_kind;
      g_snapshot.care_status = care_status;
      g_snapshot.family_reply = family_reply;
      g_snapshot.revision++;
    }
  pthread_mutex_unlock(&g_lock);
}

void velacare_lvgl_update_volume(int volume)
{
  if (volume < 0 || volume > 100)
    {
      return;
    }

  pthread_mutex_lock(&g_lock);
  if (g_snapshot.volume != volume)
    {
      g_snapshot.volume = volume;
      g_snapshot.volume_result = UI_RESULT_NONE;
      g_snapshot.revision++;
    }
  pthread_mutex_unlock(&g_lock);
}

void velacare_lvgl_take_requests(struct velacare_ui_requests_s *requests)
{
  if (requests == NULL)
    {
      return;
    }

  pthread_mutex_lock(&g_lock);
  *requests = g_requests;
  memset(&g_requests, 0, sizeof(g_requests));
  pthread_mutex_unlock(&g_lock);
}

void velacare_lvgl_report_volume_result(int result, int volume)
{
  pthread_mutex_lock(&g_lock);
  if (result == 0 && volume >= 0 && volume <= 100)
    {
      g_snapshot.volume = volume;
    }

  g_snapshot.volume_result = result;
  g_snapshot.buzzer_test_result = UI_RESULT_NONE;
  g_snapshot.revision++;
  pthread_mutex_unlock(&g_lock);
}

void velacare_lvgl_report_buzzer_test_result(int result)
{
  pthread_mutex_lock(&g_lock);
  g_snapshot.buzzer_test_result = result;
  g_snapshot.volume_result = UI_RESULT_NONE;
  g_snapshot.revision++;
  pthread_mutex_unlock(&g_lock);
}
