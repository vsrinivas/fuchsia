#include <errno.h>
#include <limits.h>
#include <sys/sem.h>

int semget(key_t key, int n, int fl) {
  errno = ENOSYS;
  return -1;
}
