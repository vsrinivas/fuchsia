#include <threads.h>

#include "pthread_impl.h"

int thrd_create(thrd_t* thr, thrd_start_t func, void* arg) {
    return thrd_create_with_name(thr, func, arg, "musl-c11");
}

int thrd_create_with_name(thrd_t* thr, thrd_start_t func, void* arg, const char* name) {
    pthread_attr_t attrs = {0};
    attrs.__name = name;
    attrs.__c11 = 1;
    int ret = pthread_create(thr, &attrs, (void* (*)(void*))func, arg);
    switch (ret) {
    case 0:
        return thrd_success;
    case EAGAIN:
        return thrd_nomem;
    default:
        return thrd_error;
    }
}
