/****************************************************************************
 * apps/velacare/velacare_skill.c — Skill 装载 (占位实现)
 ****************************************************************************/

#include <nuttx/config.h>
#include <syslog.h>
#include "velacare_skill.h"

void velacare_skill_init(void)
{
    /* TODO: 扫描 /data/agent/skills/ 并注册到 ai_agent */
    syslog(LOG_NOTICE, "[VelaCare] Skill loader init (stub)\n");
}
