############################################################################
# VelaCare — 家庭健康守护应用
# 队伍 438 (baimi) · 2026 openvela AI 硬件开发者大赛
############################################################################

include $(APPDIR)/Make.defs

# 应用名称与调度参数
PROGNAME  = velacare
PRIORITY  = SCHED_PRIORITY_DEFAULT
STACKSIZE = 8192
MODULE    = $(CONFIG_LVX_USE_DEMO_CONTEST2026_438_VELACARE)

# 源文件列表 (MAINSRC 不要重复放入 CSRCS)
CSRCS  = lv_font_velacare_cn_18.c \
          lv_font_velacare_cn_28.c \
          velacare_state.c   \
          velacare_sensor.c  \
          velacare_buzzer.c  \
          velacare_settings.c \
          velacare_skill.c   \
          velacare_lvgl.c    \
          velacare_cron.c

# 头文件搜索路径
CFLAGS += -I$(APPDIR)/packages/demos/contest2026_438_velacare

# 构建入口
MAINSRC = velacare_main.c

include $(APPDIR)/Application.mk
