#define _GNU_SOURCE
#include <sys/sem.h>

#include <errno.h>

int semtimedop(int id, struct sembuf* buf, size_t n, const struct timespec* ts) {
    errno = ENOSYS;
    return -1;
}
