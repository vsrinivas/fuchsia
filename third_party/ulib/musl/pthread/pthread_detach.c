#include "pthread_impl.h"
#include <threads.h>

static int __pthread_detach(pthread_t t) {
    switch (mxr_thread_detach(t->mxr_thread)) {
    case NO_ERROR:
        return 0;
    default:
        return EINVAL;
    }
}

weak_alias(__pthread_detach, pthread_detach);
weak_alias(__pthread_detach, thrd_detach);
