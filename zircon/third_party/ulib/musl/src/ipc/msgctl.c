#include <errno.h>
#include <sys/msg.h>

#include "ipc.h"

int msgctl(int q, int cmd, struct msqid_ds* buf) {
  errno = ENOSYS;
  return -1;
}
