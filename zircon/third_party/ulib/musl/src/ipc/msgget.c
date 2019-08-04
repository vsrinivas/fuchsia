#include <errno.h>
#include <sys/msg.h>

#include "ipc.h"

int msgget(key_t k, int flag) {
  errno = ENOSYS;
  return -1;
}
