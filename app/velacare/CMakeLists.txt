##############################################################################
# packages/demos/contest2026_438_velacare/CMakeLists.txt
#
# Copyright (C) 2026 Team baimi (contest2026_438)
#
# Licensed under the Apache License, Version 2.0 (the "License"); you may not use
# this file except in compliance with the License. You may obtain a copy of the
# License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software distributed
# under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
# CONDITIONS OF ANY KIND, either express or implied. See the License for the
# specific language governing permissions and limitations under the License.
#
##############################################################################

if(CONFIG_VELACARE_USE_DEMO)
  set(PROGNAME velacare)
  set(PRIORITY 100)
  set(STACKSIZE 40960)
  set(MODULE ${CONFIG_VELACARE_USE_DEMO})
  set(MAINSRC velacare_main.c)
  set(CSRCS velacare_core.c velacare_ui.c velacare_cn_16.c)

  set(VELACARE_INCDIRS)
  if(CONFIG_VELACARE_ENABLE_AGENT)
    # AI Agent client SDK include paths (keep in sync with Makefile)
    list(APPEND VELACARE_INCDIRS
      ${NUTTX_APPS_DIR}/packages/ai_agent/include
      ${NUTTX_APPS_DIR}/packages/ai_agent/src)
  endif()

  nuttx_add_application(
    NAME ${PROGNAME}
    PRIORITY ${PRIORITY}
    STACKSIZE ${STACKSIZE}
    MODULE ${MODULE}
    SRCS ${MAINSRC} ${CSRCS}
    INCLUDE_DIRECTORIES ${VELACARE_INCDIRS}
  )
endif()
