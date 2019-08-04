#include <errno.h>
#include <sched.h>

int sched_get_priority_max(int policy) {
  errno = ENOSYS;
  return -1;
}

int sched_get_priority_min(int policy) {
  errno = ENOSYS;
  return -1;
}
