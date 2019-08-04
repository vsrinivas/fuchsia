#include <errno.h>
#include <stdint.h>
#include <sys/shm.h>

#include "ipc.h"

int shmget(key_t key, size_t size, int flag) {
  errno = ENOSYS;
  return -1;
}
