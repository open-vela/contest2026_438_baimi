/****************************************************************************
 * apps/velacare/velacare_cron.c — 定时任务 (占位实现)
 ****************************************************************************/

#include <nuttx/config.h>
#include <syslog.h>
#include "velacare_cron.h"

void velacare_cron_init(void)
{
    /* TODO: 注册 cron_add_job 定时任务 */
    /*   - 早 8 点 / 晚 8 点: 生成每日总结 */
    /*   - 每 5 分钟: 传感器巡检 */
    syslog(LOG_NOTICE, "[VelaCare] Cron init (stub)\n");
}
