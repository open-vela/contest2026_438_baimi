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

#ifndef __VELACARE_UI_H
#define __VELACARE_UI_H

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define VC_PAGE_HOME     0
#define VC_PAGE_ENV      1
#define VC_PAGE_REMIND   2
#define VC_PAGE_EVENTS   3
#define VC_PAGE_SETTINGS 4
#define VC_PAGE_MAX      5

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

int  vc_ui_init(void);
void vc_ui_deinit(void);
void vc_ui_navigate_to(int page);

#endif /* __VELACARE_UI_H */
