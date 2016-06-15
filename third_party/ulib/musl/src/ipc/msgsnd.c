#include "ipc.h"
#include "libc.h"
#include "syscall.h"
#include <sys/msg.h>

int msgsnd(int q, const void* m, size_t len, int flag) {
    return syscall(SYS_msgsnd, q, m, len, flag);
}
