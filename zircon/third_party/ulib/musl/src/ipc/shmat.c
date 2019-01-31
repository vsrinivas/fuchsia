#include "ipc.h"
#include <errno.h>
#include <stdint.h>
#include <sys/shm.h>

void* shmat(int id, const void* addr, int flag) {
    errno = ENOSYS;
    return (void*)(intptr_t)-1;
}
