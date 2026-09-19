/****************************************************************************
 * apps/velacare/velacare_buzzer.h - D12x DEMO68 onboard buzzer interface
 ****************************************************************************/

#ifndef __APPS_VELACARE_VELACARE_BUZZER_H
#define __APPS_VELACARE_VELACARE_BUZZER_H

#include <stdbool.h>

int velacare_buzzer_init(void);
int velacare_buzzer_set(bool enabled);
int velacare_buzzer_set_volume(int percent);
int velacare_buzzer_volume(void);
bool velacare_buzzer_available(void);

#endif
