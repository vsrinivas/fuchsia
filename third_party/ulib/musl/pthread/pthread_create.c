#define _GNU_SOURCE

#include "futex_impl.h"
#include "pthread_impl.h"

#include <magenta/syscalls.h>
#include <pthread.h>
#include <runtime/tls.h>
#include <stddef.h>
#include <sys/mman.h>

#define ROUND(x) (((x) + PAGE_SIZE - 1) & -PAGE_SIZE)

/* pthread_key_create.c overrides this */
static volatile size_t dummy = 0;
weak_alias(dummy, __pthread_tsd_size);

static void dummy_0(void) {}
weak_alias(dummy_0, __acquire_ptc);
weak_alias(dummy_0, __dl_thread_cleanup);
weak_alias(dummy_0, __do_orphaned_stdio_locks);
weak_alias(dummy_0, __pthread_tsd_run_dtors);
weak_alias(dummy_0, __release_ptc);

void* __mmap(void*, size_t, int, int, int, off_t);
int __munmap(void*, size_t);

static int thread_entry(void* arg) {
    struct __mx_thread_info* ei = arg;
    if (ei->tls) {
        mxr_tls_root_set(ei->tls);
        mxr_tls_set(MXR_TLS_SLOT_ERRNO, &ei->errno_value);
    }
    ei->func(ei->arg);
    mx_thread_exit();
    return 0;
}

int pthread_create(pthread_t* restrict res, const pthread_attr_t* restrict attrp,
                   void* (*entry)(void*), void* restrict arg) {
    mx_handle_t handle;

    mx_tls_root_t* self_tls = mxr_tls_root_get();
    size_t len;
    mx_proc_info_t* proc = NULL;
    if (self_tls) {
        proc = self_tls->proc;
        len = ROUND(sizeof(struct pthread) + sizeof(mx_tls_root_t) + (sizeof(void*) * (MX_TLS_MIN_SLOTS - 1)) + __pthread_tsd_size);
    } else {
        len = ROUND(sizeof(struct pthread));
    }

    void* map = __mmap(NULL, len, PROT_READ | PROT_WRITE,
                       MAP_PRIVATE | MAP_ANON, -1, 0);
    if (map == MAP_FAILED)
        return ERR_NO_MEMORY;
    pthread_t thread = map;
    thread->map_base = map;
    thread->mx_thread_info.func = entry;
    thread->mx_thread_info.arg = arg;
    thread->map_size = len;

    if (self_tls) {
        mx_tls_root_t* tls = map + sizeof(*thread);
        tls->magic = MX_TLS_ROOT_MAGIC;
        tls->flags = 0;
        tls->maxslots = MX_TLS_MIN_SLOTS;
        tls->proc = proc;
        tls->self = tls;

        tls->slots[__pthread_key] = thread;
        thread->mx_thread_info.tls = tls;
        thread->tsd = map + len - __pthread_tsd_size;
    }

    handle = mx_thread_create(thread_entry, &thread->mx_thread_info,
                              "musl", 5);
    if (handle < 0) {
        __munmap(map, len);
        return handle;
    } else {
        thread->self = thread;
        thread->handle = handle;
        *res = thread;
        return 0;
    }
}

_Noreturn void pthread_exit(void* result) {
    pthread_t self = __pthread_self();
    sigset_t set;

    self->canceldisable = 1;
    self->cancelasync = 0;
    self->result = result;

    while (self->cancelbuf) {
        void (*f)(void*) = self->cancelbuf->__f;
        void* x = self->cancelbuf->__x;
        self->cancelbuf = self->cancelbuf->__next;
        f(x);
    }

    __pthread_tsd_run_dtors();

    mxr_mutex_lock(&self->exitlock);

    /* Mark this thread dead before decrementing count */
    mxr_mutex_lock(&self->killlock);
    self->dead = 1;

    /* Block all signals before decrementing the live thread count.
     * This is important to ensure that dynamically allocated TLS
     * is not under-allocated/over-committed, and possibly for other
     * reasons as well. */
    __block_all_sigs(&set);

    /* Wait to unlock the kill lock, which governs functions like
     * pthread_kill which target a thread id, until signals have
     * been blocked. This precludes observation of the thread id
     * as a live thread (with application code running in it) after
     * the thread was reported dead by ESRCH being returned. */
    mxr_mutex_unlock(&self->killlock);

    /* TODO(kulakowski) Thread exit process teardown. */
    /* It's impossible to determine whether this is "the last thread"
     * until performing the atomic decrement, since multiple threads
     * could exit at the same time. For the last thread, revert the
     * decrement and unblock signals to give the atexit handlers and
     * stdio cleanup code a consistent state. */
    /* if (a_fetch_add(&libc.threads_minus_1, -1) == 0) { */
    /*     libc.threads_minus_1 = 0; */
    /*     __restore_sigs(&set); */
    /*     exit(0); */
    /* } */

    /* Process robust list in userspace to handle non-pshared mutexes
     * and the detached thread case where the robust list head will
     * be invalid when the kernel would process it. */
    __vm_lock();
    volatile void* volatile* rp;
    while ((rp = self->robust_list.head) && rp != &self->robust_list.head) {
        pthread_mutex_t* m = (void*)((char*)rp - offsetof(pthread_mutex_t, _m_next));
        int waiters = m->_m_waiters;
        self->robust_list.pending = rp;
        self->robust_list.head = *rp;
        int cont = a_swap(&m->_m_lock, self->tid | 0x40000000);
        self->robust_list.pending = 0;
        if (cont < 0 || waiters)
            __wake(&m->_m_lock, 1);
    }
    __vm_unlock();

    __do_orphaned_stdio_locks();
    __dl_thread_cleanup();

    if (self->detached && self->map_base) {
        /* Detached threads must avoid the kernel clear_child_tid
         * feature, since the virtual address will have been
         * unmapped and possibly already reused by a new mapping
         * at the time the kernel would perform the write. In
         * the case of threads that started out detached, the
         * initial clone flags are correct, but if the thread was
         * detached later (== 2), we need to clear it here. */
        if (self->detached == 2)
            __syscall(SYS_set_tid_address, 0);

        /* Robust list will no longer be valid, and was already
         * processed above, so unregister it with the kernel. */
        if (self->robust_list.off)
            __syscall(SYS_set_robust_list, 0, 3 * sizeof(long));

        /* Since __unmapself bypasses the normal munmap code path,
         * explicitly wait for vmlock holders first. */
        __vm_wait();

        /* The following call unmaps the thread's stack mapping
         * and then exits without touching the stack. */
        __unmapself(self->map_base, self->map_size);
    }

    for (;;)
        __syscall(SYS_exit, 0);
}

int __mprotect(void*, size_t, int);

void __do_cleanup_push(struct __ptcb* cb) {
    struct pthread* self = __pthread_self();
    cb->__next = self->cancelbuf;
    self->cancelbuf = cb;
}

void __do_cleanup_pop(struct __ptcb* cb) {
    __pthread_self()->cancelbuf = cb->__next;
}

static int start_c11(void* p) {
    pthread_t self = p;
    int (*start)(void*) = (int (*)(void*))self->start;
    pthread_exit((void*)(uintptr_t)start(self->start_arg));
    return 0;
}

/* pthread_key_create.c overrides this */
static void* dummy_tsd[1] = {0};
weak_alias(dummy_tsd, __pthread_tsd_main);

static FILE* volatile dummy_file = 0;
weak_alias(dummy_file, __stdin_used);
weak_alias(dummy_file, __stdout_used);
weak_alias(dummy_file, __stderr_used);
