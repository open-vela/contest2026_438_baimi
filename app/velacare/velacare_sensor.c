/****************************************************************************
 * apps/velacare/velacare_sensor.c — UART1 传感器网关接入层
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <time.h>
#include <unistd.h>

#include "velacare_sensor.h"
#include "velacare_settings.h"

#define VELACARE_SENSOR_TAG            "[VelaCare/UART1]"
#define VELACARE_SENSOR_LINE_MAX       96
#define VELACARE_SENSOR_REOPEN_MS      5000
#define VELACARE_OK_RETRY_MS           1000
#define VELACARE_OK_RETRY_MAX          5
#define VELACARE_REQUEST_RETRY_MS      2000
#define VELACARE_UART_READ_BUDGET      256
#define VELACARE_UART_READ_CALLS_MAX   8
#define VELACARE_REQUEST_ID_BLOCK_SIZE 256u
#define VELACARE_REQUEST_ID_FALLBACK   0x43800000u

struct sensor_snapshot_s
{
  bool online[4];
  bool alarm[4];
  bool wifi_online;
  bool gateway_online;
  bool have_frame;
  uint32_t sequence;
  uint32_t last_frame_ms;
  uint32_t caregiver_revision;
  bool caregiver_configured;
  char caregiver_masked[9];
  uint32_t sos_request_id;
  int sos_status;
  uint32_t care_request_id;
  int care_kind;
  int care_status;
  int sos_family_reply;
  int care_family_reply;
};

static struct sensor_snapshot_s g_snapshot;
static char g_line[VELACARE_SENSOR_LINE_MAX];
static size_t g_line_len;
static int g_uart_fd = -1;
static uint32_t g_next_open_ms;
static uint32_t g_next_request_id;
static uint32_t g_request_id_limit;
static bool g_request_ids_persistent;
static struct velacare_request_history_entry_s
  g_family_ack_history[VELACARE_REQUEST_FAMILY_HISTORY];
static size_t g_family_ack_history_next;
static bool g_request_state_ready;
static bool g_sos_active;
static bool g_sos_acked;
static bool g_sos_cancel_pending;
static uint32_t g_sos_last_send_ms;
static uint32_t g_sos_cancel_last_send_ms;
static bool g_care_active;
static bool g_care_acked;
static bool g_care_cancel_pending;
static uint32_t g_care_last_send_ms;
static uint32_t g_care_cancel_last_send_ms;
static bool g_buzzer_suppressed;
static bool g_family_acknowledged;
static bool g_buzzer_control_valid;
static unsigned int g_buzzer_severity;
static bool g_elder_confirmed;
static uint32_t g_now_ms;
static uint32_t g_pending_ok_id;
static uint32_t g_ok_last_send_ms;
static int g_ok_attempts;
static bool g_ok_acked;

static int velacare_sensor_write_frame_internal(const char *payload,
                                                 bool require_online);
static int velacare_sensor_write_family_ack(uint32_t request_id,
                                             unsigned int event_type);

static void velacare_sensor_capture_request_state(
  struct velacare_request_state_s *state)
{
  memset(state, 0, sizeof(*state));
  state->sos_request_id = g_snapshot.sos_request_id;
  state->care_request_id = g_snapshot.care_request_id;
  state->sos_status = g_snapshot.sos_status;
  state->care_kind = g_snapshot.care_kind;
  state->care_status = g_snapshot.care_status;
  state->sos_family_reply = g_snapshot.sos_family_reply;
  state->care_family_reply = g_snapshot.care_family_reply;
  state->sos_active = g_sos_active ? 1 : 0;
  state->sos_acked = g_sos_acked ? 1 : 0;
  state->sos_cancel_pending = g_sos_cancel_pending ? 1 : 0;
  state->care_active = g_care_active ? 1 : 0;
  state->care_acked = g_care_acked ? 1 : 0;
  state->care_cancel_pending = g_care_cancel_pending ? 1 : 0;
  state->family_history_next = (uint8_t)g_family_ack_history_next;
  memcpy(state->family_history, g_family_ack_history,
         sizeof(state->family_history));
}

static void velacare_sensor_apply_request_state(
  const struct velacare_request_state_s *state)
{
  g_snapshot.sos_request_id = state->sos_request_id;
  g_snapshot.care_request_id = state->care_request_id;
  g_snapshot.sos_status = state->sos_status;
  g_snapshot.care_kind = state->care_kind;
  g_snapshot.care_status = state->care_status;
  g_snapshot.sos_family_reply = state->sos_family_reply;
  g_snapshot.care_family_reply = state->care_family_reply;
  g_sos_active = state->sos_active != 0;
  g_sos_acked = state->sos_acked != 0;
  g_sos_cancel_pending = state->sos_cancel_pending != 0;
  g_care_active = state->care_active != 0;
  g_care_acked = state->care_acked != 0;
  g_care_cancel_pending = state->care_cancel_pending != 0;
  g_family_ack_history_next = state->family_history_next;
  memcpy(g_family_ack_history, state->family_history,
         sizeof(g_family_ack_history));
}

static void velacare_sensor_restore_request_state(
  const struct velacare_request_state_s *state)
{
  velacare_sensor_apply_request_state(state);

  if (g_sos_active)
    {
      g_snapshot.sos_status = g_sos_cancel_pending ?
        VELACARE_SOS_CANCELLING :
        (g_sos_acked ? g_snapshot.sos_status : VELACARE_SOS_RETRYING);
    }

  if (g_care_active)
    {
      g_snapshot.care_status = g_care_cancel_pending ?
        VELACARE_CARE_CANCELLING :
        (g_care_acked ? g_snapshot.care_status : VELACARE_CARE_RETRYING);
    }
}

static int velacare_sensor_commit_request_state(
  const struct velacare_request_state_s *previous, const char *reason)
{
  struct velacare_request_state_s current;
  int ret;

  velacare_sensor_capture_request_state(&current);
  if (memcmp(previous, &current, sizeof(current)) == 0)
    {
      return 0;
    }

  if (!g_request_state_ready)
    {
      velacare_sensor_apply_request_state(previous);
      syslog(LOG_ERR, VELACARE_SENSOR_TAG
             " request state unavailable; rejected %s\n", reason);
      return -EIO;
    }

  ret = velacare_settings_save_request_state(&current);
  if (ret < 0)
    {
      velacare_sensor_apply_request_state(previous);
      syslog(LOG_ERR, VELACARE_SENSOR_TAG
             " request state save failed for %s: %d\n", reason, ret);
    }

  return ret;
}

static bool velacare_sensor_family_ack_was_seen(uint32_t request_id,
                                                 unsigned int event_type)
{
  size_t i;

  for (i = 0; i < VELACARE_REQUEST_FAMILY_HISTORY; i++)
    {
      if (g_family_ack_history[i].request_id == request_id &&
          g_family_ack_history[i].event_type == event_type)
        {
          return true;
        }
    }

  return false;
}

static void velacare_sensor_remember_family_ack(uint32_t request_id,
                                                 unsigned int event_type)
{
  if (velacare_sensor_family_ack_was_seen(request_id, event_type))
    {
      return;
    }

  g_family_ack_history[g_family_ack_history_next].request_id = request_id;
  g_family_ack_history[g_family_ack_history_next].event_type =
    (uint8_t)event_type;
  g_family_ack_history_next =
    (g_family_ack_history_next + 1) % VELACARE_REQUEST_FAMILY_HISTORY;
}

static uint32_t velacare_sensor_mix_seed(uint32_t value)
{
  value ^= value >> 16;
  value *= 0x7feb352dU;
  value ^= value >> 15;
  value *= 0x846ca68bU;
  value ^= value >> 16;
  return value;
}

static uint32_t velacare_sensor_request_seed(void)
{
  struct timespec realtime;
  struct timespec monotonic;
  uint32_t seed = (uint32_t)getpid() ^ (uint32_t)clock();

  memset(&realtime, 0, sizeof(realtime));
  memset(&monotonic, 0, sizeof(monotonic));
  if (clock_gettime(CLOCK_REALTIME, &realtime) == 0)
    {
      seed ^= (uint32_t)realtime.tv_sec;
      seed ^= (uint32_t)((uint64_t)realtime.tv_sec >> 32);
      seed ^= (uint32_t)realtime.tv_nsec;
    }

  if (clock_gettime(CLOCK_MONOTONIC, &monotonic) == 0)
    {
      seed ^= (uint32_t)monotonic.tv_sec * 0x9e3779b9U;
      seed ^= (uint32_t)monotonic.tv_nsec;
    }

  seed = velacare_sensor_mix_seed(seed);
  seed &= 0x7fffffffu;
  seed |= 0x10000000u;
  return seed == 0 ? VELACARE_REQUEST_ID_FALLBACK : seed;
}

static int velacare_sensor_reserve_request_ids(uint32_t seed)
{
  uint32_t first_id;
  uint32_t past_last_id;
  int ret;

  ret = velacare_settings_reserve_request_ids(
    seed, VELACARE_REQUEST_ID_BLOCK_SIZE, &first_id, &past_last_id);
  if (ret < 0)
    {
      return ret;
    }

  g_next_request_id = first_id;
  g_request_id_limit = past_last_id;
  g_request_ids_persistent = true;
  return 0;
}

static int velacare_sensor_next_request_id(uint32_t *request_id)
{
  int ret;

  if (request_id == NULL)
    {
      return -EINVAL;
    }

  if (!g_request_ids_persistent ||
      g_next_request_id >= g_request_id_limit)
    {
      uint32_t seed = velacare_sensor_mix_seed(
        g_next_request_id ^ g_now_ms ^ (uint32_t)clock());

      ret = velacare_sensor_reserve_request_ids(seed);
      if (ret < 0)
        {
          g_request_ids_persistent = false;
          syslog(LOG_ERR, VELACARE_SENSOR_TAG
                 " request id persistence unavailable (%d); new request rejected\n",
                 ret);
          return ret;
        }
    }

  if (g_next_request_id == 0 ||
      g_next_request_id >= g_request_id_limit)
    {
      g_request_ids_persistent = false;
      return -EOVERFLOW;
    }

  *request_id = g_next_request_id++;
  return 0;
}

static uint16_t velacare_crc16_ccitt(const uint8_t *data, size_t len)
{
  uint16_t crc = 0xffff;
  size_t i;
  int bit;

  for (i = 0; i < len; i++)
    {
      crc ^= (uint16_t)data[i] << 8;
      for (bit = 0; bit < 8; bit++)
        {
          crc = (crc & 0x8000) != 0 ? (uint16_t)((crc << 1) ^ 0x1021)
                                    : (uint16_t)(crc << 1);
        }
    }

  return crc;
}

static void velacare_sensor_set_offline(void)
{
  memset(g_snapshot.online, 0, sizeof(g_snapshot.online));
  memset(g_snapshot.alarm, 0, sizeof(g_snapshot.alarm));
  g_snapshot.wifi_online = false;
  g_snapshot.gateway_online = false;
  g_buzzer_suppressed = false;
  g_family_acknowledged = false;
  g_buzzer_control_valid = false;
  g_elder_confirmed = false;
  if (g_sos_active)
    {
      g_snapshot.sos_status = g_sos_cancel_pending ?
        VELACARE_SOS_CANCELLING : VELACARE_SOS_RETRYING;
      g_sos_acked = false;
    }

  if (g_care_active)
    {
      g_snapshot.care_status = g_care_cancel_pending ?
        VELACARE_CARE_CANCELLING : VELACARE_CARE_RETRYING;
      if (!g_care_cancel_pending)
        {
          g_care_acked = false;
        }
    }
}

static bool velacare_sensor_parse_line(char *line, uint32_t now_ms)
{
  struct velacare_request_state_s previous_request_state;
  unsigned long sequence;
  unsigned int state[4];
  unsigned int wifi;
  unsigned long received_crc;
  uint16_t calculated_crc;
  char *crc_text;
  char *end;
  char trailing;
  int ack_ret;
  int matched;
  int persist_ret;
  int i;
  unsigned int configured;
  unsigned int status;
  unsigned long revision;
  unsigned long request_id;
  char last4[5];
  unsigned int muted;
  unsigned int acknowledged;
  unsigned int severity;
  unsigned int reply_code;
  unsigned int event_type;

  crc_text = strchr(line, '*');
  if (crc_text == NULL || strlen(crc_text + 1) != 4)
    {
      return false;
    }

  *crc_text = '\0';
  received_crc = strtoul(crc_text + 1, &end, 16);
  if (*end != '\0' || received_crc > 0xffff)
    {
      return false;
    }

  calculated_crc = velacare_crc16_ccitt((const uint8_t *)line,
                                         strlen(line));
  if (calculated_crc != (uint16_t)received_crc)
    {
      syslog(LOG_WARNING, VELACARE_SENSOR_TAG " CRC error\n");
      return false;
    }

  if (sscanf(line, "VCB1,%u,%u,%u%c", &muted, &acknowledged, &severity, &trailing) == 3 &&
      muted <= 1 && acknowledged <= 1 && severity <= 3)
    {
      bool family_acknowledged = acknowledged != 0;
      bool buzzer_suppressed = muted != 0 || family_acknowledged;
      bool changed = !g_buzzer_control_valid ||
                     g_family_acknowledged != family_acknowledged ||
                     g_buzzer_suppressed != buzzer_suppressed ||
                     g_buzzer_severity != severity;

      g_family_acknowledged = family_acknowledged;
      g_buzzer_suppressed = buzzer_suppressed;
      g_buzzer_severity = severity;
      g_buzzer_control_valid = true;
      if (changed)
        {
          syslog(LOG_NOTICE, VELACARE_SENSOR_TAG
                 " buzzer %s family_ack=%u severity=%u\n",
                 g_buzzer_suppressed ? "suppressed" : "enabled",
                 acknowledged, severity);
        }
      return true;
    }

  if (strncmp(line, "CFG1,", 5) == 0)
    {
      memset(last4, 0, sizeof(last4));
      matched = sscanf(line, "CFG1,%lu,%u,%4[0-9]%c", &revision,
                       &configured, last4, &trailing);
      if (matched != 3 || configured > 1 || strlen(last4) != 4)
        {
          return false;
        }

      g_snapshot.caregiver_revision = (uint32_t)revision;
      g_snapshot.caregiver_configured = configured != 0;
      if (configured != 0)
        {
          snprintf(g_snapshot.caregiver_masked,
                   sizeof(g_snapshot.caregiver_masked), "****%s", last4);
        }
      else
        {
          g_snapshot.caregiver_masked[0] = '\0';
        }

      return true;
    }

  if (strncmp(line, "SACK1,", 6) == 0)
    {
      matched = sscanf(line, "SACK1,%lu,%u%c", &request_id, &status,
                       &trailing);
      if (matched != 2 || status < 1 || status > 2 ||
          request_id != g_snapshot.sos_request_id || !g_sos_active)
        {
          return false;
        }

      velacare_sensor_capture_request_state(&previous_request_state);
      g_sos_acked = true;
      if (!g_sos_cancel_pending)
        {
          g_snapshot.sos_status = status == 1 ?
            VELACARE_SOS_NO_CAREGIVER : VELACARE_SOS_WAIT_FAMILY;
        }
      persist_ret = velacare_sensor_commit_request_state(
        &previous_request_state, "SACK1");
      if (persist_ret < 0)
        {
          return true;
        }

      syslog(LOG_NOTICE, VELACARE_SENSOR_TAG
             " SACK1 id=%lu status=%u\n", request_id, status);
      return true;
    }

  if (strncmp(line, "SXACK1,", 7) == 0)
    {
      matched = sscanf(line, "SXACK1,%lu,%u%c", &request_id, &status,
                       &trailing);
      if (matched != 2 || status > 2 ||
          request_id != g_snapshot.sos_request_id ||
          !g_sos_active || !g_sos_cancel_pending)
        {
          return false;
        }

      velacare_sensor_capture_request_state(&previous_request_state);
      g_sos_active = false;
      g_sos_acked = true;
      g_sos_cancel_pending = false;
      g_snapshot.sos_status = status == 2 ?
        VELACARE_SOS_FAMILY_SEEN : VELACARE_SOS_CANCELLED;
      persist_ret = velacare_sensor_commit_request_state(
        &previous_request_state, "SXACK1");
      if (persist_ret < 0)
        {
          return true;
        }

      syslog(LOG_NOTICE, VELACARE_SENSOR_TAG
              " SXACK1 id=%lu status=%u\n", request_id, status);
      return true;
    }

  if (strncmp(line, "CACK1,", 6) == 0)
    {
      matched = sscanf(line, "CACK1,%lu,%u,%u%c", &request_id,
                       &event_type, &status, &trailing);
      if (matched != 3 || event_type < VELACARE_CARE_CONTACT_ME ||
          event_type > VELACARE_CARE_CHECKIN_OK ||
          status < 1 || status > 2 ||
          request_id != g_snapshot.care_request_id ||
          event_type != (unsigned int)g_snapshot.care_kind ||
          !g_care_active)
        {
          return false;
        }

      velacare_sensor_capture_request_state(&previous_request_state);
      g_care_acked = true;
      if (!g_care_cancel_pending)
        {
          g_snapshot.care_status = status == 1 ?
            VELACARE_CARE_NO_CAREGIVER : VELACARE_CARE_WAIT_FAMILY;
        }
      persist_ret = velacare_sensor_commit_request_state(
        &previous_request_state, "CACK1");
      if (persist_ret < 0)
        {
          return true;
        }

      syslog(LOG_NOTICE, VELACARE_SENSOR_TAG
              " CACK1 id=%lu type=%u status=%u\n",
              request_id, event_type, status);
      return true;
    }

  if (strncmp(line, "CXACK1,", 7) == 0)
    {
      matched = sscanf(line, "CXACK1,%lu,%u,%u%c", &request_id,
                       &event_type, &status, &trailing);
      if (matched != 3 || event_type < VELACARE_CARE_CONTACT_ME ||
          event_type > VELACARE_CARE_CHECKIN_OK || status > 2 ||
          request_id != g_snapshot.care_request_id ||
          event_type != (unsigned int)g_snapshot.care_kind ||
          !g_care_active || !g_care_cancel_pending)
        {
          return false;
        }

      velacare_sensor_capture_request_state(&previous_request_state);
      g_care_active = false;
      g_care_acked = true;
      g_care_cancel_pending = false;
      g_snapshot.care_status = status == 2 ?
        VELACARE_CARE_FAMILY_SEEN : VELACARE_CARE_CANCELLED;
      persist_ret = velacare_sensor_commit_request_state(
        &previous_request_state, "CXACK1");
      if (persist_ret < 0)
        {
          return true;
        }

      syslog(LOG_NOTICE, VELACARE_SENSOR_TAG
              " CXACK1 id=%lu type=%u status=%u\n",
              request_id, event_type, status);
      return true;
    }

  if (strncmp(line, "FAM1,", 5) == 0)
    {
      matched = sscanf(line, "FAM1,%lu,%u,%u%c", &request_id,
                       &event_type, &reply_code, &trailing);
      if (matched != 3 || event_type > VELACARE_CARE_CHECKIN_OK ||
          reply_code < 1 || reply_code > 4)
        {
          return false;
        }

      if (event_type == 0)
        {
          if (request_id != g_snapshot.sos_request_id ||
              g_snapshot.sos_request_id == 0 ||
              g_snapshot.sos_status == VELACARE_SOS_IDLE ||
              g_snapshot.sos_status == VELACARE_SOS_CANCELLED)
            {
              if (velacare_sensor_family_ack_was_seen(
                    (uint32_t)request_id, event_type))
                {
                  ack_ret = velacare_sensor_write_family_ack(
                    (uint32_t)request_id, event_type);
                  if (ack_ret < 0)
                    {
                      syslog(LOG_WARNING, VELACARE_SENSOR_TAG
                             " duplicate FAM1 id=%lu type=%u "
                             "FACK1 retry failed: %d\n",
                             request_id, event_type, ack_ret);
                    }
                  else
                    {
                      syslog(LOG_NOTICE, VELACARE_SENSOR_TAG
                             " duplicate FAM1 id=%lu type=%u "
                             "re-acknowledged without changing current SOS\n",
                             request_id, event_type);
                    }

                  return true;
                }

              return false;
            }

          velacare_sensor_capture_request_state(&previous_request_state);
          velacare_sensor_remember_family_ack((uint32_t)request_id,
                                               event_type);
          g_sos_active = false;
          g_sos_acked = true;
          g_sos_cancel_pending = false;
          g_snapshot.sos_status = VELACARE_SOS_FAMILY_SEEN;
          g_snapshot.sos_family_reply = (int)reply_code;
          persist_ret = velacare_sensor_commit_request_state(
            &previous_request_state, "FAM1 SOS");
          if (persist_ret < 0)
            {
              return true;
            }

          ack_ret = velacare_sensor_write_family_ack((uint32_t)request_id,
                                                      event_type);
          if (ack_ret < 0)
            {
              syslog(LOG_WARNING, VELACARE_SENSOR_TAG
                     " FAM1 id=%lu type=%u persisted but "
                     "FACK1 send failed: %d\n",
                     request_id, event_type, ack_ret);
            }
        }
      else
        {
          if (request_id != g_snapshot.care_request_id ||
              g_snapshot.care_request_id == 0 ||
              event_type != (unsigned int)g_snapshot.care_kind ||
              g_snapshot.care_status == VELACARE_CARE_IDLE ||
              g_snapshot.care_status == VELACARE_CARE_CANCELLED)
            {
              if (velacare_sensor_family_ack_was_seen(
                    (uint32_t)request_id, event_type))
                {
                  ack_ret = velacare_sensor_write_family_ack(
                    (uint32_t)request_id, event_type);
                  if (ack_ret < 0)
                    {
                      syslog(LOG_WARNING, VELACARE_SENSOR_TAG
                             " duplicate FAM1 id=%lu type=%u "
                             "FACK1 retry failed: %d\n",
                             request_id, event_type, ack_ret);
                    }
                  else
                    {
                      syslog(LOG_NOTICE, VELACARE_SENSOR_TAG
                             " duplicate FAM1 id=%lu type=%u "
                             "re-acknowledged without changing current care request\n",
                             request_id, event_type);
                    }

                  return true;
                }

              return false;
            }

          velacare_sensor_capture_request_state(&previous_request_state);
          velacare_sensor_remember_family_ack((uint32_t)request_id,
                                               event_type);
          g_care_active = false;
          g_care_acked = true;
          g_care_cancel_pending = false;
          g_snapshot.care_status = VELACARE_CARE_FAMILY_SEEN;
          g_snapshot.care_family_reply = (int)reply_code;
          persist_ret = velacare_sensor_commit_request_state(
            &previous_request_state, "FAM1 CARE");
          if (persist_ret < 0)
            {
              return true;
            }

          ack_ret = velacare_sensor_write_family_ack((uint32_t)request_id,
                                                      event_type);
          if (ack_ret < 0)
            {
              syslog(LOG_WARNING, VELACARE_SENSOR_TAG
                     " FAM1 id=%lu type=%u persisted but "
                     "FACK1 send failed: %d\n",
                     request_id, event_type, ack_ret);
            }
        }

      syslog(LOG_NOTICE, VELACARE_SENSOR_TAG
              " FAM1 id=%lu type=%u reply=%u\n",
              request_id, event_type, reply_code);
      return true;
    }

  if (strncmp(line, "OACK1,", 6) == 0)
    {
      matched = sscanf(line, "OACK1,%lu%c", &request_id, &trailing);
      if (matched != 1 || request_id == 0)
        {
          return false;
        }

      if (request_id == g_pending_ok_id)
        {
          g_ok_acked = true;
          g_pending_ok_id = 0;
          syslog(LOG_NOTICE, VELACARE_SENSOR_TAG
                 " OACK1 id=%lu gateway saw confirm-safe\n",
                 request_id);
        }

      return true;
    }

  matched = sscanf(line, "VC1,%lu,%u,%u,%u,%u,%u%c",
                   &sequence, &state[0], &state[1], &state[2], &state[3],
                   &wifi, &trailing);
  if (matched != 6 || wifi > 1)
    {
      return false;
    }

  for (i = 0; i < 4; i++)
    {
      if (state[i] > 2)
        {
          return false;
        }
    }

  for (i = 0; i < 4; i++)
    {
      g_snapshot.online[i] = state[i] != 0;
      g_snapshot.alarm[i] = state[i] == 2;
    }

  if (state[3] != 2)
    {
      g_elder_confirmed = false;
      g_pending_ok_id = 0;
      g_ok_acked = false;
      g_ok_attempts = 0;
    }

  g_snapshot.wifi_online = wifi != 0;
  g_snapshot.gateway_online = true;
  g_snapshot.have_frame = true;
  g_snapshot.sequence = (uint32_t)sequence;
  g_snapshot.last_frame_ms = now_ms;

  if (g_uart_fd >= 0)
    {
      char ack[32];
      int ack_len = snprintf(ack, sizeof(ack), "ACK,%lu\n", sequence);
      if (ack_len > 0)
        {
          (void)write(g_uart_fd, ack, (size_t)ack_len);
        }
    }

  return true;
}

static void velacare_sensor_consume(const uint8_t *data, size_t len,
                                    uint32_t now_ms)
{
  size_t i;

  for (i = 0; i < len; i++)
    {
      uint8_t ch = data[i];

      if (ch == '\r')
        {
          continue;
        }

      if (ch == '\n')
        {
          if (g_line_len > 0)
            {
              g_line[g_line_len] = '\0';
              (void)velacare_sensor_parse_line(g_line, now_ms);
              g_line_len = 0;
            }

          continue;
        }

      if (g_line_len < sizeof(g_line) - 1)
        {
          g_line[g_line_len++] = (char)ch;
        }
      else
        {
          g_line_len = 0;
        }
    }
}

static void velacare_sensor_open(uint32_t now_ms)
{
  if (g_uart_fd >= 0 || now_ms < g_next_open_ms)
    {
      return;
    }

  g_uart_fd = open(VELACARE_SENSOR_UART_PATH, O_RDWR | O_NONBLOCK);
  if (g_uart_fd < 0)
    {
      syslog(LOG_WARNING, VELACARE_SENSOR_TAG " open %s failed: %d\n",
             VELACARE_SENSOR_UART_PATH, errno);
      g_next_open_ms = now_ms + VELACARE_SENSOR_REOPEN_MS;
      return;
    }

  syslog(LOG_NOTICE, VELACARE_SENSOR_TAG " listening on %s, 115200 8N1\n",
         VELACARE_SENSOR_UART_PATH);
  (void)write(g_uart_fd, "HELLO,VC1\n", 10);
}

int velacare_sensor_init(void)
{
  struct velacare_request_state_s persisted;
  uint32_t request_seed;
  int state_ret;
  int ret;

  memset(&g_snapshot, 0, sizeof(g_snapshot));
  memset(g_family_ack_history, 0, sizeof(g_family_ack_history));
  g_family_ack_history_next = 0;
  g_request_state_ready = false;
  g_line_len = 0;
  g_next_open_ms = 0;
  g_next_request_id = 0;
  g_request_id_limit = 0;
  g_request_ids_persistent = false;
  g_sos_active = false;
  g_sos_acked = false;
  g_sos_cancel_pending = false;
  g_sos_last_send_ms = 0;
  g_sos_cancel_last_send_ms = 0;
  g_care_active = false;
  g_care_acked = false;
  g_care_cancel_pending = false;
  g_care_last_send_ms = 0;
  g_care_cancel_last_send_ms = 0;
  g_buzzer_suppressed = false;
  g_family_acknowledged = false;
  g_buzzer_control_valid = false;
  g_elder_confirmed = false;
  g_pending_ok_id = 0;
  g_ok_last_send_ms = 0;
  g_ok_attempts = 0;
  g_ok_acked = false;

  request_seed = velacare_sensor_request_seed();
  ret = velacare_sensor_reserve_request_ids(request_seed);
  if (ret < 0)
    {
      syslog(LOG_ERR, VELACARE_SENSOR_TAG
             " request id reservation failed: %d; existing requests will recover but new requests are disabled until storage recovers\n",
             ret);
    }

  state_ret = velacare_settings_load_request_state(&persisted);
  if (state_ret == 0)
    {
      g_request_state_ready = true;
      velacare_sensor_restore_request_state(&persisted);
      if (persisted.sos_request_id != 0 || persisted.care_request_id != 0 ||
          persisted.family_history_next != 0)
        {
          syslog(LOG_NOTICE, VELACARE_SENSOR_TAG
                 " restored SOS id=%lu active=%u CARE id=%lu active=%u history_next=%u\n",
                 (unsigned long)persisted.sos_request_id,
                 persisted.sos_active,
                 (unsigned long)persisted.care_request_id,
                 persisted.care_active,
                 persisted.family_history_next);
        }
    }
  else
    {
      syslog(LOG_ERR, VELACARE_SENSOR_TAG
             " request state unavailable: %d; SOS/CARE sending disabled\n",
             state_ret);
    }

  syslog(LOG_NOTICE, VELACARE_SENSOR_TAG
         " request ids start=%lu limit=%lu persistent=%u\n",
         (unsigned long)g_next_request_id,
         (unsigned long)g_request_id_limit,
         g_request_ids_persistent ? 1 : 0);
  velacare_sensor_open(0);
  return g_uart_fd >= 0 ? 0 : -1;
}

static int velacare_sensor_write_frame_internal(const char *payload,
                                                 bool require_online)
{
  char frame[80];
  int frame_len;
  ssize_t written;
  uint16_t crc;

  if (g_uart_fd < 0 || (require_online && !g_snapshot.gateway_online))
    {
      return -ENOTCONN;
    }

  crc = velacare_crc16_ccitt((const uint8_t *)payload, strlen(payload));
  frame_len = snprintf(frame, sizeof(frame), "%s*%04X\n", payload, crc);
  if (frame_len <= 0 || frame_len >= (int)sizeof(frame))
    {
      return -EOVERFLOW;
    }

  written = write(g_uart_fd, frame, (size_t)frame_len);
  if (written != frame_len)
    {
      return written < 0 ? -errno : -EIO;
    }

  return 0;
}

static int velacare_sensor_write_frame(const char *payload)
{
  return velacare_sensor_write_frame_internal(payload, true);
}

static int velacare_sensor_write_family_ack(uint32_t request_id,
                                             unsigned int event_type)
{
  char payload[48];

  snprintf(payload, sizeof(payload), "FACK1,%lu,%u",
           (unsigned long)request_id, event_type);
  return velacare_sensor_write_frame_internal(payload, false);
}

static int velacare_sensor_write_sos(void)
{
  char payload[40];

  snprintf(payload, sizeof(payload), "SOS1,%lu",
           (unsigned long)g_snapshot.sos_request_id);
  return velacare_sensor_write_frame(payload);
}

static int velacare_sensor_write_sos_cancel(void)
{
  char payload[40];

  snprintf(payload, sizeof(payload), "SOSX1,%lu",
           (unsigned long)g_snapshot.sos_request_id);
  return velacare_sensor_write_frame(payload);
}

static int velacare_sensor_write_care(void)
{
  char payload[48];

  snprintf(payload, sizeof(payload), "CARE1,%lu,%d",
           (unsigned long)g_snapshot.care_request_id,
           g_snapshot.care_kind);
  return velacare_sensor_write_frame(payload);
}

static int velacare_sensor_write_care_cancel(void)
{
  char payload[48];

  snprintf(payload, sizeof(payload), "CAREX1,%lu,%d",
           (unsigned long)g_snapshot.care_request_id,
           g_snapshot.care_kind);
  return velacare_sensor_write_frame(payload);
}

int velacare_sensor_send_sos(void)
{
  struct velacare_request_state_s previous;
  uint32_t request_id;
  int ret;

  if (!g_request_state_ready)
    {
      return -EIO;
    }

  if (g_sos_active)
    {
      return -EBUSY;
    }

  velacare_sensor_capture_request_state(&previous);
  ret = velacare_sensor_next_request_id(&request_id);
  if (ret < 0)
    {
      syslog(LOG_ERR, VELACARE_SENSOR_TAG
             " SOS rejected because no durable request id is available: %d\n",
             ret);
      return ret;
    }

  g_snapshot.sos_request_id = request_id;
  g_snapshot.sos_status = VELACARE_SOS_SENDING;
  g_snapshot.sos_family_reply = 0;
  g_sos_active = true;
  g_sos_acked = false;
  g_sos_cancel_pending = false;
  g_sos_last_send_ms = 0;
  g_sos_cancel_last_send_ms = 0;

  ret = velacare_sensor_commit_request_state(&previous, "new SOS");
  if (ret < 0)
    {
      return ret;
    }

  ret = velacare_sensor_write_sos();
  if (ret < 0)
    {
      g_snapshot.sos_status = VELACARE_SOS_RETRYING;
      syslog(LOG_WARNING, VELACARE_SENSOR_TAG
             " SOS id=%lu queued for retry: %d\n",
             (unsigned long)request_id, ret);
      return ret;
    }

  g_snapshot.sos_status = VELACARE_SOS_WAIT_GATEWAY;
  g_sos_last_send_ms = g_now_ms;
  syslog(LOG_NOTICE, VELACARE_SENSOR_TAG " SOS request %lu sent\n",
         (unsigned long)request_id);
  return 0;
}

int velacare_sensor_cancel_sos(void)
{
  struct velacare_request_state_s previous;
  int ret;

  if (!g_sos_active || g_snapshot.sos_request_id == 0)
    {
      return -ENOENT;
    }

  if (g_sos_cancel_pending)
    {
      return -EALREADY;
    }

  velacare_sensor_capture_request_state(&previous);
  g_sos_cancel_pending = true;
  g_snapshot.sos_status = VELACARE_SOS_CANCELLING;
  ret = velacare_sensor_commit_request_state(&previous, "SOS cancel");
  if (ret < 0)
    {
      return ret;
    }

  ret = velacare_sensor_write_sos_cancel();
  if (ret == 0)
    {
      g_sos_cancel_last_send_ms = g_now_ms;
      syslog(LOG_NOTICE, VELACARE_SENSOR_TAG
              " SOSX1 id=%lu sent\n",
              (unsigned long)g_snapshot.sos_request_id);
    }
  else
    {
      syslog(LOG_WARNING, VELACARE_SENSOR_TAG
              " SOSX1 id=%lu queued for retry: %d\n",
              (unsigned long)g_snapshot.sos_request_id, ret);
    }

  return ret;
}

int velacare_sensor_send_care(int kind)
{
  struct velacare_request_state_s previous;
  uint32_t request_id;
  int ret;

  if (kind != VELACARE_CARE_CONTACT_ME &&
      kind != VELACARE_CARE_CHECKIN_OK)
    {
      return -EINVAL;
    }

  if (g_care_active)
    {
      return -EBUSY;
    }

  if (!g_request_state_ready)
    {
      return -EIO;
    }

  velacare_sensor_capture_request_state(&previous);
  ret = velacare_sensor_next_request_id(&request_id);
  if (ret < 0)
    {
      syslog(LOG_ERR, VELACARE_SENSOR_TAG
             " CARE rejected because no durable request id is available: %d\n",
             ret);
      return ret;
    }

  g_snapshot.care_request_id = request_id;
  g_snapshot.care_kind = kind;
  g_snapshot.care_status = VELACARE_CARE_SENDING;
  g_snapshot.care_family_reply = 0;
  g_care_active = true;
  g_care_acked = false;
  g_care_cancel_pending = false;
  g_care_last_send_ms = 0;
  g_care_cancel_last_send_ms = 0;

  ret = velacare_sensor_commit_request_state(&previous, "new CARE");
  if (ret < 0)
    {
      return ret;
    }

  ret = velacare_sensor_write_care();
  if (ret < 0)
    {
      g_snapshot.care_status = VELACARE_CARE_RETRYING;
      syslog(LOG_WARNING, VELACARE_SENSOR_TAG
             " CARE1 id=%lu kind=%d queued for retry: %d\n",
             (unsigned long)request_id, kind, ret);
      return ret;
    }

  g_care_last_send_ms = g_now_ms;
  syslog(LOG_NOTICE, VELACARE_SENSOR_TAG
         " CARE1 id=%lu kind=%d sent\n",
         (unsigned long)request_id, kind);
  return 0;
}

int velacare_sensor_cancel_care(void)
{
  struct velacare_request_state_s previous;
  int ret;

  if (!g_care_active || g_snapshot.care_request_id == 0 ||
      g_snapshot.care_kind == VELACARE_CARE_NONE)
    {
      return -ENOENT;
    }

  if (g_care_cancel_pending)
    {
      return -EALREADY;
    }

  velacare_sensor_capture_request_state(&previous);
  g_care_cancel_pending = true;
  g_snapshot.care_status = VELACARE_CARE_CANCELLING;
  ret = velacare_sensor_commit_request_state(&previous, "CARE cancel");
  if (ret < 0)
    {
      return ret;
    }

  ret = velacare_sensor_write_care_cancel();
  if (ret == 0)
    {
      g_care_cancel_last_send_ms = g_now_ms;
      syslog(LOG_NOTICE, VELACARE_SENSOR_TAG
             " CAREX1 id=%lu type=%d sent\n",
             (unsigned long)g_snapshot.care_request_id,
             g_snapshot.care_kind);
    }
  else
    {
      syslog(LOG_WARNING, VELACARE_SENSOR_TAG
             " CAREX1 id=%lu type=%d queued for retry: %d\n",
             (unsigned long)g_snapshot.care_request_id,
             g_snapshot.care_kind, ret);
    }

  return ret;
}

static int velacare_sensor_write_ok_frame(uint32_t request_id)
{
  char payload[40];
  char frame[52];
  int payload_len;
  int frame_len;
  ssize_t written;
  uint16_t crc;

  if (g_uart_fd < 0 || !g_snapshot.gateway_online)
    {
      return -ENOTCONN;
    }

  payload_len = snprintf(payload, sizeof(payload), "OK1,%lu",
                         (unsigned long)request_id);
  if (payload_len <= 0 || payload_len >= (int)sizeof(payload))
    {
      return -EOVERFLOW;
    }

  crc = velacare_crc16_ccitt((const uint8_t *)payload,
                             (size_t)payload_len);
  frame_len = snprintf(frame, sizeof(frame), "%s*%04X\n", payload, crc);
  if (frame_len <= 0 || frame_len >= (int)sizeof(frame))
    {
      return -EOVERFLOW;
    }

  written = write(g_uart_fd, frame, (size_t)frame_len);
  if (written != frame_len)
    {
      syslog(LOG_WARNING, VELACARE_SENSOR_TAG " OK1 write failed\n");
      return written < 0 ? -errno : -EIO;
    }

  syslog(LOG_NOTICE, VELACARE_SENSOR_TAG " OK1 request %lu sent attempt=%d\n",
         (unsigned long)request_id, g_ok_attempts + 1);
  return 0;
}

static void velacare_sensor_retry_ok(uint32_t now_ms)
{
  int ret;

  if (g_pending_ok_id == 0 || g_ok_acked)
    {
      return;
    }

  if (g_uart_fd < 0 || !g_snapshot.gateway_online)
    {
      return;
    }

  if (g_ok_attempts >= VELACARE_OK_RETRY_MAX)
    {
      syslog(LOG_WARNING, VELACARE_SENSOR_TAG
             " OK1 id=%lu no OACK after %d attempts; check D12X TX1(PA.2)->ESP32 GPIO18\n",
             (unsigned long)g_pending_ok_id, g_ok_attempts);
      g_pending_ok_id = 0;
      return;
    }

  if (g_ok_last_send_ms != 0 &&
      now_ms - g_ok_last_send_ms < VELACARE_OK_RETRY_MS)
    {
      return;
    }

  ret = velacare_sensor_write_ok_frame(g_pending_ok_id);
  if (ret == 0)
    {
      g_ok_attempts++;
      g_ok_last_send_ms = now_ms;
    }
}

static void velacare_sensor_retry_user_requests(uint32_t now_ms)
{
  int ret;

  if (g_sos_active && g_sos_cancel_pending)
    {
      if (g_sos_cancel_last_send_ms == 0 ||
          now_ms - g_sos_cancel_last_send_ms >= VELACARE_REQUEST_RETRY_MS)
        {
          ret = velacare_sensor_write_sos_cancel();
          if (ret == 0)
            {
              g_sos_cancel_last_send_ms = now_ms;
              g_snapshot.sos_status = VELACARE_SOS_CANCELLING;
            }
        }
    }
  else if (g_sos_active && !g_sos_acked &&
           (g_sos_last_send_ms == 0 ||
            now_ms - g_sos_last_send_ms >= VELACARE_REQUEST_RETRY_MS))
    {
      ret = velacare_sensor_write_sos();
      if (ret == 0)
        {
          g_sos_last_send_ms = now_ms;
          g_snapshot.sos_status = VELACARE_SOS_WAIT_GATEWAY;
        }
      else
        {
          g_snapshot.sos_status = VELACARE_SOS_RETRYING;
        }
    }

  if (g_care_active && g_care_cancel_pending)
    {
      if (g_care_cancel_last_send_ms == 0 ||
          now_ms - g_care_cancel_last_send_ms >=
            VELACARE_REQUEST_RETRY_MS)
        {
          ret = velacare_sensor_write_care_cancel();
          if (ret == 0)
            {
              g_care_cancel_last_send_ms = now_ms;
              g_snapshot.care_status = VELACARE_CARE_CANCELLING;
            }
        }
    }
  else if (g_care_active && !g_care_acked &&
           (g_care_last_send_ms == 0 ||
            now_ms - g_care_last_send_ms >= VELACARE_REQUEST_RETRY_MS))
    {
      ret = velacare_sensor_write_care();
      if (ret == 0)
        {
          g_care_last_send_ms = now_ms;
          g_snapshot.care_status = VELACARE_CARE_SENDING;
        }
      else
        {
          g_snapshot.care_status = VELACARE_CARE_RETRYING;
        }
    }
}

int velacare_sensor_send_ok(void)
{
  uint32_t request_id;
  int ret;

  ret = velacare_sensor_next_request_id(&request_id);
  if (ret < 0)
    {
      syslog(LOG_ERR, VELACARE_SENSOR_TAG
             " OK1 rejected because no durable request id is available: %d\n",
             ret);
      return ret;
    }

  g_elder_confirmed = true;
  g_pending_ok_id = request_id;
  g_ok_acked = false;
  g_ok_attempts = 0;
  g_ok_last_send_ms = 0;

  if (g_uart_fd < 0 || !g_snapshot.gateway_online)
    {
      syslog(LOG_WARNING, VELACARE_SENSOR_TAG
             " OK1 local only, gateway offline\n");
      return -ENOTCONN;
    }

  ret = velacare_sensor_write_ok_frame(request_id);
  if (ret == 0)
    {
      g_ok_attempts = 1;
      g_ok_last_send_ms = g_now_ms;
    }

  return ret;
}

void velacare_sensor_poll(uint32_t now_ms)
{
  uint8_t buffer[64];
  ssize_t nread;
  size_t total_read = 0;
  unsigned int read_calls = 0;

  g_now_ms = now_ms;
  velacare_sensor_open(now_ms);

  if (g_uart_fd >= 0)
    {
      do
        {
          nread = read(g_uart_fd, buffer, sizeof(buffer));
          if (nread > 0)
            {
              velacare_sensor_consume(buffer, (size_t)nread, now_ms);
              total_read += (size_t)nread;
              read_calls++;
            }
        }
      while (nread > 0 && total_read < VELACARE_UART_READ_BUDGET &&
             read_calls < VELACARE_UART_READ_CALLS_MAX);

      if (nread < 0 && errno != EAGAIN && errno != EWOULDBLOCK)
        {
          syslog(LOG_WARNING, VELACARE_SENSOR_TAG " read failed: %d\n",
                 errno);
          close(g_uart_fd);
          g_uart_fd = -1;
          g_next_open_ms = now_ms + VELACARE_SENSOR_REOPEN_MS;
        }
    }

  if (g_snapshot.have_frame &&
      now_ms - g_snapshot.last_frame_ms > VELACARE_SENSOR_TIMEOUT_MS)
    {
      g_snapshot.have_frame = false;
      velacare_sensor_set_offline();
      syslog(LOG_WARNING, VELACARE_SENSOR_TAG " gateway heartbeat timeout\n");
    }

  velacare_sensor_retry_ok(now_ms);
  velacare_sensor_retry_user_requests(now_ms);
}

bool velacare_sensor_gateway_online(void)
{
  return g_snapshot.gateway_online;
}

bool velacare_sensor_wifi_online(void)
{
  return g_snapshot.wifi_online;
}

uint32_t velacare_sensor_last_sequence(void)
{
  return g_snapshot.sequence;
}

bool velacare_sensor_smoke_online(void) { return g_snapshot.online[0]; }
bool velacare_sensor_smoke_alarm(void) { return g_snapshot.alarm[0]; }
bool velacare_sensor_water_online(void) { return g_snapshot.online[1]; }
bool velacare_sensor_water_alarm(void) { return g_snapshot.alarm[1]; }
bool velacare_sensor_door_online(void) { return g_snapshot.online[2]; }
bool velacare_sensor_door_alarm(void) { return g_snapshot.alarm[2]; }
bool velacare_sensor_fall_online(void) { return g_snapshot.online[3]; }
bool velacare_sensor_fall_alarm(void) { return g_snapshot.alarm[3]; }

bool velacare_sensor_caregiver_configured(void)
{
  return g_snapshot.caregiver_configured;
}

const char *velacare_sensor_caregiver_masked(void)
{
  return g_snapshot.caregiver_masked;
}

int velacare_sensor_sos_status(void)
{
  return g_snapshot.sos_status;
}

int velacare_sensor_care_kind(void)
{
  return g_snapshot.care_kind;
}

int velacare_sensor_care_status(void)
{
  return g_snapshot.care_status;
}

int velacare_sensor_family_reply(void)
{
  return g_snapshot.care_family_reply;
}

int velacare_sensor_sos_family_reply(void)
{
  return g_snapshot.sos_family_reply;
}

bool velacare_sensor_buzzer_suppressed(void)
{
  return g_buzzer_suppressed;
}

bool velacare_sensor_family_acknowledged(void)
{
  return g_family_acknowledged;
}

bool velacare_sensor_elder_confirmed(void)
{
  return g_elder_confirmed;
}
