#include "ipc.h"
#include "libc.h"
#include "syscall.h"
#include <sys/msg.h>

ssize_t msgrcv(int q, void* m, size_t len, long type, int flag) {
    return syscall(SYS_msgrcv, q, m, len, type, flag);
}
