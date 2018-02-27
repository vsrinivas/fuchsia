#include "threads_impl.h"

int pthread_mutexattr_init(pthread_mutexattr_t* a) {
    *a = (pthread_mutexattr_t){};
    return 0;
}
