#include <pthread.h>

#include <errno.h>

int pthread_setcanceltype(int new, int* old) {
    if (new != PTHREAD_CANCEL_ENABLE &&
        new != PTHREAD_CANCEL_DISABLE)
        return EINVAL;
    return ENOSYS;
}
