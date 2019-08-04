#define _GNU_SOURCE
#include <errno.h>
#include <sys/sem.h>

int semtimedop(int id, struct sembuf* buf, size_t n, const struct timespec* ts) {
  errno = ENOSYS;
  return -1;
}
