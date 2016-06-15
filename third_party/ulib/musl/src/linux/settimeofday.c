#define _BSD_SOURCE
#include "syscall.h"
#include <sys/time.h>

int settimeofday(const struct timeval* tv, const struct timezone* tz) {
    return syscall(SYS_settimeofday, tv, 0);
}
