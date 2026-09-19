/****************************************************************************
 * apps/velacare/velacare_main.c — VelaCare 主进程入口
 *
 * 队伍 438 (baimi) · 2026 openvela AI 硬件开发者大赛
 *
 * 职责:
 *   1. 初始化 4 级状态机
 *   2. 装载 4 个 Skill 到 ai_agent
 *   3. 注册 cron 定时任务
 *   4. 启动 LVGL 显示线程
 *   5. 进入 100 ms 非阻塞主循环，轮询传感器并评估状态机
 ****************************************************************************/

#include <nuttx/config.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdbool.h>
#include <stdint.h>
#include <syslog.h>
#include <time.h>

#include "velacare_state.h"
#include "velacare_sensor.h"
#include "velacare_buzzer.h"
#include "velacare_settings.h"
#include "velacare_skill.h"
#include "velacare_lvgl.h"
#include "velacare_cron.h"

/****************************************************************************
 * 私有宏
 ****************************************************************************/

#define VELACARE_TAG       "[VelaCare]"
#define VELACARE_LOOP_PERIOD_MS 100
#define VELACARE_BUZZER_TEST_MS 400
#define VELACARE_LOG(fmt, ...) \
    syslog(LOG_NOTICE, VELACARE_TAG " " fmt "\n", ##__VA_ARGS__)

/****************************************************************************
 * 私有函数
 ****************************************************************************/

/**
 * velacare_on_state_change - 状态变更回调
 *
 * 当状态机发生切换时被调用, 负责:
 *   - 打印状态变更日志
 *   - 触发对应的 Skill 执行
 *   - 更新 LVGL 屏幕显示
 *
 * @param new_state  新状态
 * @param old_state  旧状态
 * @param reason     变更原因描述
 */

static void velacare_on_state_change(int new_state, int old_state,
                                     const char *reason)
{
    VELACARE_LOG("State: %s -> %s (%s)",
                 velacare_state_name(old_state),
                 velacare_state_name(new_state),
                 reason ? reason : "N/A");

    /* 更新 LVGL 屏幕显示 */

    velacare_lvgl_update_state(new_state);

    /* 语音只在传感器边沿触发一次，避免紧急升级时重复播报。 */
}

/**
 * velacare_update_sensor_state - 更新网关和传感器状态
 *
 * @param ctx  状态机上下文
 * @param now_ms 当前单调时钟（毫秒）
 */

static void velacare_update_sensor_state(struct velacare_state_s *ctx,
                                         uint32_t now_ms)
{
    velacare_sensor_poll(now_ms);

    /* 读取传感器状态 */

    ctx->smoke_online = velacare_sensor_smoke_online();
    ctx->smoke_alarm  = velacare_sensor_smoke_alarm();
    ctx->water_online = velacare_sensor_water_online();
    ctx->water_alarm  = velacare_sensor_water_alarm();
    ctx->door_online  = velacare_sensor_door_online();
    ctx->door_alarm   = velacare_sensor_door_alarm();
    ctx->fall_online  = velacare_sensor_fall_online();
    ctx->fall_alarm   = velacare_sensor_fall_alarm();
    ctx->gateway_online = velacare_sensor_gateway_online();
    ctx->wifi_online  = velacare_sensor_wifi_online();
}

/**
 * velacare_sleep_ms - 毫秒级睡眠
 *
 * @param ms  睡眠毫秒数
 */

static void velacare_sleep_ms(unsigned int ms)
{
    struct timespec ts;
    ts.tv_sec  = ms / 1000;
    ts.tv_nsec = (ms % 1000) * 1000000L;
    nanosleep(&ts, NULL);
}

static bool velacare_any_alarm(const struct velacare_state_s *ctx)
{
    return ctx->smoke_alarm || ctx->water_alarm ||
           ctx->door_alarm || ctx->fall_alarm;
}

static void velacare_update_alarm_outputs(struct velacare_state_s *ctx,
                                           unsigned long now_ms,
                                           bool buzzer_test_active)
{
    static bool last_buzzer_valid;
    static bool last_buzzer_on;
    bool alarm = velacare_any_alarm(ctx);
    bool buzzer_on = false;
    int countdown = 0;

    if (alarm && ctx->current == VELACARE_STATE_WARNING)
        {
            unsigned long elapsed = now_ms - ctx->state_enter_ms;

            if (elapsed < EMERGENCY_TIMEOUT_MS)
                {
                    countdown = (int)((EMERGENCY_TIMEOUT_MS - elapsed + 999) /
                                      1000);
                }

            /* WARNING: one second on, one second off. */

            buzzer_on = ((elapsed / 1000) % 2) == 0;
        }
    else if (alarm && ctx->current == VELACARE_STATE_EMERGENCY)
        {
            /* EMERGENCY: continuous local alarm until the sensor recovers. */

            buzzer_on = true;
        }

    /* VCB1 mute/ack stops the local buzzer only; UI still shows real alarms. */
    if (velacare_sensor_buzzer_suppressed())
        {
            buzzer_on = false;
        }

    /* An explicit local test is independent from gateway alarm silence. */

    if (buzzer_test_active)
        {
            buzzer_on = true;
        }

    velacare_lvgl_update_alarm_countdown(countdown);
    if (velacare_buzzer_available() &&
        (!last_buzzer_valid || last_buzzer_on != buzzer_on))
        {
            if (velacare_buzzer_set(buzzer_on) == 0)
                {
                    last_buzzer_valid = true;
                    last_buzzer_on = buzzer_on;
                }
        }
}

/****************************************************************************
 * 公共函数
 ****************************************************************************/

/**
 * main - VelaCare 主入口
 *
 * @param argc  参数个数
 * @param argv  参数数组
 * @return      0=正常退出, <0=错误
 */

int main(int argc, char *argv[])
{
    struct velacare_state_s state;
    struct velacare_ui_requests_s ui_requests;
    unsigned long tick_ms = 0;
    unsigned long buzzer_test_until_ms = 0;
    bool buzzer_test_active = false;
    int saved_volume;

    VELACARE_LOG("=== VelaCare v0.8.0 family-care starting ===");
    VELACARE_LOG("Family Health Guardian - Team 438");
    VELACARE_LOG("Speech output: ESP32-S3/MAX98357A only");

    /* 1. 初始化状态机 */

    velacare_state_init(&state);
    state.on_state_change = velacare_on_state_change;

    VELACARE_LOG("State machine init: %s", velacare_state_name(state.current));

    /* 2. 初始化子系统 */

    velacare_skill_init();
    (void)velacare_settings_init();
    (void)velacare_sensor_init();
    (void)velacare_buzzer_init();
    saved_volume = velacare_settings_volume();
    (void)velacare_buzzer_set_volume(saved_volume);
    velacare_lvgl_init();
    velacare_lvgl_update_volume(saved_volume);
    velacare_lvgl_update_state(state.current);
    velacare_cron_init();

    VELACARE_LOG("Entering main loop (%d ms)", VELACARE_LOOP_PERIOD_MS);

    /* 5. 主循环: 非阻塞轮询 + 状态评估 */

    while (1)
        {
            /* 轮询传感器 */

            velacare_update_sensor_state(&state, (uint32_t)tick_ms);

            /* UI callbacks only enqueue requests; hardware I/O runs here. */

            if (buzzer_test_active && tick_ms >= buzzer_test_until_ms)
                {
                    buzzer_test_active = false;
                    velacare_lvgl_report_buzzer_test_result(0);
                }

            velacare_lvgl_take_requests(&ui_requests);
            if (ui_requests.confirm_safe)
                {
                    (void)velacare_sensor_send_ok();
                }

            if (ui_requests.send_sos)
                {
                    (void)velacare_sensor_send_sos();
                }

            if (ui_requests.cancel_sos)
                {
                    (void)velacare_sensor_cancel_sos();
                }

            if (ui_requests.request_contact)
                {
                    (void)velacare_sensor_send_care(
                        VELACARE_CARE_CONTACT_ME);
                }

            if (ui_requests.send_checkin)
                {
                    (void)velacare_sensor_send_care(
                        VELACARE_CARE_CHECKIN_OK);
                }

            if (ui_requests.cancel_care)
                {
                    (void)velacare_sensor_cancel_care();
                }

            if (ui_requests.apply_volume)
                {
                    int previous_volume = velacare_settings_volume();
                    int ret = velacare_buzzer_set_volume(ui_requests.volume);

                    if (ret == 0)
                        {
                            ret = velacare_settings_set_volume(
                                ui_requests.volume);
                            if (ret < 0)
                                {
                                    int rollback_ret =
                                        velacare_buzzer_set_volume(
                                            previous_volume);

                                    if (rollback_ret < 0)
                                        {
                                            VELACARE_LOG(
                                                "Volume rollback failed: %d",
                                                rollback_ret);
                                        }
                                }
                        }

                    velacare_lvgl_report_volume_result(
                        ret, ui_requests.volume);
                }

            if (ui_requests.test_buzzer)
                {
                    if (!velacare_buzzer_available())
                        {
                            velacare_lvgl_report_buzzer_test_result(-ENODEV);
                        }
                    else
                        {
                            buzzer_test_active = true;
                            buzzer_test_until_ms =
                                tick_ms + VELACARE_BUZZER_TEST_MS;
                        }
                }

            /* Push the latest sensor snapshot to the UI thread. */

            velacare_lvgl_update_sensors(
                state.smoke_online, state.smoke_alarm,
                state.water_online, state.water_alarm,
                state.door_online, state.door_alarm,
                state.fall_online, state.fall_alarm,
                state.wifi_online);
            velacare_lvgl_update_gateway_settings(
                velacare_sensor_gateway_online(),
                velacare_sensor_caregiver_configured(),
                velacare_sensor_caregiver_masked(),
                velacare_sensor_sos_status(),
                velacare_sensor_sos_family_reply());
            velacare_lvgl_update_care(
                velacare_sensor_family_acknowledged(),
                velacare_sensor_elder_confirmed());
            velacare_lvgl_update_family_flow(
                velacare_sensor_care_kind(),
                velacare_sensor_care_status(),
                velacare_sensor_family_reply());

            /* 评估状态机 */

            if (velacare_state_evaluate(&state, tick_ms))
                {
                    /* 状态已变更, 回调中处理后续动作 */

                    if (state.on_state_change)
                        {
                            state.on_state_change(
                                state.current,
                                state.previous,
                                "sensor_poll");
                        }
                }

            /* Drive the local audible alarm and visible escalation timer. */

            velacare_update_alarm_outputs(&state, tick_ms,
                                           buzzer_test_active);

            /* 100 ms 节拍保证试听与 UART 都不阻塞。 */

            tick_ms += VELACARE_LOOP_PERIOD_MS;
            velacare_sleep_ms(VELACARE_LOOP_PERIOD_MS);
        }

    return 0;
}
