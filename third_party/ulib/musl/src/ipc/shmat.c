#include "ipc.h"
#include "syscall.h"
#include <sys/shm.h>

void* shmat(int id, const void* addr, int flag) {
    return (void*)syscall(SYS_shmat, id, addr, flag);
}
