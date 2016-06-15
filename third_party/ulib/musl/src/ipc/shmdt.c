#include "ipc.h"
#include "syscall.h"
#include <sys/shm.h>

int shmdt(const void* addr) {
    return syscall(SYS_shmdt, addr);
}
