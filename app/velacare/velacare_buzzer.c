/****************************************************************************
 * apps/velacare/velacare_buzzer.c - D12x DEMO68 onboard buzzer driver
 *
 * BZR1 is driven by PC7/PWM1_B.  The official schematic specifies a 4 kHz
 * PWM input.  PWM channel 0 remains reserved for the LCD backlight.
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <sys/ioctl.h>
#include <syslog.h>
#include <unistd.h>

#include <nuttx/timers/pwm.h>

#include "velacare_buzzer.h"

#define VELACARE_BUZZER_DEVICE    "/dev/pwm1"
#define VELACARE_BUZZER_FREQUENCY 4000
#define VELACARE_BUZZER_MAX_DUTY  0x7fff
#define VELACARE_BUZZER_MIN_VOLUME 20

static int g_buzzer_fd = -1;
static bool g_buzzer_enabled;
static int g_buzzer_volume = 70;

static int velacare_buzzer_apply(void)
{
  struct pwm_info_s info;
  int ret;

  info.frequency = VELACARE_BUZZER_FREQUENCY;
  info.duty = (uint32_t)VELACARE_BUZZER_MAX_DUTY *
              (uint32_t)g_buzzer_volume / 100u;
  ret = ioctl(g_buzzer_fd, PWMIOC_SETCHARACTERISTICS,
              (unsigned long)&info);
  if (ret < 0)
    {
      syslog(LOG_ERR, "[VelaCare] buzzer setup failed: %d\n", errno);
      return -errno;
    }

  return 0;
}

int velacare_buzzer_init(void)
{
  if (g_buzzer_fd >= 0)
    {
      return 0;
    }

  g_buzzer_fd = open(VELACARE_BUZZER_DEVICE, O_RDWR);
  if (g_buzzer_fd < 0)
    {
      syslog(LOG_ERR, "[VelaCare] open %s failed: %d\n",
             VELACARE_BUZZER_DEVICE, errno);
      return -errno;
    }

  syslog(LOG_NOTICE, "[VelaCare] buzzer ready: PC7/PWM1_B, 4 kHz\n");
  return 0;
}

int velacare_buzzer_set(bool enabled)
{
  int ret;

  if (g_buzzer_fd < 0)
    {
      return -ENODEV;
    }

  if (enabled == g_buzzer_enabled)
    {
      return 0;
    }

  if (!enabled)
    {
      ret = ioctl(g_buzzer_fd, PWMIOC_STOP, 0);
      if (ret < 0)
        {
          syslog(LOG_ERR, "[VelaCare] buzzer stop failed: %d\n", errno);
          return -errno;
        }

      g_buzzer_enabled = false;
      return 0;
    }

  ret = velacare_buzzer_apply();
  if (ret < 0)
    {
      return ret;
    }

  ret = ioctl(g_buzzer_fd, PWMIOC_START, 0);
  if (ret < 0)
    {
      syslog(LOG_ERR, "[VelaCare] buzzer start failed: %d\n", errno);
      return -errno;
    }

  g_buzzer_enabled = true;
  return 0;
}

int velacare_buzzer_set_volume(int percent)
{
  if (percent < VELACARE_BUZZER_MIN_VOLUME || percent > 100)
    {
      return -EINVAL;
    }

  g_buzzer_volume = percent;
  if (g_buzzer_fd >= 0 && g_buzzer_enabled)
    {
      return velacare_buzzer_apply();
    }

  return 0;
}

int velacare_buzzer_volume(void)
{
  return g_buzzer_volume;
}

bool velacare_buzzer_available(void)
{
  return g_buzzer_fd >= 0;
}
