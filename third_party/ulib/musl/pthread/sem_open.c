#include <sys/stat.h>

#include <errno.h>

sem_t* sem_open(const char* name, int flags, ...) {
    errno = ENOSYS;
    return SEM_FAILED;
}

int sem_close(sem_t* sem) {
    errno = ENOSYS;
    return -1;
}
