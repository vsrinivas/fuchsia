#include <zircon/process.h>
#include <zircon/threads.h>

#include <runtime/thread.h>

#include "threads_impl.h"

zx_handle_t thrd_get_zx_handle(thrd_t t) {
    return zxr_thread_get_handle(&t->zxr_thread);
}

zx_handle_t _zx_thread_self(void) {
    return zxr_thread_get_handle(&__pthread_self()->zxr_thread);
}
__typeof(zx_thread_self) zx_thread_self
    __attribute__((weak, alias("_zx_thread_self")));
