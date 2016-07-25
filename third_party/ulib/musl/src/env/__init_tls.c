#include "atomic.h"
#include "libc.h"
#include "pthread_impl.h"
#include <limits.h>
#include <runtime/tls.h>
#include <stddef.h>
#include <sys/mman.h>

#define ROUND(x) (((x) + PAGE_SIZE - 1) & -PAGE_SIZE)

void* __mmap(void*, size_t, int, int, int, off_t);

int __init_tp(pthread_t thread) {
    thread->self = thread;
    //TODO: Get and set thread ID
    //thread->tid = __syscall(SYS_set_tid_address, &thread->tid);
    thread->locale = &libc.global_locale;
    return 0;
}

static void* __mmap_wrapper(size_t len) {
    void* p = __mmap(NULL, len, PROT_WRITE | PROT_READ,
                     MAP_PRIVATE | MAP_ANON, -1, 0);
    if (p == MAP_FAILED)
        a_crash();
    return p;
}

mxr_tls_t __pthread_key;

static void static_init_tls(void) {
    __pthread_key = mxr_tls_allocate();
    size_t len = ROUND(sizeof(struct pthread));
    pthread_t thread = __mmap_wrapper(len);
    mxr_tls_set(__pthread_key, thread);
    if (__init_tp(thread))
        a_crash();
}
weak_alias(static_init_tls, __init_tls);
