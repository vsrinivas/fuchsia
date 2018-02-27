#include "futex_impl.h"
#include "threads_impl.h"

int pthread_cond_signal(pthread_cond_t* c) {
    __private_cond_signal(c, 1);
    return 0;
}
