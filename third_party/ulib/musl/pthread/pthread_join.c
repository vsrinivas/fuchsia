#include "pthread_impl.h"

int pthread_join(pthread_t t, void** res) {
    switch (mxr_thread_join(t->mxr_thread)) {
    case NO_ERROR:
        if (res)
            *res = t->result;
        return 0;
    default:
        return EINVAL;
    }
}
