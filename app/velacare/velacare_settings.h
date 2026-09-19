/****************************************************************************
 * apps/velacare/velacare_settings.h - persistent local settings
 ****************************************************************************/

#ifndef __APPS_VELACARE_VELACARE_SETTINGS_H
#define __APPS_VELACARE_VELACARE_SETTINGS_H

#include <stdbool.h>
#include <stdint.h>

#define VELACARE_REQUEST_FAMILY_HISTORY 16

struct velacare_request_history_entry_s
{
  uint32_t request_id;
  uint8_t event_type;
  uint8_t reserved[3];
};

/* Durable SOS/CARE state is stored separately from settings.bin so existing
 * v1/v2 volume, brightness and request-ID high-water files remain compatible.
 */

struct velacare_request_state_s
{
  uint32_t sos_request_id;
  uint32_t care_request_id;
  int32_t sos_status;
  int32_t care_kind;
  int32_t care_status;
  int32_t sos_family_reply;
  int32_t care_family_reply;
  uint8_t sos_active;
  uint8_t sos_acked;
  uint8_t sos_cancel_pending;
  uint8_t care_active;
  uint8_t care_acked;
  uint8_t care_cancel_pending;
  uint8_t family_history_next;
  uint8_t reserved;
  struct velacare_request_history_entry_s
    family_history[VELACARE_REQUEST_FAMILY_HISTORY];
};

int velacare_settings_init(void);
bool velacare_settings_ready(void);
int velacare_settings_volume(void);
int velacare_settings_brightness(void);
int velacare_settings_set_volume(int value);
int velacare_settings_set_brightness(int value);
int velacare_settings_reserve_request_ids(uint32_t seed, uint32_t count,
                                           uint32_t *first_id,
                                           uint32_t *past_last_id);
int velacare_settings_load_request_state(
  struct velacare_request_state_s *state);
int velacare_settings_save_request_state(
  const struct velacare_request_state_s *state);

#endif
