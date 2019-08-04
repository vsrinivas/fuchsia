#include <errno.h>
#include <sys/shm.h>

#include "ipc.h"

int shmctl(int id, int cmd, struct shmid_ds* buf) {
  errno = ENOSYS;
  return -1;
}
