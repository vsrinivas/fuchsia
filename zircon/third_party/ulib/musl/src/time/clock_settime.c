#include <errno.h>
#include <time.h>

int clock_settime(clockid_t clk, const struct timespec* ts) { return ENOSYS; }
