#include "asan_impl.h"
#include "futex_impl.h"
#include "libc.h"
#include "pthread_impl.h"
#include "magenta_impl.h"
#include "stdio_impl.h"

#include <magenta/process.h>
#include <magenta/syscalls.h>
#include <pthread.h>
#include <runtime/thread.h>
#include <runtime/tls.h>
#include <stdatomic.h>
#include <stddef.h>
#include <string.h>
#include <sys/mman.h>

static inline pthread_t prestart(void* arg) {
    pthread_t self = arg;
    mxr_tp_set(mxr_thread_get_handle(&self->mxr_thread), pthread_to_tp(self));
    __sanitizer_thread_start_hook(self->sanitizer_hook, (thrd_t)self);
    return self;
}

static void start_pthread(void* arg) {
    pthread_t self = prestart(arg);
    __pthread_exit(self->start(self->start_arg));
}

static void start_c11(void* arg) {
    pthread_t self = prestart(arg);
    int (*start)(void*) = (int (*)(void*))self->start;
    __pthread_exit((void*)(intptr_t)start(self->start_arg));
}

static void deallocate_region(const struct iovec* region) {
    _mx_vmar_unmap(_mx_vmar_root_self(),
                   (uintptr_t)region->iov_base, region->iov_len);
}

int __pthread_create(pthread_t* restrict res, const pthread_attr_t* restrict attrp,
                     void* (*entry)(void*), void* restrict arg) {
    pthread_attr_t attr = attrp == NULL ? DEFAULT_PTHREAD_ATTR : *attrp;

    // We do not support providing a stack via pthread attributes.
    if (attr._a_stackaddr != NULL)
        return ENOTSUP;

    char thread_name[MX_MAX_NAME_LEN];
    pthread_t new = __allocate_thread(&attr,
                                      attr.__name != NULL ? attr.__name :
                                      attr.__c11 ? "thrd_t" : "pthread_t",
                                      thread_name);
    if (new == NULL)
        return EAGAIN;

    const char* name = attr.__name != NULL ? attr.__name : thread_name;
    mx_status_t status =
        mxr_thread_create(_mx_process_self(), name, attr._a_detach,
                          &new->mxr_thread);
    if (status != MX_OK)
        goto fail_after_alloc;

    mxr_thread_entry_t start = attr.__c11 ? start_c11 : start_pthread;

    new->start = entry;
    new->start_arg = arg;

    void* sanitizer_hook = __sanitizer_before_thread_create_hook(
        (thrd_t)new, attr._a_detach, name,
        new->safe_stack.iov_base, new->safe_stack.iov_len);
    new->sanitizer_hook = sanitizer_hook;

    // We have to publish the pointer now, and make sure it is
    // visible, as in C11 the end of thrd_create synchronizes with the
    // entry point of the new thread.
    *res = new;
    atomic_thread_fence(memory_order_release);

    atomic_fetch_add(&libc.thread_count, 1);

    // This will (hopefully) start the new thread. It could instantly
    // run to completion and deallocate it self. As such, we can't
    // access new->anything after this point.
    status = mxr_thread_start(&new->mxr_thread,
                              (uintptr_t)new->safe_stack.iov_base,
                              new->safe_stack.iov_len, start, new);

    if (status == MX_OK) {
        __sanitizer_thread_create_hook(sanitizer_hook,
                                       (thrd_t)new, thrd_success);
        return 0;
    }

    *res = NULL;
    atomic_fetch_sub(&libc.thread_count, 1);

    __sanitizer_thread_create_hook(
        sanitizer_hook, (thrd_t)new,
        status == MX_ERR_ACCESS_DENIED ? thrd_error : thrd_nomem);

fail_after_alloc:
    deallocate_region(&new->safe_stack_region);
    deallocate_region(&new->unsafe_stack_region);
    deallocate_region(&new->tcb_region);
    return status == MX_ERR_ACCESS_DENIED ? EPERM : EAGAIN;
}

static _Noreturn void final_exit(pthread_t self)
    __asm__("final_exit") __attribute__((used));

static __NO_SAFESTACK NO_ASAN void final_exit(pthread_t self) {
    deallocate_region(&self->safe_stack_region);
    deallocate_region(&self->unsafe_stack_region);

    // This deallocates the TCB region too for the detached case.
    // If not detached, pthread_join will deallocate it.
    mxr_thread_exit_unmap_if_detached(&self->mxr_thread, _mx_vmar_root_self(),
                                      (uintptr_t)self->tcb_region.iov_base,
                                      self->tcb_region.iov_len);
}

static NO_ASAN _Noreturn void finish_exit(pthread_t self) {
    __sanitizer_thread_exit_hook(self->sanitizer_hook, (thrd_t)self);

    /* It's impossible to determine whether this is "the last thread"
     * until performing the atomic decrement, since multiple threads
     * could exit at the same time. For the last thread, revert the
     * decrement and unblock signals to give the atexit handlers and
     * stdio cleanup code a consistent state. */
    if (atomic_fetch_sub(&libc.thread_count, 1) == -1) {
        atomic_store(&libc.thread_count, 0);
        exit(0);
    }

    // Switch off the thread's normal stack so it can be freed.  The TCB
    // region stays alive so the pthread_t is still valid for pthread_join.
    // The rest of the region is no longer used for TLS, so it can serve
    // as the small amount of temporary stack needed for the exit calls.

#ifdef __x86_64__
    // The thread descriptor is at the end of the region, so the space
    // before it is available as the temporary stack.
    // The x86-64 ABI requires %rsp % 16 = 8 on entry.
    __asm__("mov %[self], %%rsp\n"
            "and $-16, %%rsp\n"
            "call final_exit\n"
            "# Target receives %[self]" : :
            [self]"D"(self));
#elif defined(__aarch64__)
    // The thread descriptor is at the start of the region, so the rest of
    // the space up to the guard page is available as the temporary stack.
    __asm__("add sp, %[base], %[len]\n"
            "mov x0, %[self]\n"
            "bl final_exit" : :
            [base]"r"(self->tcb_region.iov_base),
            [len]"r"(self->tcb_region.iov_len - PAGE_SIZE),
            [self]"r"(self));
#else
#error what architecture?
#endif
    __builtin_unreachable();
}

_Noreturn void __pthread_exit(void* result) {
    pthread_t self = __pthread_self();

    self->result = result;

    __tls_run_dtors();

    __pthread_tsd_run_dtors();

    __dl_thread_cleanup();

    // After this point the sanitizer runtime will tear down its state,
    // so we cannot run any more sanitized code.
    finish_exit(self);
}

weak_alias(__pthread_create, pthread_create);
weak_alias(__pthread_exit, pthread_exit);
