#include "threads_impl.h"

int pthread_mutex_consistent(pthread_mutex_t* m) {
    // We do not support robust pthread_mutex_ts.
    return EINVAL;
}
