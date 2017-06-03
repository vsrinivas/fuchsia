#include <magenta/process.h>
#include <magenta/threads.h>

#include <runtime/thread.h>

#include "pthread_impl.h"

mx_handle_t thrd_get_mx_handle(thrd_t t) {
    return mxr_thread_get_handle(&t->mxr_thread);
}

mx_handle_t _mx_thread_self(void) {
    return mxr_thread_get_handle(&__pthread_self()->mxr_thread);
}
__typeof(mx_thread_self) mx_thread_self
    __attribute__((weak, alias("_mx_thread_self")));
