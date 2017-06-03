#include "pthread_impl.h"
#include "magenta_impl.h"
#include <magenta/process.h>
#include <threads.h>

static int __pthread_detach(pthread_t t) {
    switch (mxr_thread_detach(&t->mxr_thread)) {
    case NO_ERROR:
        return 0;
    case ERR_BAD_STATE:
        // It already died before it knew to deallocate itself.
        _mx_vmar_unmap(_mx_vmar_root_self(),
                       (uintptr_t)t->tcb_region.iov_base,
                       t->tcb_region.iov_len);
        return 0;
    default:
        return ESRCH;
    }
}

weak_alias(__pthread_detach, pthread_detach);
weak_alias(__pthread_detach, thrd_detach);
