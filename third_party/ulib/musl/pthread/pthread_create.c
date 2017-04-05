#define _GNU_SOURCE

#include "futex_impl.h"
#include "libc.h"
#include "pthread_impl.h"
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

static void start_pthread(void* arg) {
    pthread_t self = arg;
    // TODO(kulakowski) Signals?
    // if (self->startlock[0]) {
    //     __wait(self->startlock, 0, 1, 1);
    //     if (self->startlock[0]) {
    //         self->detached = 2;
    //         pthread_exit(0);
    //     }
    //     __restore_sigs(self->sigmask);
    // }
    mxr_tp_set(mxr_thread_get_handle(&self->mxr_thread), pthread_to_tp(self));
    pthread_exit(self->start(self->start_arg));
}

static void start_c11(void* arg) {
    pthread_t self = arg;
    mxr_tp_set(mxr_thread_get_handle(&self->mxr_thread), pthread_to_tp(self));
    int (*start)(void*) = (int (*)(void*))self->start;
    pthread_exit((void*)(intptr_t)start(self->start_arg));
}

static void deallocate_region(const struct iovec* region) {
    _mx_vmar_unmap(_mx_vmar_root_self(),
                   (uintptr_t)region->iov_base, region->iov_len);
}

int pthread_create(pthread_t* restrict res, const pthread_attr_t* restrict attrp, void* (*entry)(void*), void* restrict arg) {
    pthread_attr_t attr = attrp == NULL ? DEFAULT_PTHREAD_ATTR : *attrp;

    // We do not support providing a stack via pthread attributes.
    if (attr._a_stackaddr != NULL)
        return ENOTSUP;

    __acquire_ptc();

    pthread_t new = __allocate_thread(&attr);
    if (new == NULL) {
        __release_ptc();
        return EAGAIN;
    }

    const char* name = attr.__name ? attr.__name : "";
    mx_status_t status =
        mxr_thread_create(_mx_process_self(), name, attr._a_detach,
                          &new->mxr_thread);
    if (status != NO_ERROR)
        goto fail_after_alloc;

    mxr_thread_entry_t start = attr.__c11 ? start_c11 : start_pthread;

    new->start = entry;
    new->start_arg = arg;

    // TODO(kulakowski) Signals?
    // int do_sched = 0;
    // if (attr._a_sched) {
    //     do_sched = new->startlock[0] = 1;
    //     __block_app_sigs(new->sigmask);
    // }

    atomic_fetch_add(&libc.thread_count, 1);
    status = mxr_thread_start(&new->mxr_thread,
                              (uintptr_t)new->safe_stack.iov_base,
                              new->safe_stack.iov_len, start, new);

    __release_ptc();

    // TODO(kulakowski) Signals?
    // if (do_sched) {
    //     __restore_sigs(new->sigmask);
    // }

    // TODO(kulakowski)
    // if (do_sched) {
    //     ret = __syscall(SYS_sched_setscheduler, new->tid,
    //                     attr._a_policy, &attr._a_prio);
    //     atomic_store(new->startlock, ret < 0 ? 2 : 0);
    //     __wake(new->startlock, 1, 1);
    //     if (ret < 0)
    //         return -ret;
    // }

    if (status == NO_ERROR) {
        *res = new;
        return 0;
    }

    atomic_fetch_sub(&libc.thread_count, 1);
fail_after_alloc:
    deallocate_region(&new->safe_stack_region);
    deallocate_region(&new->unsafe_stack_region);
    deallocate_region(&new->tcb_region);
    return status == ERR_ACCESS_DENIED ? EPERM : EAGAIN;
}

static _Noreturn void final_exit(pthread_t self)
    __asm__("final_exit") __attribute__((used));

static __NO_SAFESTACK void final_exit(pthread_t self) {
    deallocate_region(&self->safe_stack_region);
    deallocate_region(&self->unsafe_stack_region);

    // This deallocates the TCB region too for the detached case.
    // If not detached, pthread_join will deallocate it.
    mxr_thread_exit_unmap_if_detached(&self->mxr_thread, _mx_vmar_root_self(),
                                      (uintptr_t)self->tcb_region.iov_base,
                                      self->tcb_region.iov_len);
}

_Noreturn void pthread_exit(void* result) {
    pthread_t self = __pthread_self();
    // TODO(kulakowski) Signals?
    // sigset_t set;

    self->result = result;

    __pthread_tsd_run_dtors();

    /* Block all signals before decrementing the live thread count.
     * This is important to ensure that dynamically allocated TLS
     * is not under-allocated/over-committed, and possibly for other
     * reasons as well. */
    // TODO(kulakowski) Signals?
    // __block_all_sigs(&set);

    /* It's impossible to determine whether this is "the last thread"
     * until performing the atomic decrement, since multiple threads
     * could exit at the same time. For the last thread, revert the
     * decrement and unblock signals to give the atexit handlers and
     * stdio cleanup code a consistent state. */
    if (atomic_fetch_sub(&libc.thread_count, 1) == -1) {
        atomic_store(&libc.thread_count, 0);
        // TODO(kulakowski) Signals.
        // __restore_sigs(&set);
        exit(0);
    }

    /* TODO(kulakowski): Pthread robust mutex processing used to occur
     * inside this vm lock/unlock pair. I don't if there is also
     * implicitly a need to synchronize on this lock in this function
     * in any case, so I'm leaving the lock/unlock pair.
     */
    __vm_lock();
    __vm_unlock();

    __dl_thread_cleanup();

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
