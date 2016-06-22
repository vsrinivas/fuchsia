#include <errno.h>
#include <limits.h>
#include <semaphore.h>

int sem_init(sem_t* sem, int pshared, unsigned value) {
    if (value > SEM_VALUE_MAX || pshared) {
        errno = EINVAL;
        return -1;
    }
    sem->__val[0] = value;
    sem->__val[1] = 0;
    sem->__val[2] = 0;
    return 0;
}
