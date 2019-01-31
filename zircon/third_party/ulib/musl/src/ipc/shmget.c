#include "ipc.h"
#include <stdint.h>
#include <errno.h>
#include <sys/shm.h>

int shmget(key_t key, size_t size, int flag) {
    errno = ENOSYS;
    return -1;
}
