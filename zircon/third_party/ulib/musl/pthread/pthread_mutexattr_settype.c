#include "threads_impl.h"

int pthread_mutexattr_settype(pthread_mutexattr_t* a, int type) {
    // RECURSIVE and ERRORCHECK are mutually exclusive.  Only allow one of the
    // two (or neither).
    switch (type) {
    case PTHREAD_MUTEX_NORMAL:
    case PTHREAD_MUTEX_RECURSIVE:
    case PTHREAD_MUTEX_ERRORCHECK:
        break;
    default:
        return EINVAL;
    }

    a->__attr = (a->__attr & ~(PTHREAD_MUTEX_TYPE_MASK << PTHREAD_MUTEX_TYPE_SHIFT))
              | (type << PTHREAD_MUTEX_TYPE_SHIFT);

    return 0;
}
