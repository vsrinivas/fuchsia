#include "threads_impl.h"
#include "zircon_impl.h"
#include <zircon/process.h>
#include <threads.h>

int __pthread_detach(pthread_t t) {
    switch (zxr_thread_detach(&t->zxr_thread)) {
    case ZX_OK:
        return 0;
    case ZX_ERR_BAD_STATE:
        // It already died before it knew to deallocate itself.
        _zx_vmar_unmap(_zx_vmar_root_self(),
                       (uintptr_t)t->tcb_region.iov_base,
                       t->tcb_region.iov_len);
        return 0;
    default:
        return ESRCH;
    }
}

weak_alias(__pthread_detach, pthread_detach);
