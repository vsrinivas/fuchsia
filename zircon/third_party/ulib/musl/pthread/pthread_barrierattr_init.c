#include "threads_impl.h"

int pthread_barrierattr_init(pthread_barrierattr_t* a) {
    *a = (pthread_barrierattr_t){};
    return 0;
}
