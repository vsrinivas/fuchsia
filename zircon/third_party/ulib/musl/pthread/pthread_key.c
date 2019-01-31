#include "threads_impl.h"

#include <assert.h>

static_assert(TSS_DTOR_ITERATIONS == PTHREAD_DESTRUCTOR_ITERATIONS, "");

int pthread_key_create(pthread_key_t* k, void (*dtor)(void*)) {
    return tss_create(k, dtor) == thrd_success ? 0 : EAGAIN;
}

int pthread_key_delete(pthread_key_t k) {
    tss_delete(k);
    return 0;
}
