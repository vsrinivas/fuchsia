#define _GNU_SOURCE

#include "futex_impl.h"
#include "libc.h"
#include "pthread_impl.h"
#include "stdio_impl.h"

#include <magenta/syscalls.h>
#include <pthread.h>
#include <runtime/thread.h>
#include <runtime/tls.h>
#include <stdatomic.h>
#include <stddef.h>
#include <string.h>
#include <sys/mman.h>

/* pthread_key_create.c overrides this */
static volatile size_t dummy = 0;
weak_alias(dummy, __pthread_tsd_size);
static void* dummy_tsd[1] = {0};
weak_alias(dummy_tsd, __pthread_tsd_main);

static void dummy_0(void) {}
weak_alias(dummy_0, __acquire_ptc);
weak_alias(dummy_0, __dl_thread_cleanup);
weak_alias(dummy_0, __do_orphaned_stdio_locks);
weak_alias(dummy_0, __pthread_tsd_run_dtors);
weak_alias(dummy_0, __release_ptc);

#define ROUND(x) (((x) + PAGE_SIZE - 1) & -PAGE_SIZE)

void* __mmap(void*, size_t, int, int, int, off_t);
int __munmap(void*, size_t);
int __mprotect(void*, size_t, int);

void* __copy_tls(unsigned char*);

static intptr_t start_pthread(void* arg) {
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
    // if (self->unblock_cancel)
    //     __syscall(SYS_rt_sigprocmask, SIG_UNBLOCK,
    //               SIGPT_SET, 0, _NSIG / 8);
    mxr_tp_set(pthread_to_tp(self));
    pthread_exit(self->start(self->start_arg));
    return 0;
}

static intptr_t start_c11(void* arg) {
    pthread_t self = arg;
    mxr_tp_set(pthread_to_tp(self));
    int (*start)(void*) = (int (*)(void*))self->start;
    pthread_exit((void*)(intptr_t)start(self->start_arg));
}

int pthread_create(pthread_t* restrict res, const pthread_attr_t* restrict attrp, void* (*entry)(void*), void* restrict arg) {
    pthread_attr_t attr = {0};
    if (attrp)
        attr = *attrp;
    size_t size = 0u;
    size_t guard = 0u;
    unsigned char *map = 0, *stack = 0, *tsd = 0, *stack_limit;

    __acquire_ptc();

    if (attr._a_stackaddr) {
        size_t need = libc.tls_size + __pthread_tsd_size;
        size = attr._a_stacksize + DEFAULT_STACK_SIZE;
        stack = (void*)(attr._a_stackaddr & -16);
        stack_limit = (void*)(attr._a_stackaddr - size);
        /* Use application-provided stack for TLS only when it does
         * not take more than ~12% or 2k of the application's stack
         * space. */
        if (need < size / 8 && need < 2048) {
            tsd = stack - __pthread_tsd_size;
            stack = tsd - libc.tls_size;
            memset(stack, 0, need);
        } else {
            size = ROUND(need);
            guard = 0;
        }
    } else {
        guard = ROUND(DEFAULT_GUARD_SIZE + attr._a_guardsize);
        size = guard + ROUND(DEFAULT_STACK_SIZE + attr._a_stacksize + libc.tls_size + __pthread_tsd_size);
    }

    // TODO(kulakowski) Proper stack allocation and guard pages as in
    // the first branch of the #if below. For now, bypass all the
    // guard page arithmetic and just map the entire size. When we can
    // break up mapped regions and have PROT_NONE, the guard stuff is
    // easy to reintroduce.
#if 0
    if (!tsd) {
        if (guard) {
            map = __mmap(0, size, PROT_NONE, MAP_PRIVATE | MAP_ANON, -1, 0);
            if (map == MAP_FAILED)
                goto fail;
            if (__mprotect(map + guard, size - guard, PROT_READ | PROT_WRITE) && errno != ENOSYS) {
                __munmap(map, size);
                goto fail;
            }
        } else {
            map = __mmap(0, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
            if (map == MAP_FAILED)
                goto fail;
        }
        tsd = map + size - __pthread_tsd_size;
        if (!stack) {
            stack = tsd - libc.tls_size;
            stack_limit = map + guard;
        }
    }
#else
    if (!tsd) {
        map = __mmap(0, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
        if (map == MAP_FAILED)
            goto fail;
        tsd = map + size - __pthread_tsd_size;
    }
#endif

    const char* name = attr.__name ? attr.__name : "";
    mxr_thread_entry_t start = attr.__c11 ? start_c11 : start_pthread;
    struct pthread* self = __pthread_self();

    struct pthread* new = __copy_tls(tsd - libc.tls_size);
    new->map_base = map;
    new->map_size = size;
    // TODO(kulakowski) Stack size etc.
    // new->stack = stack;
    // new->stack_size = stack - stack_limit;
    new->start = entry;
    new->start_arg = arg;
    new->self = new;
    new->tsd = (void*)tsd;
    new->locale = &libc.global_locale;
    // TODO (kulakowski) Actually detach via attribute
    if (attr._a_detach) {
        new->detached = 1;
    }
    // TODO(kulakowski) Signals?
    // int do_sched = 0;
    // if (attr._a_sched) {
    //     do_sched = new->startlock[0] = 1;
    //     __block_app_sigs(new->sigmask);
    // }
    new->unblock_cancel = self->cancel;
    new->CANARY = self->CANARY;

    atomic_fetch_add(&libc.thread_count, 1);
    // TODO(kulakowski) Separate stack creation here.
    mx_status_t status = mxr_thread_create(start, new, name, &new->mxr_thread);

    __release_ptc();

    // TODO(kulakowski) Signals?
    // if (do_sched) {
    //     __restore_sigs(new->sigmask);
    // }

    if (status != NO_ERROR) {
        atomic_fetch_sub(&libc.thread_count, 1);
        if (map)
            __munmap(map, size);
        return status == ERR_ACCESS_DENIED ? EPERM : EAGAIN;
    }

    // TODO(kulakowski)
    // if (do_sched) {
    //     ret = __syscall(SYS_sched_setscheduler, new->tid,
    //                     attr._a_policy, &attr._a_prio);
    //     a_store(new->startlock, ret < 0 ? 2 : 0);
    //     __wake(new->startlock, 1, 1);
    //     if (ret < 0)
    //         return -ret;
    // }

    *res = new;
    return 0;
fail:
    __release_ptc();
    return EAGAIN;
}

_Noreturn void pthread_exit(void* result) {
    pthread_t self = __pthread_self();
    mxr_thread_t* mxr_thread = self->mxr_thread;
    // TODO(kulakowski) Signals?
    // sigset_t set;

    self->canceldisable = 1;
    self->cancelasync = 0;
    self->result = result;

    // TODO(kulakowski) pthread_cancel?
    // while (self->cancelbuf) {
    //     void (*f)(void*) = self->cancelbuf->__f;
    //     void* x = self->cancelbuf->__x;
    //     self->cancelbuf = self->cancelbuf->__next;
    //     f(x);
    // }

    __pthread_tsd_run_dtors();

    mtx_lock(&self->exitlock);

    /* Mark this thread dead before decrementing count */
    mtx_lock(&self->killlock);
    self->dead = 1;

    /* Block all signals before decrementing the live thread count.
     * This is important to ensure that dynamically allocated TLS
     * is not under-allocated/over-committed, and possibly for other
     * reasons as well. */
    // TODO(kulakowski) Signals?
    // __block_all_sigs(&set);

    /* Wait to unlock the kill lock, which governs functions like
     * pthread_kill which target a thread id, until signals have
     * been blocked. This precludes observation of the thread id
     * as a live thread (with application code running in it) after
     * the thread was reported dead by ESRCH being returned. */
    mtx_unlock(&self->killlock);

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

    __do_orphaned_stdio_locks();
    __dl_thread_cleanup();

    mxr_thread_exit(mxr_thread, 0);
}

void __do_cleanup_push(struct __ptcb* cb) {
    struct pthread* self = __pthread_self();
    cb->__next = self->cancelbuf;
    self->cancelbuf = cb;
}

void __do_cleanup_pop(struct __ptcb* cb) {
    __pthread_self()->cancelbuf = cb->__next;
}
