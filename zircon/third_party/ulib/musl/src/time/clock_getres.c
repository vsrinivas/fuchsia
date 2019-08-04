#include <errno.h>
#include <time.h>

int clock_getres(clockid_t clk, struct timespec* ts) { return ENOSYS; }
