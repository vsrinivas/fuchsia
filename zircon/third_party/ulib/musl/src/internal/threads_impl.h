#pragma once

#include "atomic.h"
#include "libc.h"
#include "pthread_arch.h"
#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <locale.h>
#include <pthread.h>
#include <signal.h>
#include <sys/uio.h>
#include <threads.h>

#include <runtime/thread.h>
#include <runtime/tls.h>
#include <zircon/assert.h>
#include <zircon/tls.h>

#define pthread __pthread

// This is what the thread pointer points to directly.  On TLS_ABOVE_TP
// machines, the size of this is part of the ABI known to the compiler
// and linker.
typedef struct {
    // The position of this pointer is part of the ABI on x86.
    // It has the same value as the thread pointer itself.
    uintptr_t tp;
    void** dtv;
} tcbhead_t;

// The locations of these fields is part of the ABI known to the compiler.
typedef struct {
    uintptr_t stack_guard;
    uintptr_t unsafe_sp;
} tp_abi_t;

struct tls_dtor;

struct pthread {
#ifndef TLS_ABOVE_TP
    // These must be the very first members.
    tcbhead_t head;
    tp_abi_t abi;
#endif

    zxr_thread_t zxr_thread;

    // The *_region fields describe whole memory regions reserved,
    // including guard pages (for deallocation).  safe_stack and
    // unsafe_stack describe just the actual stack block between the
    // guards.
    struct iovec tcb_region;
    struct iovec safe_stack, safe_stack_region;
    struct iovec unsafe_stack, unsafe_stack_region;

    struct tls_dtor* tls_dtors;
    void* tsd[PTHREAD_KEYS_MAX];
    int tsd_used;
    int errno_value;

    void* sanitizer_hook;
    void* start_arg;
    void* (*start)(void*);
    void* result;
    locale_t locale;
    char* dlerror_buf;
    int dlerror_flag;

#ifdef TLS_ABOVE_TP
    // These must be the very last members.
    tp_abi_t abi;
    tcbhead_t head;
#endif
};

#ifdef TLS_ABOVE_TP
#define PTHREAD_TP_OFFSET offsetof(struct pthread, head)
#else
#define PTHREAD_TP_OFFSET 0
#endif

#define TP_OFFSETOF(field) \
    ((ptrdiff_t)offsetof(struct pthread, field) - PTHREAD_TP_OFFSET)

static_assert(TP_OFFSETOF(head) == 0,
              "ABI tcbhead_t misplaced in struct pthread");

#ifdef ABI_TCBHEAD_SIZE
static_assert((sizeof(struct pthread) -
               offsetof(struct pthread, head)) == ABI_TCBHEAD_SIZE,
              "ABI tcbhead_t misplaced in struct pthread");
#endif

#if defined(__x86_64__) || defined(__aarch64__)
// The tlsdesc.s assembly code assumes this, though it's not part of the ABI.
static_assert(TP_OFFSETOF(head.dtv) == 8, "dtv misplaced in struct pthread");
#endif

static_assert(TP_OFFSETOF(abi.stack_guard) == ZX_TLS_STACK_GUARD_OFFSET,
              "stack_guard not at ABI-mandated offset from thread pointer");
static_assert(TP_OFFSETOF(abi.unsafe_sp) == ZX_TLS_UNSAFE_SP_OFFSET,
              "unsafe_sp not at ABI-mandated offset from thread pointer");

static inline void* pthread_to_tp(struct pthread* thread) {
    return (void*)((char*)thread + PTHREAD_TP_OFFSET);
}

static inline struct pthread* tp_to_pthread(void* tp) {
    return (struct pthread*)((char*)tp - PTHREAD_TP_OFFSET);
}

#define SIGALL_SET ((sigset_t*)(const unsigned long long[2]){-1, -1})

#define PTHREAD_MUTEX_MASK (PTHREAD_MUTEX_RECURSIVE | PTHREAD_MUTEX_ERRORCHECK)

// Contested state tracking bits.  Note; all users are required to use the
// static inline functions for manipulating and checking state.  This
// centralizes the operations and makes it easier to adapt code if/when the
// reserve handle bit(s) change.
//
// Note; currently valid handles are always expected to have the contested bit
// *set*.  A uncontested-and-owned mutex state is turned into a
// contested-and-owned mutex state by clearing the contested bit, not setting
// it.

#define _PTHREAD_MUTEX_CONTESTED_BIT ((int)0x00000001)
#define _PTHREAD_MUTEX_CONTESTED_MASK ((int)(~_PTHREAD_MUTEX_CONTESTED_BIT))

static inline int pthread_mutex_tid_to_uncontested_state(pid_t h) {
    // We rely on the fact that the reserved must-be-one bits are always set.
    // For now, let's incur the cost of this sanity check, but consider relaxing
    // it so that it is only performed in debug builds.
    if ((h & ZX_HANDLE_FIXED_BITS_MASK) != ZX_HANDLE_FIXED_BITS_MASK) {
        __builtin_trap();
    }
    return ((int)h);
}

static inline int pthread_mutex_tid_to_contested_state(pid_t h) {
    return ((int)(h & _PTHREAD_MUTEX_CONTESTED_MASK));
}

static inline int pthread_mutex_uncontested_to_contested_state(int state) {
    return (state & _PTHREAD_MUTEX_CONTESTED_MASK);
}

static inline pid_t pthread_mutex_state_to_tid(int state) {
    return ((pid_t)(state | _PTHREAD_MUTEX_CONTESTED_BIT));
}

static inline bool pthread_mutex_is_state_contested(int state) {
    return ((state & _PTHREAD_MUTEX_CONTESTED_BIT) == 0);
}

#undef _PTHREAD_MUTEX_CONTESTED_BIT
#undef _PTHREAD_MUTEX_CONTESTED_MASK

// Bits used by pthreads R/W locks for tracking locked vs. unlocked state, as
// well as reader count.
//
// Notes about pthreads R/W lock state...
// 1) (state == 0)               => "unlocked"
// 2) (state in [1, 0x7ffffffe]) => locked-for-read.
// 3) (state == 0x7fffffff)      => locked-for-write.
// 4) #2 and #3 above may also have the CONTESTED bit set to indicate that there
//    are waiters.
#define PTHREAD_MUTEX_RWLOCK_CONTESTED_BIT  ((int)0x80000000)
#define PTHREAD_MUTEX_RWLOCK_COUNT_MASK     ((int)(~PTHREAD_MUTEX_RWLOCK_CONTESTED_BIT))
#define PTHREAD_MUTEX_RWLOCK_UNLOCKED       ((int)0)
#define PTHREAD_MUTEX_RWLOCK_LOCKED_FOR_WR  (PTHREAD_MUTEX_RWLOCK_COUNT_MASK)
#define PTHREAD_MUTEX_RWLOCK_MAX_RD_COUNT   ((int)(PTHREAD_MUTEX_RWLOCK_COUNT_MASK - 1))

extern void* __pthread_tsd_main[];
extern volatile size_t __pthread_tsd_size;

void* __tls_get_new(size_t*) ATTR_LIBC_VISIBILITY;

static inline pthread_t __pthread_self(void) {
    return tp_to_pthread(zxr_tp_get());
}

static inline thrd_t __thrd_current(void) {
    return (thrd_t)__pthread_self();
}

static inline pid_t __thread_get_tid(void) {
    return zxr_thread_get_handle(&__pthread_self()->zxr_thread);
}

int __pthread_create(pthread_t* __restrict, const pthread_attr_t* __restrict,
                     void* (*)(void*), void* __restrict) ATTR_LIBC_VISIBILITY;
int __pthread_detach(pthread_t t) ATTR_LIBC_VISIBILITY;
_Noreturn void __pthread_exit(void* result) ATTR_LIBC_VISIBILITY;
int __pthread_join(pthread_t t, void** result) ATTR_LIBC_VISIBILITY;

// Signal n (or all, for -1) threads on a pthread_cond_t or cnd_t.
void __private_cond_signal(void* condvar, int n) ATTR_LIBC_VISIBILITY;

// This is guaranteed to only return 0, EINVAL, or ETIMEDOUT.
int __timedwait(atomic_int*, int, clockid_t, const struct timespec*)
    ATTR_LIBC_VISIBILITY;

// Loading a library can introduce more thread_local variables. Thread
// allocation bases bookkeeping decisions based on the current state
// of thread_locals in the program, so thread creation needs to be
// inhibited by a concurrent dlopen. This lock implements that
// exclusion.
void __thread_allocation_inhibit(void) ATTR_LIBC_VISIBILITY;
void __thread_allocation_release(void) ATTR_LIBC_VISIBILITY;

void __thread_tsd_run_dtors(void) ATTR_LIBC_VISIBILITY;

#define DEFAULT_PTHREAD_ATTR                                                  \
    ((pthread_attr_t){                                                        \
        ._a_stacksize = libc.stack_size,                                      \
        ._a_guardsize = PAGE_SIZE,                                            \
    })

thrd_t __allocate_thread(size_t guard_size,
                         size_t stack_size,
                         const char* thread_name,
                         char default_name[ZX_MAX_NAME_LEN])
    __attribute__((nonnull(3))) ATTR_LIBC_VISIBILITY;

pthread_t __init_main_thread(zx_handle_t thread_self) ATTR_LIBC_VISIBILITY;

int __clock_gettime(clockid_t, struct timespec*) ATTR_LIBC_VISIBILITY;
