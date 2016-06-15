#include "ipc.h"
#include "syscall.h"
#include <sys/msg.h>

int msgctl(int q, int cmd, struct msqid_ds* buf) {
    return syscall(SYS_msgctl, q, cmd | IPC_64, buf);
}
