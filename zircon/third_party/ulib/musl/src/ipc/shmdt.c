#include "ipc.h"
#include <errno.h>
#include <sys/shm.h>

int shmdt(const void* addr) {
    errno = ENOSYS;
    return -1;
}
