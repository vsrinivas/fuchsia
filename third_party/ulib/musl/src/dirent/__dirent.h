#include "libc.h"

#define __NEED_off_t

#include <bits/alltypes.h>

#include <runtime/mutex.h>

struct __dirstream {
    int fd;
    off_t tell;
    int buf_pos;
    int buf_end;
    mxr_mutex_t lock;
    char buf[2048];
};
