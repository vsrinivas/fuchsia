#include <errno.h>
#include <time.h>
#include <unistd.h>

int clock_getcpuclockid(pid_t pid, clockid_t* clk) { return ENOSYS; }
