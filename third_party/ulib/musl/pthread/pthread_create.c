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
    // if (self->unblock_cancel)
    //     __syscall(SYS_rt_sigprocmask, SIG_UNBLOCK,
    //               SIGPT_SET, 0, _NSIG / 8);
    mxr_tp_set(pthread_to_tp(self));
    pthread_exit(self->start(self->start_arg));
}

static void start_c11(void* arg) {
    pthread_t self = arg;
    mxr_tp_set(pthread_to_tp(self));
    int (*start)(void*) = (int (*)(void*))self->start;
    pthread_exit((void*)(intptr_t)start(self->start_arg));
}

// Allocate stack_size via a vmo, and place the pointer to it in *stack_out.
static mx_status_t allocate_stack(size_t stack_size, size_t guard_size, uintptr_t* stack_out) {
    // TODO(kulakowski) Implement guard pages. For now, bypass all the
    // guard page arithmetic and just map the entire size. When we can
    // break up mapped regions and have PROT_NONE, the guard stuff is
    // easy to reintroduce.

    mx_handle_t thread_stack_vmo = _mx_vmo_create(stack_size);
    if (thread_stack_vmo < 0)
        return thread_stack_vmo;

    mx_status_t status = _mx_process_map_vm(
        libc.proc, thread_stack_vmo, 0, stack_size, stack_out,
        MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_WRITE);
    _mx_handle_close(thread_stack_vmo);

    return status;
}

int pthread_create(pthread_t* restrict res, const pthread_attr_t* restrict attrp, void* (*entry)(void*), void* restrict arg) {
    pthread_attr_t attr = {0};
    if (attrp)
        attr = *attrp;

    const char* name = attr.__name ? attr.__name : "";
    mxr_thread_t* mxr_thread = NULL;
    mx_status_t status = mxr_thread_create(name, &mxr_thread);
    if (status < 0)
        return EAGAIN;

    __acquire_ptc();

    size_t size = 0u;
    size_t guard_size = 0u;
    unsigned char *map = 0, *stack = 0, *tsd = 0, *stack_limit;

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
        }
    } else {
        guard_size = ROUND(DEFAULT_GUARD_SIZE + attr._a_guardsize);
        size = guard_size + ROUND(DEFAULT_STACK_SIZE + attr._a_stacksize + libc.tls_size + __pthread_tsd_size);
    }

    // At this point:
    // If the attributes provided a stack, we know to use it.
    //   - This also accounts for the guard page.
    //   - If that stack is sufficiently large, also use that for the tsd.
    //   - Thus:
    //     - stack size is known
    //     - stack is known
    //     - guard_size is known
    //     - tsd may or may not be known
    // Otherwise:
    //   - We use the default guard and default or provided stack size
    //   - Thus:
    //     - stack size is known
    //     - stack is unknown
    //     - guard_size is unknown
    //     - tsd is unknown

    // So unless we have already allocated the stack _and_ tsd in the
    // first case, we are going to allocate here, and possibly set the
    // stack pointer.

    if (!tsd) {
        uintptr_t addr = 0u;
        status = allocate_stack(size, guard_size, &addr);
        if (status < 0) {
            __release_ptc();
            mxr_thread_destroy(mxr_thread);
            return EAGAIN;
        }
        map = (void*)addr;
        tsd = map + size - __pthread_tsd_size;
        if (!stack) {
            stack = tsd - libc.tls_size;
            stack_limit = map + guard_size;
        }
    }

    mxr_thread_entry_t start = attr.__c11 ? start_c11 : start_pthread;
    struct pthread* self = __pthread_self();

    struct pthread* new = __copy_tls(tsd - libc.tls_size);
    new->map_base = map;
    new->map_size = size;
    new->stack = stack;
    new->stack_size = stack - stack_limit;
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
    new->mxr_thread = mxr_thread;

    atomic_fetch_add(&libc.thread_count, 1);
    status = mxr_thread_start(mxr_thread, (uintptr_t)stack_limit, new->stack_size, start, new);

    __release_ptc();

    // TODO(kulakowski) Signals?
    // if (do_sched) {
    //     __restore_sigs(new->sigmask);
    // }

    if (status != NO_ERROR) {
        atomic_fetch_sub(&libc.thread_count, 1);
        if (map)
            __munmap(map, size);
        mxr_thread_destroy(mxr_thread);
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

    mxr_thread_exit(mxr_thread);
}

void __do_cleanup_push(struct __ptcb* cb) {
    struct pthread* self = __pthread_self();
    cb->__next = self->cancelbuf;
    self->cancelbuf = cb;
}

void __do_cleanup_pop(struct __ptcb* cb) {
    __pthread_self()->cancelbuf = cb->__next;
}
