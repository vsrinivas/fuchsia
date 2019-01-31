#include "ipc.h"
#include <errno.h>
#include <sys/msg.h>

int msgget(key_t k, int flag) {
    errno = ENOSYS;
    return -1;
}
