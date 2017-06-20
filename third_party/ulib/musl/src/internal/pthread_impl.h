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

#include <magenta/stack.h>
#include <magenta/tls.h>
#include <runtime/thread.h>
#include <runtime/tls.h>

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

    mxr_thread_t mxr_thread;

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

static_assert(TP_OFFSETOF(abi.stack_guard) == MX_TLS_STACK_GUARD_OFFSET,
              "stack_guard not at ABI-mandated offset from thread pointer");
static_assert(TP_OFFSETOF(abi.unsafe_sp) == MX_TLS_UNSAFE_SP_OFFSET,
              "unsafe_sp not at ABI-mandated offset from thread pointer");

static inline void* pthread_to_tp(struct pthread* thread) {
    return (void*)((char*)thread + PTHREAD_TP_OFFSET);
}

static inline struct pthread* tp_to_pthread(void* tp) {
    return (struct pthread*)((char*)tp - PTHREAD_TP_OFFSET);
}

#ifndef DTP_OFFSET
#define DTP_OFFSET 0
#endif

#define SIGALL_SET ((sigset_t*)(const unsigned long long[2]){-1, -1})
#define SIGPT_SET                                                                     \
    ((sigset_t*)(const unsigned long[_NSIG / 8 / sizeof(long)]){[sizeof(long) == 4] = \
                                                                    3UL               \
                                                                    << (32 * (sizeof(long) > 4))})
#define SIGTIMER_SET ((sigset_t*)(const unsigned long[_NSIG / 8 / sizeof(long)]){0x80000000})

#define PTHREAD_MUTEX_MASK (PTHREAD_MUTEX_RECURSIVE | PTHREAD_MUTEX_ERRORCHECK)
// The bit used in the recursive and errorchecking cases, which track thread owners.
#define PTHREAD_MUTEX_OWNED_LOCK_BIT 0x80000000
#define PTHREAD_MUTEX_OWNED_LOCK_MASK 0x7fffffff

extern void* __pthread_tsd_main[];
extern volatile size_t __pthread_tsd_size;

void* __tls_get_new(size_t*) ATTR_LIBC_VISIBILITY;

static inline pthread_t __pthread_self(void) {
    return tp_to_pthread(mxr_tp_get());
}

static inline thrd_t __thrd_current(void) {
    return (thrd_t)__pthread_self();
}

static inline pid_t __thread_get_tid(void) {
    // We rely on the fact that the high bit is not set. For now,
    // let's incur the cost of this check, until we consider the
    // userspace handle value representation completely baked.
    pid_t id = __pthread_self()->mxr_thread.handle;
    if (id & PTHREAD_MUTEX_OWNED_LOCK_BIT) {
        __builtin_trap();
    }
    return id;
}

int __pthread_create(pthread_t* __restrict, const pthread_attr_t* __restrict,
                     void* (*)(void*), void* __restrict) ATTR_LIBC_VISIBILITY;
int __pthread_detach(pthread_t t) ATTR_LIBC_VISIBILITY;
_Noreturn void __pthread_exit(void* result) ATTR_LIBC_VISIBILITY;
int __pthread_join(pthread_t t, void** result) ATTR_LIBC_VISIBILITY;

// Signal n (or all, for -1) threads on a pthread_cond_t or cnd_t.
void __private_cond_signal(void* condvar, int n) ATTR_LIBC_VISIBILITY;

int __pthread_key_create(tss_t*, void (*)(void*)) ATTR_LIBC_VISIBILITY;
int __pthread_key_delete(tss_t k) ATTR_LIBC_VISIBILITY;

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

void __pthread_tsd_run_dtors(void) ATTR_LIBC_VISIBILITY;

#define DEFAULT_PTHREAD_ATTR                                                  \
    ((pthread_attr_t){                                                        \
        ._a_stacksize = MAGENTA_DEFAULT_STACK_SIZE,                           \
        ._a_guardsize = PAGE_SIZE,                                            \
    })

pthread_t __allocate_thread(const pthread_attr_t* attr,
                            const char* thread_name,
                            char default_name[MX_MAX_NAME_LEN])
    __attribute__((nonnull(1,2))) ATTR_LIBC_VISIBILITY;

pthread_t __init_main_thread(mx_handle_t thread_self) ATTR_LIBC_VISIBILITY;

int __pthread_once(pthread_once_t*, void (*)(void)) ATTR_LIBC_VISIBILITY;

int __clock_gettime(clockid_t, struct timespec*) ATTR_LIBC_VISIBILITY;
