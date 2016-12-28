#include <sys/sem.h>

#include <errno.h>

int semop(int id, struct sembuf* buf, size_t n) {
    errno = ENOSYS;
    return -1;
}
