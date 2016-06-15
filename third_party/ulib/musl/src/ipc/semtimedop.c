#define _GNU_SOURCE
#include "ipc.h"
#include "syscall.h"
#include <sys/sem.h>

int semtimedop(int id, struct sembuf* buf, size_t n, const struct timespec* ts) {
    return syscall(SYS_semtimedop, id, buf, n, ts);
}
