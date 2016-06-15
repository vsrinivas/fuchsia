#include "ipc.h"
#include "syscall.h"
#include <sys/sem.h>

int semop(int id, struct sembuf* buf, size_t n) {
    return syscall(SYS_semop, id, buf, n);
}
