/****************************************************************************
 * apps/velacare/velacare_state.c — 4 级状态机实现
 *
 * 队伍 438 (baimi) · 2026 openvela AI 硬件开发者大赛
 *
 * 转换规则:
 *   网关心跳丢失                      → OFFLINE
 *   单个传感器失联                    → ATTENTION
 *   传感器真报警                      → WARNING
 *   WARNING 持续 5s 未解除           → EMERGENCY
 *   全部恢复正常                      → NORMAL (立即)
 *   WiFi down > 60s                  → OFFLINE
 ****************************************************************************/

#include <nuttx/config.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>

#include "velacare_state.h"

/****************************************************************************
 * 私有函数
 ****************************************************************************/

/* 状态名称表 */

static const char *g_state_names[] = VELACARE_STATE_NAMES;

/****************************************************************************
 * 公共函数
 ****************************************************************************/

/**
 * velacare_state_name - 获取状态名称字符串
 *
 * @param state  状态枚举值
 * @return       状态名称 (不可释放)
 */

const char *velacare_state_name(int state)
{
    if (state >= 0 && state <= VELACARE_STATE_OFFLINE)
        {
            return g_state_names[state];
        }

    return "UNKNOWN";
}

/**
 * velacare_state_init - 初始化状态机为 NORMAL
 *
 * @param ctx  状态机上下文 (调用者分配)
 */

void velacare_state_init(struct velacare_state_s *ctx)
{
    memset(ctx, 0, sizeof(*ctx));
    ctx->current      = VELACARE_STATE_NORMAL;
    ctx->previous     = VELACARE_STATE_NORMAL;
    ctx->smoke_online = true;
    ctx->water_online = true;
    ctx->door_online  = true;
    ctx->fall_online  = true;
    ctx->gateway_online = true;
    ctx->wifi_online  = true;
}

/**
 * velacare_state_evaluate - 每帧评估状态机
 *
 * @param ctx     状态机上下文
 * @param now_ms  当前时间 (毫秒)
 * @return        true 表示发生了状态变更
 */

bool velacare_state_evaluate(struct velacare_state_s *ctx, unsigned long now_ms)
{
    bool changed = false;
    bool any_alarm;
    bool all_sensors_online;

    /* 记录当前时间 */

    ctx->last_event_ms = now_ms;

    any_alarm = ctx->smoke_alarm || ctx->water_alarm ||
                ctx->door_alarm || ctx->fall_alarm;
    all_sensors_online = ctx->smoke_online && ctx->water_online &&
                         ctx->door_online && ctx->fall_online;

    /* ---- 1. 网关心跳丢失：数据不再可信，立即进入 OFFLINE ---- */

    if (!ctx->gateway_online)
        {
            ctx->wifi_down_ms = 0;
            if (ctx->current != VELACARE_STATE_OFFLINE)
                {
                    ctx->previous = ctx->current;
                    ctx->current = VELACARE_STATE_OFFLINE;
                    ctx->state_enter_ms = now_ms;
                    changed = true;
                }

            return changed;
        }

    /* ---- 2. 跟踪 WiFi 断开持续时间，但不能屏蔽本地报警 ---- */

    if (!ctx->wifi_online)
        {
            if (ctx->wifi_down_ms == 0)
                {
                    ctx->wifi_down_ms = now_ms;
                }
        }
    else
        {
            ctx->wifi_down_ms = 0;
        }

    /* ---- 3. 真报警优先于网络状态，保证本地告警始终有效 ---- */

    if (any_alarm)
        {
            if (ctx->current != VELACARE_STATE_WARNING &&
                ctx->current != VELACARE_STATE_EMERGENCY)
                {
                    ctx->previous = ctx->current;
                    ctx->current  = VELACARE_STATE_WARNING;
                    ctx->state_enter_ms = now_ms;
                    changed = true;
                }

            /* WARNING → EMERGENCY 升级 */

            if (ctx->current == VELACARE_STATE_WARNING &&
                (now_ms - ctx->state_enter_ms) >= EMERGENCY_TIMEOUT_MS)
                {
                    ctx->previous = ctx->current;
                    ctx->current  = VELACARE_STATE_EMERGENCY;
                    ctx->state_enter_ms = now_ms;
                    changed = true;
                }

            return changed;
        }

    /* ---- 4. 单个传感器失联 → ATTENTION，并解除旧紧急状态 ---- */

    if (!all_sensors_online)
        {
            if (ctx->current != VELACARE_STATE_ATTENTION)
                {
                    ctx->previous = ctx->current;
                    ctx->current  = VELACARE_STATE_ATTENTION;
                    ctx->state_enter_ms = now_ms;
                    changed = true;
                }

            return changed;
        }

    /* ---- 5. WiFi 持续离线 → OFFLINE ---- */

    if (!ctx->wifi_online &&
        (now_ms - ctx->wifi_down_ms) >= OFFLINE_TIMEOUT_MS)
        {
            if (ctx->current != VELACARE_STATE_OFFLINE)
                {
                    ctx->previous = ctx->current;
                    ctx->current = VELACARE_STATE_OFFLINE;
                    ctx->state_enter_ms = now_ms;
                    changed = true;
                }

            return changed;
        }

    /* ---- 6. 报警解除且四路在线：立即恢复 NORMAL ---- */

    if (ctx->current != VELACARE_STATE_NORMAL)
        {
            ctx->previous = ctx->current;
            ctx->current  = VELACARE_STATE_NORMAL;
            ctx->state_enter_ms = now_ms;
            changed = true;
        }

    return changed;
}

/**
 * velacare_state_smoke_online - 烟感传感器上下线事件
 *
 * @param ctx    状态机上下文
 * @param online true=上线, false=掉线
 */

void velacare_state_smoke_online(struct velacare_state_s *ctx, bool online)
{
    ctx->smoke_online = online;
}

/**
 * velacare_state_smoke_alarm - 烟感报警事件
 *
 * @param ctx   状态机上下文
 * @param alarm true=报警, false=解除
 */

void velacare_state_smoke_alarm(struct velacare_state_s *ctx, bool alarm)
{
    ctx->smoke_alarm = alarm;
}

/**
 * velacare_state_water_online - 水浸传感器上下线事件
 */

void velacare_state_water_online(struct velacare_state_s *ctx, bool online)
{
    ctx->water_online = online;
}

/**
 * velacare_state_water_alarm - 水浸报警事件
 */

void velacare_state_water_alarm(struct velacare_state_s *ctx, bool alarm)
{
    ctx->water_alarm = alarm;
}

/**
 * velacare_state_door_online - 门磁传感器上下线事件
 */

void velacare_state_door_online(struct velacare_state_s *ctx, bool online)
{
    ctx->door_online = online;
}

/**
 * velacare_state_door_alarm - 门磁异常开启事件
 */

void velacare_state_door_alarm(struct velacare_state_s *ctx, bool alarm)
{
    ctx->door_alarm = alarm;
}

/**
 * velacare_state_fall_online - 跌倒检测传感器上下线事件
 */

void velacare_state_fall_online(struct velacare_state_s *ctx, bool online)
{
    ctx->fall_online = online;
}

/**
 * velacare_state_fall_alarm - 跌倒检测触发事件
 */

void velacare_state_fall_alarm(struct velacare_state_s *ctx, bool alarm)
{
    ctx->fall_alarm = alarm;
}

/**
 * velacare_state_wifi - WiFi 状态事件
 *
 * @param ctx    状态机上下文
 * @param online true=在线, false=断开
 */

void velacare_state_wifi(struct velacare_state_s *ctx, bool online)
{
    ctx->wifi_online = online;
}
