/****************************************************************************
 * apps/velacare/velacare_lvgl.h - VelaCare LVGL display interface
 ****************************************************************************/

#ifndef __APPS_VELACARE_VELACARE_LVGL_H
#define __APPS_VELACARE_VELACARE_LVGL_H

#include <stdbool.h>

struct velacare_ui_requests_s
{
  bool confirm_safe;
  bool send_sos;
  bool cancel_sos;
  bool request_contact;
  bool send_checkin;
  bool cancel_care;
  bool apply_volume;
  int volume;
  bool test_buzzer;
};

void velacare_lvgl_init(void);
void velacare_lvgl_update_state(int state);
void velacare_lvgl_update_alarm_countdown(int seconds);
void velacare_lvgl_update_sensors(bool smoke_online, bool smoke_alarm,
                                   bool water_online, bool water_alarm,
                                   bool door_online, bool door_alarm,
                                   bool fall_online, bool fall_alarm,
                                   bool wifi_online);
void velacare_lvgl_update_gateway_settings(bool gateway_online,
                                            bool caregiver_configured,
                                            const char *caregiver_masked,
                                            int sos_status,
                                            int sos_family_reply);
void velacare_lvgl_update_care(bool family_seen, bool elder_confirmed);
void velacare_lvgl_update_family_flow(int care_kind, int care_status,
                                      int family_reply);
void velacare_lvgl_update_volume(int volume);
void velacare_lvgl_take_requests(struct velacare_ui_requests_s *requests);
void velacare_lvgl_report_volume_result(int result, int volume);
void velacare_lvgl_report_buzzer_test_result(int result);

#endif
