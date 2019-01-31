#include "threads_impl.h"

int pthread_mutexattr_setrobust(pthread_mutexattr_t* a, int robust) {
    // We do not support robust pthread_mutex_ts.
    return ENOSYS;
}
