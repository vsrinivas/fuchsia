#pragma once

#include "atomic.h"
#include "libc.h"
#include "syscall.h"
#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <signal.h>
#include <threads.h>

#include <runtime/thread.h>
#include <runtime/tls.h>

#define pthread __pthread

struct pthread {
    struct pthread* self;
    void **dtv, *unused1, *unused2;
    uintptr_t canary, canary2;
    pid_t tid, pid;
    int errno_value;
    int tsd_used;
    volatile int cancel, canceldisable, cancelasync;
    int detached;
    unsigned char* map_base;
    size_t map_size;
    void* stack;
    size_t stack_size;
    void* start_arg;
    void* (*start)(void*);
    void* result;
    struct __ptcb* cancelbuf;
    void** tsd;
    pthread_attr_t attr;
    volatile int dead;
    int unblock_cancel;
    volatile int timer_id;
    locale_t locale;
    mtx_t killlock;
    mtx_t exitlock;
    mtx_t startlock;
    unsigned long sigmask[_NSIG / 8 / sizeof(long)];
    char* dlerror_buf;
    int dlerror_flag;
    void* stdio_locks;
    uintptr_t canary_at_end;
    void** dtv_copy;
    mxr_thread_t* mxr_thread;
};

struct __timer {
    int timerid;
    pthread_t thread;
};

#include "pthread_arch.h"

#ifndef CANARY
#define CANARY canary
#endif

#ifndef DTP_OFFSET
#define DTP_OFFSET 0
#endif

#define SIGTIMER 32
#define SIGCANCEL 33
#define SIGSYNCCALL 34

#define SIGALL_SET ((sigset_t*)(const unsigned long long[2]){-1, -1})
#define SIGPT_SET                                                                     \
    ((sigset_t*)(const unsigned long[_NSIG / 8 / sizeof(long)]){[sizeof(long) == 4] = \
                                                                    3UL               \
                                                                    << (32 * (sizeof(long) > 4))})
#define SIGTIMER_SET ((sigset_t*)(const unsigned long[_NSIG / 8 / sizeof(long)]){0x80000000})

extern void* __pthread_tsd_main[];
extern volatile size_t __pthread_tsd_size;

static inline pthread_t __pthread_self(void) {
    return tp_to_pthread(mxr_tp_get());
}

static inline pid_t __thread_get_tid(void) {
    // TODO(kulakowski) Replace this with the current thread handle's
    // ID when magenta exposes those.
    return (pid_t)(intptr_t)__pthread_self();
}

// Signal n (or all, for -1) threads on a pthread_cond_t or cnd_t.
void __private_cond_signal(void* condvar, int n);

int __libc_sigaction(int, const struct sigaction*, struct sigaction*);
int __libc_sigprocmask(int, const sigset_t*, sigset_t*);

void __vm_wait(void);
void __vm_lock(void);
void __vm_unlock(void);

// These are guaranteed to only return 0, EINVAL, or ETIMEDOUT.
int __timedwait(volatile int*, int, clockid_t, const struct timespec*);
int __timedwait_cp(volatile int*, int, clockid_t, const struct timespec*);

void __acquire_ptc(void);
void __release_ptc(void);
void __inhibit_ptc(void);

void __block_all_sigs(void*);
void __block_app_sigs(void*);
void __restore_sigs(void*);

#define DEFAULT_STACK_SIZE 81920
#define DEFAULT_GUARD_SIZE PAGE_SIZE
