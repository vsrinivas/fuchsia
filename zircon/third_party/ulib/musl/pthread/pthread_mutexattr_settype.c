#include "threads_impl.h"

int pthread_mutexattr_settype(pthread_mutexattr_t* a, int type) {
    if (type & ~PTHREAD_MUTEX_MASK)
        return EINVAL;
    a->__attr = (a->__attr & ~PTHREAD_MUTEX_MASK) | type;
    return 0;
}
