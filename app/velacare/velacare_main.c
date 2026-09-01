/****************************************************************************
 * Copyright (C) 2026 Team baimi (contest2026_438)
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <syslog.h>

#ifndef CONFIG_LV_USE_NUTTX_LIBUV
#include <unistd.h>
#else
#include <uv.h>
#endif

#include <lvgl/lvgl.h>

#include "velacare_core.h"
#include "velacare_ui.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define VC_MAIN_TAG "velacare"

/****************************************************************************
 * Private Functions
 ****************************************************************************/

#ifdef CONFIG_LV_USE_NUTTX_LIBUV
static void lv_nuttx_uv_loop(uv_loop_t* loop, lv_nuttx_result_t* result)
{
  lv_nuttx_uv_t uv_info;
  void* data;

  uv_loop_init(loop);

  lv_memset(&uv_info, 0, sizeof(uv_info));
  uv_info.loop = loop;
  uv_info.disp = result->disp;
  uv_info.indev = result->indev;
#ifdef CONFIG_UINPUT_TOUCH
  uv_info.uindev = result->utouch_indev;
#endif

  data = lv_nuttx_uv_init(&uv_info);
  uv_run(loop, UV_RUN_DEFAULT);
  lv_nuttx_uv_deinit(&data);
}
#else
static void lv_nuttx_loop(void)
{
  while (1)
    {
      uint32_t idle;

      idle = lv_timer_handler();
      idle = idle ? idle : 1;
      usleep(idle * 1000);
    }
}
#endif

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int main(int argc, FAR char* argv[])
{
  lv_nuttx_dsc_t info;
  lv_nuttx_result_t result;
#ifdef CONFIG_LV_USE_NUTTX_LIBUV
  uv_loop_t ui_loop;
#endif
  bool sim_mode = false;
  bool selftest = false;
  int ret;
  int i;

  syslog(LOG_INFO, "%s: VelaCare starting\n", VC_MAIN_TAG);

  for (i = 1; i < argc; i++)
    {
      if (strcmp(argv[i], "--sim") == 0 || strcmp(argv[i], "sim") == 0)
        {
          sim_mode = true;
        }
      else if (strcmp(argv[i], "--selftest") == 0 ||
               strcmp(argv[i], "selftest") == 0)
        {
          selftest = true;
        }
    }

  if (lv_is_initialized())
    {
      syslog(LOG_ERR, "%s: LVGL already initialized\n", VC_MAIN_TAG);
      return -1;
    }

  lv_init();
  lv_nuttx_dsc_init(&info);

#ifdef CONFIG_LV_USE_NUTTX_LCD
  info.fb_path = "/dev/lcd0";
#endif

  lv_nuttx_init(&info, &result);
  if (result.disp == NULL)
    {
      syslog(LOG_ERR, "%s: display init failed\n", VC_MAIN_TAG);
      lv_deinit();
      return -1;
    }

  ret = vc_core_init(NULL);
  if (ret < 0)
    {
      syslog(LOG_ERR, "%s: core init failed: %d\n", VC_MAIN_TAG, ret);
      lv_nuttx_deinit(&result);
      lv_deinit();
      return ret;
    }

  if (sim_mode)
    {
      vc_sensor_set_sim(true);
      syslog(LOG_INFO, "%s: simulated sensor mode enabled\n", VC_MAIN_TAG);
    }

  /* AI Agent 集成（可选，失败自动降级到本地规则） */
  ret = vc_agent_init();
  if (ret < 0)
    {
      syslog(LOG_WARNING, "%s: agent init failed: %d\n", VC_MAIN_TAG, ret);
    }

  ret = vc_ui_init();
  if (ret < 0)
    {
      syslog(LOG_ERR, "%s: UI init failed: %d\n", VC_MAIN_TAG, ret);
      vc_agent_deinit();
      vc_core_deinit();
      lv_nuttx_deinit(&result);
      lv_deinit();
      return ret;
    }

  if (selftest)
    {
      ret = vc_selftest();
      syslog(LOG_INFO, "%s: selftest returned %d\n", VC_MAIN_TAG, ret);
    }

#ifdef CONFIG_LV_USE_NUTTX_LIBUV
  lv_memset(&ui_loop, 0, sizeof(uv_loop_t));
  syslog(LOG_INFO, "%s: entering libuv event loop\n", VC_MAIN_TAG);
  lv_nuttx_uv_loop(&ui_loop, &result);
#else
  if (result.indev != NULL)
    {
      lv_indev_set_mode(result.indev, LV_INDEV_MODE_TIMER);
      lv_timer_t* t = lv_indev_get_read_timer(result.indev);

      if (t != NULL)
        {
          lv_timer_set_period(t, 30);
        }
    }

  syslog(LOG_INFO, "%s: entering poll loop, disp=%p indev=%p\n",
         VC_MAIN_TAG, (void*)result.disp, (void*)result.indev);
  lv_nuttx_loop();
#endif

  vc_ui_deinit();
  vc_agent_deinit();
  vc_core_deinit();
  lv_nuttx_deinit(&result);
  lv_deinit();

  syslog(LOG_INFO, "%s: VelaCare exiting\n", VC_MAIN_TAG);
  return 0;
}
