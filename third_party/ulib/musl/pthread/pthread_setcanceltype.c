#include <pthread.h>

#include <errno.h>

int pthread_setcanceltype(int new, int* old) {
    if (new > 1U)
        return EINVAL;
    return ENOSYS;
}
