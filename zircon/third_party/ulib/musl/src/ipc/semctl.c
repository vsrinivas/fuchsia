#include <errno.h>
#include <sys/sem.h>

int semctl(int id, int num, int cmd, ...) {
  errno = ENOSYS;
  return -1;
}
