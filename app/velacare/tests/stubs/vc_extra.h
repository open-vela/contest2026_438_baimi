#ifndef _VC_EXTRA_H
#define _VC_EXTRA_H

#include <time.h>

struct tm* localtime_r(const time_t* t, struct tm* r);
struct tm* gmtime_r(const time_t* t, struct tm* r);

#endif
