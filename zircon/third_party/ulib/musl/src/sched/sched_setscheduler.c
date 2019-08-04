#include <errno.h>
#include <sched.h>

int sched_setscheduler(pid_t pid, int sched, const struct sched_param* param) {
  errno = ENOSYS;
  return -1;
}
