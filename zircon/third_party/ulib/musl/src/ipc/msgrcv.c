#include "ipc.h"
#include <errno.h>
#include <sys/msg.h>

ssize_t msgrcv(int q, void* m, size_t len, long type, int flag) {
    errno = ENOSYS;
    return -1;
}
