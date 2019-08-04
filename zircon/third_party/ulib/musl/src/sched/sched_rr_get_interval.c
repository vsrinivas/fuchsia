#include <errno.h>
#include <sched.h>

int sched_rr_get_interval(pid_t pid, struct timespec* ts) {
  errno = ENOSYS;
  return -1;
}
