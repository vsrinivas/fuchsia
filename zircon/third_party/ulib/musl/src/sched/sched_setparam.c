#include <errno.h>
#include <sched.h>

int sched_setparam(pid_t pid, const struct sched_param* param) {
  errno = ENOSYS;
  return -1;
}
