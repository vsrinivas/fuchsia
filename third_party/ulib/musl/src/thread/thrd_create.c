#define _ALL_SOURCE
#include <threads.h>

#include "pthread_impl.h"

static int thrd_create_internal(thrd_t* thr, thrd_start_t func, void* arg, const char* name) {
    pthread_attr_t attrs = DEFAULT_PTHREAD_ATTR;
    attrs.__name = name;
    attrs.__c11 = 1;
    // Technically this could introduce a restrict violation if thr and arg alias.
    int ret = __pthread_create(thr, &attrs, (void* (*)(void*))func, arg);
    switch (ret) {
    case 0:
        return thrd_success;
    case EAGAIN:
        return thrd_nomem;
    default:
        return thrd_error;
    }
}

int thrd_create(thrd_t* thr, thrd_start_t func, void* arg) {
    return thrd_create_internal(thr, func, arg, NULL);
}

__typeof(thrd_create_with_name) thrd_create_with_name
    __attribute__((alias("thrd_create_internal")));
