#include "ipc.h"
#include "syscall.h"
#include <sys/msg.h>

int msgget(key_t k, int flag) {
    return syscall(SYS_msgget, k, flag);
}
