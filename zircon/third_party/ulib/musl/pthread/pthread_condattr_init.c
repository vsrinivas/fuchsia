#include "threads_impl.h"

int pthread_condattr_init(pthread_condattr_t* a) {
    *a = (pthread_condattr_t){};
    return 0;
}
