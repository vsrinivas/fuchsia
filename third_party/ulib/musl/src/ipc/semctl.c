#include <sys/sem.h>

#include <errno.h>

int semctl(int id, int num, int cmd, ...) {
    errno = ENOSYS;
    return -1;
}
