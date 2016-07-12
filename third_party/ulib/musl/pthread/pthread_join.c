#include <pthread.h>

#include "pthread_impl.h"

#include <magenta/syscalls.h>

int __munmap(void*, size_t);

int pthread_join(pthread_t t, void** res) {
    struct pthread* thread = (struct pthread*)t;
    mx_status_t r = mx_handle_wait_one(thread->handle,
                                             MX_SIGNAL_SIGNALED, MX_TIME_INFINITE,
                                             NULL, NULL);
    if (r != 0)
        return -1;

    __munmap(thread, thread->map_size);

    return 0;
}
