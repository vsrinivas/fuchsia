#include <errno.h>
#include <sys/shm.h>

#include "ipc.h"

int shmdt(const void* addr) {
  errno = ENOSYS;
  return -1;
}
