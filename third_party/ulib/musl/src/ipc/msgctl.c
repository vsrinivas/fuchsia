#include "ipc.h"
#include <errno.h>
#include <sys/msg.h>

int msgctl(int q, int cmd, struct msqid_ds* buf) {
    errno = ENOSYS;
    return -1;
}
