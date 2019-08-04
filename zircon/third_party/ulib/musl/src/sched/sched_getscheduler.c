#include <errno.h>
#include <sched.h>

int sched_getscheduler(pid_t pid) {
  errno = ENOSYS;
  return -1;
}
