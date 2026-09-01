#ifndef _LVGL_H
#define _LVGL_H

/* 仅供宿主静态检查使用的最小 LVGL 声明，非真实 LVGL 头文件 */

#include <stdint.h>
#include <stdbool.h>

typedef struct _lv_obj_t lv_obj_t;
typedef struct _lv_timer_t lv_timer_t;
typedef struct _lv_event_t lv_event_t;
typedef struct _lv_font_t lv_font_t;
typedef struct _lv_indev_t lv_indev_t;
typedef struct _lv_display_t lv_display_t;

typedef struct
{
  union
  {
    struct
    {
      uint8_t blue;
      uint8_t green;
      uint8_t red;
      uint8_t alpha;
    } ch;
    uint32_t full;
  };
} lv_color_t;

typedef void (*lv_event_cb_t)(lv_event_t* e);
typedef void (*lv_timer_cb_t)(lv_timer_t* timer);

typedef struct
{
  const char* fb_path;
} lv_nuttx_dsc_t;

typedef struct
{
  lv_display_t* disp;
  lv_indev_t* indev;
  lv_indev_t* utouch_indev;
} lv_nuttx_result_t;

enum
{
  LV_FLEX_FLOW_ROW = 0,
  LV_FLEX_FLOW_COLUMN,
  LV_OBJ_FLAG_HIDDEN = 1,
  LV_EVENT_CLICKED = 2,
  LV_ANIM_OFF = 0,
  LV_TEXT_ALIGN_CENTER = 1,
  LV_DIR_VER = 2,
  LV_LABEL_LONG_WRAP = 1,
  LV_INDEV_MODE_TIMER = 1,
  LV_OPA_TRANSP = 0,
  LV_OPA_COVER = 255
};

static inline lv_color_t lv_color_white(void)
{
  lv_color_t c;

  c.ch.red = 255;
  c.ch.green = 255;
  c.ch.blue = 255;
  c.ch.alpha = 255;
  return c;
}

static inline lv_color_t lv_color_hex(uint32_t v)
{
  lv_color_t c;

  c.ch.red = (uint8_t)((v >> 16) & 0xff);
  c.ch.green = (uint8_t)((v >> 8) & 0xff);
  c.ch.blue = (uint8_t)(v & 0xff);
  c.ch.alpha = 255;
  return c;
}

#define lv_pct(x) ((int16_t)(x))

extern const lv_font_t lv_font_montserrat_24;
extern const lv_font_t lv_font_montserrat_28;
extern const lv_font_t lv_font_simsun_16_cjk;

bool lv_is_initialized(void);
void lv_init(void);
void lv_deinit(void);
uint32_t lv_timer_handler(void);

lv_obj_t* lv_screen_active(void);
lv_obj_t* lv_obj_create(lv_obj_t* parent);
void lv_obj_clean(lv_obj_t* obj);
void lv_obj_set_width(lv_obj_t* obj, int16_t w);
void lv_obj_set_height(lv_obj_t* obj, int16_t h);
void lv_obj_set_size(lv_obj_t* obj, int16_t w, int16_t h);
void lv_obj_set_flex_grow(lv_obj_t* obj, int grow);
void lv_obj_set_flex_flow(lv_obj_t* obj, int flow);
void lv_obj_set_scroll_dir(lv_obj_t* obj, int dir);
void lv_obj_add_flag(lv_obj_t* obj, int flag);
void lv_obj_clear_flag(lv_obj_t* obj, int flag);
void lv_obj_add_event_cb(lv_obj_t* obj, lv_event_cb_t cb, int filter,
                         void* user_data);
void* lv_event_get_user_data(lv_event_t* e);

void lv_obj_set_style_bg_color(lv_obj_t* obj, lv_color_t color, int sel);
void lv_obj_set_style_bg_opa(lv_obj_t* obj, int opa, int sel);
void lv_obj_set_style_border_width(lv_obj_t* obj, int width, int sel);
void lv_obj_set_style_border_color(lv_obj_t* obj, lv_color_t color, int sel);
void lv_obj_set_style_text_color(lv_obj_t* obj, lv_color_t color, int sel);
void lv_obj_set_style_text_font(lv_obj_t* obj, const lv_font_t* font, int sel);
void lv_obj_set_style_text_align(lv_obj_t* obj, int align, int sel);
void lv_obj_set_style_pad_all(lv_obj_t* obj, int pad, int sel);
void lv_obj_set_style_pad_row(lv_obj_t* obj, int pad, int sel);
void lv_obj_set_style_pad_column(lv_obj_t* obj, int pad, int sel);
void lv_obj_set_style_radius(lv_obj_t* obj, int radius, int sel);

lv_obj_t* lv_label_create(lv_obj_t* parent);
void lv_label_set_text(lv_obj_t* label, const char* text);
void lv_label_set_long_mode(lv_obj_t* label, int mode);

lv_obj_t* lv_button_create(lv_obj_t* parent);

lv_obj_t* lv_bar_create(lv_obj_t* parent);
void lv_bar_set_range(lv_obj_t* bar, int min, int max);
void lv_bar_set_value(lv_obj_t* bar, int value, int anim);

lv_timer_t* lv_timer_create(lv_timer_cb_t cb, uint32_t period, void* data);
void lv_timer_delete(lv_timer_t* timer);
void lv_timer_set_period(lv_timer_t* timer, uint32_t period);
void lv_timer_set_repeat_count(lv_timer_t* timer, int count);

lv_obj_t* lv_msgbox_create(lv_obj_t* parent);
lv_obj_t* lv_msgbox_add_title(lv_obj_t* obj, const char* title);
lv_obj_t* lv_msgbox_add_text(lv_obj_t* obj, const char* text);
lv_obj_t* lv_msgbox_add_footer_button(lv_obj_t* obj, const char* text);
void lv_msgbox_close(lv_obj_t* mbox);
void lv_msgbox_close_async(lv_obj_t* mbox);

void lv_indev_set_mode(lv_indev_t* indev, int mode);
lv_timer_t* lv_indev_get_read_timer(lv_indev_t* indev);

void lv_nuttx_dsc_init(lv_nuttx_dsc_t* dsc);
void lv_nuttx_init(lv_nuttx_dsc_t* dsc, lv_nuttx_result_t* result);
void lv_nuttx_deinit(lv_nuttx_result_t* result);

#endif
