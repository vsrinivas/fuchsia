#include "pthread_impl.h"

int pthread_join(pthread_t t, void** res) {
    intptr_t result;
    switch (mxr_thread_join(t->mxr_thread, &result)) {
    case NO_ERROR:
        if (res)
            *res = (void*)result;
        return 0;
    default:
        return EINVAL;
    }
}
