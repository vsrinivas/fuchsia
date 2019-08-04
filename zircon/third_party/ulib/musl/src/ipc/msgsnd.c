#include <errno.h>
#include <sys/msg.h>

#include "ipc.h"

int msgsnd(int q, const void* m, size_t len, int flag) {
  errno = ENOSYS;
  return -1;
}
