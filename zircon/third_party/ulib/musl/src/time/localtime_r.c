#include "libc.h"
#include "time_impl.h"
#include <errno.h>

struct tm* __gmtime_r(const time_t* restrict, struct tm* restrict);

struct tm* __localtime_r(const time_t* restrict t, struct tm* restrict tm) {
    // By design the system local time is always UTC.
    return __gmtime_r(t, tm);
}

weak_alias(__localtime_r, localtime_r);
