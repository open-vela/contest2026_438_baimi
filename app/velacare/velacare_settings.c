/****************************************************************************
 * apps/velacare/velacare_settings.c - LittleFS-backed local settings
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <syslog.h>
#include <unistd.h>

#include "velacare_settings.h"

#define SETTINGS_DIR     "/data/velacare"
#define SETTINGS_PATH    SETTINGS_DIR "/settings.bin"
#define SETTINGS_TMP     SETTINGS_DIR "/settings.tmp"
#define SETTINGS_MAGIC   0x56435331u
#define SETTINGS_VERSION 2u
#define SETTINGS_VERSION_LEGACY 1u
#define REQUEST_STATE_PATH SETTINGS_DIR "/request-state.bin"
#define REQUEST_STATE_TMP  SETTINGS_DIR "/request-state.tmp"
#define REQUEST_STATE_MAGIC 0x56435231u
#define REQUEST_STATE_VERSION 2u
#define REQUEST_STATE_VERSION_LEGACY 1u
#define REQUEST_FAMILY_HISTORY_LEGACY 8u
#define DEFAULT_VOLUME   70
#define DEFAULT_BRIGHTNESS 80
#define REQUEST_ID_FALLBACK_BASE 0x43800000u

struct settings_store_v1_s
{
  uint32_t magic;
  uint16_t version;
  uint8_t volume;
  uint8_t brightness;
  uint32_t checksum;
};

struct settings_store_s
{
  uint32_t magic;
  uint16_t version;
  uint8_t volume;
  uint8_t brightness;
  uint32_t next_request_id;
  uint32_t checksum;
};

struct velacare_request_state_v1_s
{
  uint32_t sos_request_id;
  uint32_t care_request_id;
  int32_t sos_status;
  int32_t care_kind;
  int32_t care_status;
  int32_t family_reply;
  uint8_t sos_active;
  uint8_t sos_acked;
  uint8_t sos_cancel_pending;
  uint8_t care_active;
  uint8_t care_acked;
  uint8_t care_cancel_pending;
  uint8_t family_history_next;
  uint8_t reserved;
  struct velacare_request_history_entry_s
    family_history[REQUEST_FAMILY_HISTORY_LEGACY];
};

struct request_state_store_v1_s
{
  uint32_t magic;
  uint16_t version;
  uint16_t payload_size;
  struct velacare_request_state_v1_s state;
  uint32_t checksum;
};

struct request_state_store_s
{
  uint32_t magic;
  uint16_t version;
  uint16_t payload_size;
  struct velacare_request_state_s state;
  uint32_t checksum;
};

static struct settings_store_s g_settings;
static bool g_settings_loaded;
static bool g_request_id_store_ready;
static int g_settings_load_error = -EIO;

static uint32_t settings_checksum_bytes(const void *value, size_t length)
{
  const uint8_t *data = (const uint8_t *)value;
  uint32_t hash = 2166136261u;
  size_t i;

  for (i = 0; i < length; i++)
    {
      hash ^= data[i];
      hash *= 16777619u;
    }

  return hash;
}

static uint32_t settings_checksum(const struct settings_store_s *settings)
{
  return settings_checksum_bytes(settings,
                                 offsetof(struct settings_store_s, checksum));
}

static uint32_t settings_legacy_checksum(
  const struct settings_store_v1_s *settings)
{
  return settings_checksum_bytes(
    settings, offsetof(struct settings_store_v1_s, checksum));
}

static uint32_t request_state_checksum(
  const struct request_state_store_s *stored)
{
  return settings_checksum_bytes(
    stored, offsetof(struct request_state_store_s, checksum));
}

static uint32_t request_state_v1_checksum(
  const struct request_state_store_v1_s *stored)
{
  return settings_checksum_bytes(
    stored, offsetof(struct request_state_store_v1_s, checksum));
}

static void settings_defaults(void)
{
  memset(&g_settings, 0, sizeof(g_settings));
  g_settings.magic = SETTINGS_MAGIC;
  g_settings.version = SETTINGS_VERSION;
  g_settings.volume = DEFAULT_VOLUME;
  g_settings.brightness = DEFAULT_BRIGHTNESS;
  g_settings.next_request_id = 0;
  g_settings.checksum = settings_checksum(&g_settings);
}

static int settings_atomic_write(const char *temporary_path,
                                 const char *final_path,
                                 const void *value, size_t length)
{
  const uint8_t *data;
  size_t remaining;
  int fd;
  int error = 0;
  ssize_t written;

  if (mkdir(SETTINGS_DIR, 0755) < 0 && errno != EEXIST)
    {
      return -errno;
    }

  fd = open(temporary_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if (fd < 0)
    {
      return -errno;
    }

  data = (const uint8_t *)value;
  remaining = length;
  while (remaining > 0)
    {
      written = write(fd, data, remaining);
      if (written > 0)
        {
          data += written;
          remaining -= (size_t)written;
          continue;
        }

      if (written < 0 && errno == EINTR)
        {
          continue;
        }

      error = written < 0 ? errno : EIO;
      break;
    }

  if (error == 0 && fsync(fd) < 0)
    {
      error = errno;
    }

  if (close(fd) < 0 && error == 0)
    {
      error = errno;
    }

  if (error != 0)
    {
      (void)unlink(temporary_path);
      return -error;
    }

  if (rename(temporary_path, final_path) < 0)
    {
      error = errno;
      (void)unlink(temporary_path);
      return -error;
    }

  return 0;
}

static int settings_read_file(const char *path, void *value,
                              size_t capacity, size_t *length)
{
  uint8_t *data = (uint8_t *)value;
  size_t total = 0;
  uint8_t extra;
  int fd;
  int error = 0;
  ssize_t nread;

  if (value == NULL || length == NULL || capacity == 0)
    {
      return -EINVAL;
    }

  fd = open(path, O_RDONLY);
  if (fd < 0)
    {
      return -errno;
    }

  while (total < capacity)
    {
      nread = read(fd, data + total, capacity - total);
      if (nread > 0)
        {
          total += (size_t)nread;
          continue;
        }

      if (nread < 0 && errno == EINTR)
        {
          continue;
        }

      if (nread < 0)
        {
          error = errno;
        }

      break;
    }

  if (error == 0 && total == capacity)
    {
      do
        {
          nread = read(fd, &extra, sizeof(extra));
        }
      while (nread < 0 && errno == EINTR);

      if (nread < 0)
        {
          error = errno;
        }
      else if (nread != 0)
        {
          error = EOVERFLOW;
        }
    }

  if (close(fd) < 0 && error == 0)
    {
      error = errno;
    }

  if (error != 0)
    {
      return -error;
    }

  *length = total;
  return 0;
}

static int settings_save(void)
{
  int ret;

  g_settings.checksum = settings_checksum(&g_settings);
  ret = settings_atomic_write(SETTINGS_TMP, SETTINGS_PATH, &g_settings,
                              sizeof(g_settings));
  if (ret == 0)
    {
      g_request_id_store_ready = true;
      g_settings_load_error = 0;
    }

  return ret;
}

int velacare_settings_init(void)
{
  struct settings_store_s stored;
  struct settings_store_v1_s legacy;
  uint8_t raw[sizeof(struct settings_store_s)];
  size_t length = 0;
  int ret;

  settings_defaults();
  g_settings_loaded = false;
  g_request_id_store_ready = false;
  g_settings_load_error = -EIO;

  memset(raw, 0, sizeof(raw));
  ret = settings_read_file(SETTINGS_PATH, raw, sizeof(raw), &length);
  if (ret == -ENOENT)
    {
      syslog(LOG_NOTICE, "[VelaCare] using default local settings\n");
      g_settings_loaded = true;
      ret = settings_save();
      if (ret < 0)
        {
          g_settings_load_error = ret;
          syslog(LOG_ERR,
                 "[VelaCare] failed to create settings store: %d; new request ids disabled\n",
                 ret);
        }

      return ret;
    }

  if (ret < 0)
    {
      g_settings_load_error = ret;
      syslog(LOG_ERR,
             "[VelaCare] settings read failed: %d; file left untouched and new request ids disabled\n",
             ret);
      return ret;
    }

  if (length == sizeof(stored))
    {
      memcpy(&stored, raw, sizeof(stored));
      if (stored.magic == SETTINGS_MAGIC &&
          stored.version == SETTINGS_VERSION && stored.volume >= 20 &&
          stored.volume <= 100 && stored.brightness >= 20 &&
          stored.brightness <= 100 &&
          stored.checksum == settings_checksum(&stored))
        {
          g_settings = stored;
          g_settings_loaded = true;
          g_request_id_store_ready = true;
          g_settings_load_error = 0;
          syslog(LOG_NOTICE,
                 "[VelaCare] settings loaded: volume=%u brightness=%u request_next=%lu\n",
                 g_settings.volume, g_settings.brightness,
                 (unsigned long)g_settings.next_request_id);
          return 0;
        }
    }
  else if (length == sizeof(legacy))
    {
      memcpy(&legacy, raw, sizeof(legacy));
      if (legacy.magic == SETTINGS_MAGIC &&
          legacy.version == SETTINGS_VERSION_LEGACY &&
          legacy.volume >= 20 && legacy.volume <= 100 &&
          legacy.brightness >= 20 && legacy.brightness <= 100 &&
          legacy.checksum == settings_legacy_checksum(&legacy))
        {
          g_settings.magic = SETTINGS_MAGIC;
          g_settings.version = SETTINGS_VERSION;
          g_settings.volume = legacy.volume;
          g_settings.brightness = legacy.brightness;
          g_settings.next_request_id = 0;
          g_settings_loaded = true;
          syslog(LOG_NOTICE,
                 "[VelaCare] migrating settings v1 without changing volume/brightness\n");
          ret = settings_save();
          if (ret < 0)
            {
              g_settings_load_error = ret;
              syslog(LOG_ERR,
                     "[VelaCare] settings v1 migration save failed: %d; new request ids disabled until retry succeeds\n",
                     ret);
            }

          return ret;
        }
    }

  g_settings_load_error = -EINVAL;
  syslog(LOG_ERR,
         "[VelaCare] invalid settings; file left untouched and new request ids disabled\n");
  return -EINVAL;
}

bool velacare_settings_ready(void)
{
  return g_settings_loaded && g_request_id_store_ready;
}

int velacare_settings_volume(void)
{
  return g_settings.volume;
}

int velacare_settings_brightness(void)
{
  return g_settings.brightness;
}

int velacare_settings_set_volume(int value)
{
  struct settings_store_s previous;
  int ret;

  if (value < 20 || value > 100)
    {
      return -EINVAL;
    }

  if (!g_settings_loaded)
    {
      return g_settings_load_error != 0 ? g_settings_load_error : -EIO;
    }

  previous = g_settings;
  g_settings.volume = (uint8_t)value;
  ret = settings_save();
  if (ret < 0)
    {
      g_settings = previous;
    }

  return ret;
}

int velacare_settings_set_brightness(int value)
{
  struct settings_store_s previous;
  int ret;

  if (value < 20 || value > 100)
    {
      return -EINVAL;
    }

  if (!g_settings_loaded)
    {
      return g_settings_load_error != 0 ? g_settings_load_error : -EIO;
    }

  previous = g_settings;
  g_settings.brightness = (uint8_t)value;
  ret = settings_save();
  if (ret < 0)
    {
      g_settings = previous;
    }

  return ret;
}

int velacare_settings_reserve_request_ids(uint32_t seed, uint32_t count,
                                           uint32_t *first_id,
                                           uint32_t *past_last_id)
{
  struct settings_store_s previous;
  uint32_t first;
  uint32_t limit;
  int ret;

  if (first_id == NULL || past_last_id == NULL || count == 0)
    {
      return -EINVAL;
    }

  /* Never let callers accidentally keep using an ID returned by an earlier
   * successful reservation after this reservation fails.
   */

  *first_id = 0;
  *past_last_id = 0;

  if (!velacare_settings_ready())
    {
      return g_settings_load_error != 0 ? g_settings_load_error : -EIO;
    }

  previous = g_settings;
  first = previous.next_request_id;
  if (first == 0)
    {
      /* A v1 migration has no historical high-water mark.  Pick a nontrivial
       * persisted starting point once; all later allocations are monotonic.
       */

      first = seed & 0x7fffffffu;
      first |= 0x10000000u;
      if (first == 0 || first > UINT32_MAX - count)
        {
          first = REQUEST_ID_FALLBACK_BASE;
        }
    }

  if (first == 0 || first > UINT32_MAX - count)
    {
      syslog(LOG_ERR,
             "[VelaCare] request id space exhausted at %lu count=%lu\n",
             (unsigned long)first, (unsigned long)count);
      return -EOVERFLOW;
    }

  limit = first + count;
  g_settings.next_request_id = limit;
  ret = settings_save();
  if (ret < 0)
    {
      g_settings = previous;
      syslog(LOG_ERR,
             "[VelaCare] failed to persist request id reservation: %d\n",
             ret);
      return ret;
    }

  *first_id = first;
  *past_last_id = limit;
  syslog(LOG_NOTICE,
         "[VelaCare] reserved request ids [%lu, %lu)\n",
         (unsigned long)first, (unsigned long)limit);
  return 0;
}

static bool request_state_payload_valid(
  const struct velacare_request_state_s *state)
{
  size_t i;

  if (state->sos_active > 1 || state->sos_acked > 1 ||
      state->sos_cancel_pending > 1 || state->care_active > 1 ||
      state->care_acked > 1 || state->care_cancel_pending > 1 ||
      state->family_history_next >= VELACARE_REQUEST_FAMILY_HISTORY ||
      state->sos_status < -1 || state->sos_status > 7 ||
      state->care_kind < 0 || state->care_kind > 2 ||
      state->care_status < -1 || state->care_status > 6 ||
      state->sos_family_reply < 0 || state->sos_family_reply > 4 ||
      state->care_family_reply < 0 || state->care_family_reply > 4)
    {
      return false;
    }

  if ((state->sos_active != 0 && state->sos_request_id == 0) ||
      (state->sos_cancel_pending != 0 && state->sos_active == 0) ||
      (state->care_active != 0 &&
       (state->care_request_id == 0 || state->care_kind == 0)) ||
      (state->care_cancel_pending != 0 && state->care_active == 0))
    {
      return false;
    }

  for (i = 0; i < VELACARE_REQUEST_FAMILY_HISTORY; i++)
    {
      if (state->family_history[i].event_type > 2 ||
          (state->family_history[i].request_id == 0 &&
           state->family_history[i].event_type != 0))
        {
          return false;
        }
    }

  return true;
}

static bool request_state_v1_payload_valid(
  const struct velacare_request_state_v1_s *state)
{
  size_t i;

  if (state->sos_active > 1 || state->sos_acked > 1 ||
      state->sos_cancel_pending > 1 || state->care_active > 1 ||
      state->care_acked > 1 || state->care_cancel_pending > 1 ||
      state->family_history_next >= REQUEST_FAMILY_HISTORY_LEGACY ||
      state->sos_status < -1 || state->sos_status > 7 ||
      state->care_kind < 0 || state->care_kind > 2 ||
      state->care_status < -1 || state->care_status > 6 ||
      state->family_reply < 0 || state->family_reply > 4)
    {
      return false;
    }

  if ((state->sos_active != 0 && state->sos_request_id == 0) ||
      (state->sos_cancel_pending != 0 && state->sos_active == 0) ||
      (state->care_active != 0 &&
       (state->care_request_id == 0 || state->care_kind == 0)) ||
      (state->care_cancel_pending != 0 && state->care_active == 0))
    {
      return false;
    }

  for (i = 0; i < REQUEST_FAMILY_HISTORY_LEGACY; i++)
    {
      if (state->family_history[i].event_type > 2 ||
          (state->family_history[i].request_id == 0 &&
           state->family_history[i].event_type != 0))
        {
          return false;
        }
    }

  return true;
}

static void request_state_migrate_v1(
  const struct velacare_request_state_v1_s *legacy,
  struct velacare_request_state_s *state)
{
  size_t count = 0;
  size_t i;
  bool history_full = true;

  memset(state, 0, sizeof(*state));
  state->sos_request_id = legacy->sos_request_id;
  state->care_request_id = legacy->care_request_id;
  state->sos_status = legacy->sos_status;
  state->care_kind = legacy->care_kind;
  state->care_status = legacy->care_status;
  state->sos_family_reply = 0;
  state->care_family_reply = legacy->family_reply;
  state->sos_active = legacy->sos_active;
  state->sos_acked = legacy->sos_acked;
  state->sos_cancel_pending = legacy->sos_cancel_pending;
  state->care_active = legacy->care_active;
  state->care_acked = legacy->care_acked;
  state->care_cancel_pending = legacy->care_cancel_pending;

  for (i = 0; i < REQUEST_FAMILY_HISTORY_LEGACY; i++)
    {
      if (legacy->family_history[i].request_id == 0)
        {
          history_full = false;
          break;
        }
    }

  if (history_full)
    {
      for (i = 0; i < REQUEST_FAMILY_HISTORY_LEGACY; i++)
        {
          size_t index = (legacy->family_history_next + i) %
                         REQUEST_FAMILY_HISTORY_LEGACY;
          state->family_history[count++] = legacy->family_history[index];
        }
    }
  else
    {
      for (i = 0; i < REQUEST_FAMILY_HISTORY_LEGACY; i++)
        {
          if (legacy->family_history[i].request_id != 0)
            {
              state->family_history[count++] = legacy->family_history[i];
            }
        }
    }

  state->family_history_next = (uint8_t)count;
}

int velacare_settings_load_request_state(
  struct velacare_request_state_s *state)
{
  struct request_state_store_s stored;
  struct request_state_store_v1_s legacy;
  uint8_t raw[sizeof(struct request_state_store_s)];
  size_t length = 0;
  int ret;

  if (state == NULL)
    {
      return -EINVAL;
    }

  memset(state, 0, sizeof(*state));
  memset(raw, 0, sizeof(raw));
  ret = settings_read_file(REQUEST_STATE_PATH, raw, sizeof(raw), &length);
  if (ret == -ENOENT)
    {
      return 0;
    }

  if (ret < 0)
    {
      return ret;
    }

  if (length == sizeof(stored))
    {
      memcpy(&stored, raw, sizeof(stored));
      if (stored.magic != REQUEST_STATE_MAGIC ||
          stored.version != REQUEST_STATE_VERSION ||
          stored.payload_size != (uint16_t)sizeof(stored.state) ||
          stored.checksum != request_state_checksum(&stored) ||
          !request_state_payload_valid(&stored.state))
        {
          return -EINVAL;
        }

      *state = stored.state;
      return 0;
    }

  if (length == sizeof(legacy))
    {
      memcpy(&legacy, raw, sizeof(legacy));
      if (legacy.magic != REQUEST_STATE_MAGIC ||
          legacy.version != REQUEST_STATE_VERSION_LEGACY ||
          legacy.payload_size != (uint16_t)sizeof(legacy.state) ||
          legacy.checksum != request_state_v1_checksum(&legacy) ||
          !request_state_v1_payload_valid(&legacy.state))
        {
          return -EINVAL;
        }

      request_state_migrate_v1(&legacy.state, state);
      syslog(LOG_NOTICE,
             "[VelaCare] request state v1 loaded in memory; next save will migrate to v2\n");
      return 0;
    }

  return -EINVAL;
}

int velacare_settings_save_request_state(
  const struct velacare_request_state_s *state)
{
  struct request_state_store_s stored;

  if (state == NULL || !request_state_payload_valid(state))
    {
      return -EINVAL;
    }

  memset(&stored, 0, sizeof(stored));
  stored.magic = REQUEST_STATE_MAGIC;
  stored.version = REQUEST_STATE_VERSION;
  stored.payload_size = (uint16_t)sizeof(stored.state);
  stored.state = *state;
  stored.checksum = request_state_checksum(&stored);
  return settings_atomic_write(REQUEST_STATE_TMP, REQUEST_STATE_PATH,
                               &stored, sizeof(stored));
}
