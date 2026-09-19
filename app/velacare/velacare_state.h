/****************************************************************************
 * apps/velacare/velacare_state.h — 4 级状态机头文件
 *
 * 队伍 438 (baimi) · 2026 openvela AI 硬件开发者大赛
 *
 * 状态转换规则:
 *   NORMAL    — 一切正常, 传感器在线, WiFi 在线
 *   ATTENTION — 单个传感器失联 (非紧急, 仅提醒)
 *   WARNING   — 传感器真报警 (烟感/水浸/门磁/跌倒)
 *   EMERGENCY — WARNING 持续 5s 未解除, 自动升级
 *   OFFLINE   — 网关心跳丢失, 或 WiFi 断开 > 60s
 ****************************************************************************/

#ifndef __APPS_VELACARE_VELACARE_STATE_H
#define __APPS_VELACARE_VELACARE_STATE_H

/****************************************************************************
 * 预处理指令
 ****************************************************************************/

/* 状态枚举 — 与 LVGL 屏幕颜色一一对应 */

#define VELACARE_STATE_NORMAL    0  /* 绿屏: 一切正常 */
#define VELACARE_STATE_ATTENTION 1  /* 黄屏: 传感器失联 */
#define VELACARE_STATE_WARNING   2  /* 橙屏: 真报警 */
#define VELACARE_STATE_EMERGENCY 3  /* 红屏: 紧急升级 */
#define VELACARE_STATE_OFFLINE   4  /* 灰屏: 离线模式 */

/* 状态机阈值 (毫秒) */

#define EMERGENCY_TIMEOUT_MS  5000    /* WARNING 5s 未解除 → EMERGENCY */
#define OFFLINE_TIMEOUT_MS    60000   /* WiFi 断开 60s → OFFLINE */

/* 状态名称字符串 (调试用) */

#define VELACARE_STATE_NAMES \
{                            \
    "NORMAL",                \
    "ATTENTION",             \
    "WARNING",               \
    "EMERGENCY",             \
    "OFFLINE"                \
}

/****************************************************************************
 * 公共类型定义
 ****************************************************************************/

/* 状态变更回调: 用户/系统收到状态切换时的处理函数 */

typedef void (*velacare_state_cb_t)(int new_state, int old_state, const char *reason);

/* 状态机上下文 */

struct velacare_state_s
{
    int                 current;          /* 当前状态 */
    int                 previous;         /* 上一状态 */
    unsigned long       state_enter_ms;   /* 进入当前状态的时间戳 (ms) */
    unsigned long       last_event_ms;    /* 最近一次传感器事件时间戳 */
    unsigned long       wifi_down_ms;     /* WiFi 断开的时间戳 (ms) */

    /* 传感器在线标志 */

    bool                smoke_online;     /* 烟感在线 */
    bool                water_online;     /* 水浸在线 */
    bool                door_online;      /* 门磁在线 */
    bool                fall_online;      /* 跌倒检测在线 */

    /* 传感器报警标志 */

    bool                smoke_alarm;      /* 烟感真报警 */
    bool                water_alarm;      /* 水浸报警 */
    bool                door_alarm;       /* 门磁异常开启 */
    bool                fall_alarm;       /* 跌倒检测触发 */

    /* 网络状态 */

    bool                gateway_online; /* 串口网关心跳在线 */
    bool                wifi_online;      /* WiFi 在线 */

    /* 回调 */

    velacare_state_cb_t on_state_change;  /* 状态变更回调 */
};

/****************************************************************************
 * 公共函数声明
 ****************************************************************************/

/* 初始化状态机为 NORMAL */

void velacare_state_init(struct velacare_state_s *ctx);

/* 每帧评估 (建议 1Hz 调用), 返回是否有状态变更 */

bool velacare_state_evaluate(struct velacare_state_s *ctx, unsigned long now_ms);

/* 外部事件触发 */

void velacare_state_smoke_online(struct velacare_state_s *ctx, bool online);
void velacare_state_smoke_alarm(struct velacare_state_s *ctx, bool alarm);
void velacare_state_water_online(struct velacare_state_s *ctx, bool online);
void velacare_state_water_alarm(struct velacare_state_s *ctx, bool alarm);
void velacare_state_door_online(struct velacare_state_s *ctx, bool online);
void velacare_state_door_alarm(struct velacare_state_s *ctx, bool alarm);
void velacare_state_fall_online(struct velacare_state_s *ctx, bool online);
void velacare_state_fall_alarm(struct velacare_state_s *ctx, bool alarm);
void velacare_state_wifi(struct velacare_state_s *ctx, bool online);

/* 获取状态名称 */

const char *velacare_state_name(int state);

#endif /* __APPS_VELACARE_VELACARE_STATE_H */
