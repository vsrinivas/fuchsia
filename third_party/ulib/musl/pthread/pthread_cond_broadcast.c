#include "futex_impl.h"
#include "pthread_impl.h"

int __private_cond_signal(pthread_cond_t*, int);

int pthread_cond_broadcast(pthread_cond_t* c) {
    return __private_cond_signal(c, -1);
}
