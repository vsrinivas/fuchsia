#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#define __NEED_time_t

#include <bits/alltypes.h>

struct timeb {
    time_t time;
    unsigned short millitm;
    short timezone, dstflag;
};

int ftime(struct timeb*);

#ifdef __cplusplus
}
#endif
