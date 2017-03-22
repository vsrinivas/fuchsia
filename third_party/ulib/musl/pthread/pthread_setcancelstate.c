#include <pthread.h>

#include <errno.h>

int pthread_setcancelstate(int new, int* old) {
    if (new > 2U)
        return EINVAL;
    return ENOSYS;
}
