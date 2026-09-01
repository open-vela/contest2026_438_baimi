/* VelaCare core smoke test（Windows 宿主，内存文件系统模拟 NuttX） */

#include <nuttx/config.h>

#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <stdlib.h>
#include <time.h>
#include <pthread.h>
#include <sys/stat.h>
#include <velaclaw/client.h>
#include "vc_extra.h"

#include "velacare_core.h"

/* ---------- 内存文件系统 ---------- */

#define TEST_MAX_FILES 16
#define TEST_FILE_SIZE 8192

typedef struct
{
  char path[128];
  char data[TEST_FILE_SIZE];
  int  len;
  int  open;
} test_file_t;

static test_file_t g_files[TEST_MAX_FILES];
static int g_fd_counter = 100;

static test_file_t* find_file(const char* path)
{
  int i;

  for (i = 0; i < TEST_MAX_FILES; i++)
    {
      if (g_files[i].path[0] != '\0' &&
          strcmp(g_files[i].path, path) == 0)
        {
          return &g_files[i];
        }
    }

  return NULL;
}

static test_file_t* create_file(const char* path)
{
  int i;

  for (i = 0; i < TEST_MAX_FILES; i++)
    {
      if (g_files[i].path[0] == '\0')
        {
          strncpy(g_files[i].path, path, sizeof(g_files[i].path) - 1);
          g_files[i].len = 0;
          g_files[i].open = 0;
          return &g_files[i];
        }
    }

  return NULL;
}

int syslog(int priority, const char* fmt, ...)
{
  va_list ap;
  (void)priority;
  va_start(ap, fmt);
  vprintf(fmt, ap);
  va_end(ap);
  return 0;
}

int mkdir(const char* path, unsigned int mode)
{
  (void)path;
  (void)mode;
  return 0;
}

int stat(const char* path, struct stat* st)
{
  test_file_t* f = find_file(path);

  if (f == NULL)
    {
      return -1;
    }

  st->st_size = f->len;
  st->st_mode = 0100644;
  return 0;
}

int open(const char* path, int flags, ...)
{
  test_file_t* f = find_file(path);

  if (f == NULL)
    {
      if ((flags & 0x40) != 0)   /* O_CREAT */
        {
          f = create_file(path);
          if (f == NULL)
            {
              return -1;
            }
        }
      else
        {
          return -1;
        }
    }

  if ((flags & 0x200) != 0)      /* O_TRUNC */
    {
      f->len = 0;
    }

  f->open = g_fd_counter++;
  return f->open;
}

long read(int fd, void* buf, unsigned long count)
{
  int i;

  for (i = 0; i < TEST_MAX_FILES; i++)
    {
      if (g_files[i].open == fd)
        {
          unsigned long n = count < (unsigned long)g_files[i].len ?
                            count : (unsigned long)g_files[i].len;
          memcpy(buf, g_files[i].data, n);
          return (long)n;
        }
    }

  return -1;
}

long write(int fd, const void* buf, unsigned long count)
{
  int i;

  for (i = 0; i < TEST_MAX_FILES; i++)
    {
      if (g_files[i].open == fd)
        {
          unsigned long room = TEST_FILE_SIZE - (unsigned long)g_files[i].len;
          unsigned long n = count < room ? count : room;
          memcpy(g_files[i].data + g_files[i].len, buf, n);
          g_files[i].len += (int)n;
          return (long)n;
        }
    }

  return -1;
}

int close(int fd)
{
  int i;

  for (i = 0; i < TEST_MAX_FILES; i++)
    {
      if (g_files[i].open == fd)
        {
          g_files[i].open = 0;
          return 0;
        }
    }

  return -1;
}

int unlink(const char* path)
{
  test_file_t* f = find_file(path);

  if (f != NULL)
    {
      f->path[0] = '\0';
      f->len = 0;
      f->open = 0;
    }

  return 0;
}

int rename(const char* oldp, const char* newp)
{
  test_file_t* f = find_file(oldp);

  if (f == NULL)
    {
      return -1;
    }

  strncpy(f->path, newp, sizeof(f->path) - 1);
  return 0;
}

/* ---------- pthread 桩 ---------- */

int pthread_mutex_init(pthread_mutex_t* m, const void* a)
{
  (void)m;
  (void)a;
  return 0;
}

int pthread_mutex_lock(pthread_mutex_t* m)
{
  (void)m;
  return 0;
}

int pthread_mutex_unlock(pthread_mutex_t* m)
{
  (void)m;
  return 0;
}

int pthread_mutex_destroy(pthread_mutex_t* m)
{
  (void)m;
  return 0;
}

/* ---------- 时间桩 ---------- */

struct tm* localtime_r(const time_t* t, struct tm* r)
{
  const struct tm* l = localtime(t);

  if (l == NULL)
    {
      return NULL;
    }

  *r = *l;
  return r;
}

struct tm* gmtime_r(const time_t* t, struct tm* r)
{
  const struct tm* g = gmtime(t);

  if (g == NULL)
    {
      return NULL;
    }

  *r = *g;
  return r;
}

/* ---------- velaclaw 桩（模拟离线） ---------- */

velaclaw_client_t* velaclaw_client_open(const char* name)
{
  (void)name;
  return NULL;
}

void velaclaw_client_close(velaclaw_client_t* c)
{
  (void)c;
}

int velaclaw_ask(velaclaw_client_t* c, const velaclaw_ask_req_t* req,
                 void (*cb)(int, const char*, void*), void* cookie)
{
  (void)c;
  (void)req;
  (void)cb;
  (void)cookie;
  return -1;
}

/* ---------- 测试主体 ---------- */

static int g_fail;

#define CHECK(cond, msg) \
  do { \
    if (!(cond)) \
      { \
        printf("FAIL: %s (line %d)\n", msg, __LINE__); \
        g_fail++; \
      } \
    else \
      { \
        printf("PASS: %s\n", msg); \
      } \
  } while (0)

int main(void)
{
  vc_reminder_t r;
  vc_event_t ev;
  vc_thresholds_t th;
  vc_env_sample_t sample;
  struct tm tm;
  time_t now;
  int reminder_id;
  int count_before;
  int count_after;

  printf("=== VelaCare core smoke test ===\n");

  CHECK(vc_core_init(NULL) == 0, "core init");
  CHECK(vc_agent_init() == 0, "agent init (offline)");
  CHECK(!vc_agent_connected(), "agent offline");

  now = time(NULL);
  localtime_r(&now, &tm);

  /* 1. 初始应为 NORMAL */
  vc_core_tick();
  CHECK(vc_risk_get_level() == VC_RISK_NORMAL, "initial risk NORMAL");

  /* 2. 触发演示异常 -> 应为 EMERGENCY 且告警开启 */
  vc_sensor_trigger_demo_anomaly();
  vc_core_tick();
  vc_core_tick();
  CHECK(vc_risk_get_level() == VC_RISK_EMERGENCY, "anomaly -> EMERGENCY");
  CHECK(vc_alarm_active(), "alarm on for EMERGENCY");
  CHECK(vc_core_pending_has(), "pending notification queued");
  CHECK(vc_core_pending_pop(&ev) == 0, "pending pop");
  CHECK(ev.risk == VC_RISK_EMERGENCY, "pending event is EMERGENCY");

  /* 3. AI 解释离线降级 */
  {
    char reply[256];
    const char* advice = vc_agent_local_advice(VC_RISK_EMERGENCY,
                                               VC_EVENT_ENV);
    strncpy(reply, advice, sizeof(reply) - 1);
    CHECK(strlen(reply) > 10, "local advice available");
  }

  /* 4. 提醒：当前分钟触发 */
  memset(&r, 0, sizeof(r));
  snprintf(r.label, sizeof(r.label), "喝水");
  r.hour = tm.tm_hour;
  r.minute = tm.tm_min;
  r.enabled = true;
  reminder_id = vc_reminder_add(&r);
  CHECK(reminder_id > 0, "reminder add");
  vc_core_tick();
  CHECK(vc_core_pending_has(), "reminder pending");
  CHECK(vc_core_pending_pop(&ev) == 0, "reminder pending pop");
  CHECK(ev.cat == VC_EVENT_REMINDER, "pending is reminder");
  CHECK(ev.src_id == (uint32_t)reminder_id, "reminder src_id carried");

  /* 5. 确认事件 */
  CHECK(vc_event_confirm(ev.id) == 0, "event confirm");

  /* 6. 稍后提醒入队（不触发，仅验证接口） */
  CHECK(vc_reminder_snooze((uint32_t)reminder_id, 5) == 0, "snooze add");
  CHECK(vc_reminder_set_enabled((uint32_t)reminder_id, false) == 0,
        "reminder disable");

  /* 7. 采样数据可读 */
  CHECK(vc_sensor_read(&sample), "sensor sample readable");
  CHECK(sample.temperature_c10 > 100 && sample.temperature_c10 < 500,
        "temperature in sane range");

  /* 8. 事件持久化 round-trip */
  count_before = vc_event_count();
  vc_core_flush();
  vc_core_deinit();
  CHECK(vc_core_init(NULL) == 0, "core re-init");
  count_after = vc_event_count();
  CHECK(count_after == count_before, "events persisted");
  CHECK(vc_reminder_count() == 1, "reminder persisted");

  /* 9. 自检 */
  CHECK(vc_selftest() == 0, "selftest");

  /* 10. 阈值接口 */
  vc_risk_get_thresholds(&th);
  CHECK(th.no_motion_min == 10, "default no-motion threshold");

  /* 11. 清空事件 */
  vc_event_clear_all();
  CHECK(vc_event_count() == 0, "events cleared");

  vc_agent_deinit();
  vc_core_deinit();

  printf("=== %s (%d failures) ===\n", g_fail == 0 ? "ALL PASS" : "FAILED",
         g_fail);
  return g_fail;
}
