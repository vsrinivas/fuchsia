#include <magenta/threads.h>

#include <runtime/thread.h>

#include "pthread_impl.h"

mx_handle_t thrd_get_mx_handle(thrd_t t) {
    return mxr_thread_get_handle(t->mxr_thread);
}
