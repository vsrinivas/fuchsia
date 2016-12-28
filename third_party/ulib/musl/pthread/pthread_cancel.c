#define _GNU_SOURCE
#include "libc.h"
#include "pthread_impl.h"
#include "syscall.h"
#include <string.h>

long __cancel(void) {
    pthread_t self = __pthread_self();
    if (self->canceldisable == PTHREAD_CANCEL_ENABLE || self->cancelasync)
        pthread_exit(PTHREAD_CANCELED);
    self->canceldisable = PTHREAD_CANCEL_DISABLE;
    return -ECANCELED;
}

long __syscall_cp_asm(volatile void*, syscall_arg_t, syscall_arg_t, syscall_arg_t, syscall_arg_t,
                      syscall_arg_t, syscall_arg_t, syscall_arg_t);

static void _sigaddset(sigset_t* set, int sig) {
    unsigned s = sig - 1;
    set->__bits[s / 8 / sizeof *set->__bits] |= 1UL << (s & 8 * sizeof *set->__bits - 1);
}

__attribute__((__visibility__("hidden"))) extern const char __cp_begin[1], __cp_end[1],
    __cp_cancel[1];

static void cancel_handler(int sig, siginfo_t* si, void* ctx) {
    pthread_t self = __pthread_self();
    ucontext_t* uc = ctx;
    uintptr_t pc = uc->uc_mcontext.MC_PC;

    a_barrier();
    if (!self->cancel || self->canceldisable == PTHREAD_CANCEL_DISABLE)
        return;

    _sigaddset(&uc->uc_sigmask, SIGCANCEL);

    if (self->cancelasync || pc >= (uintptr_t)__cp_begin && pc < (uintptr_t)__cp_end) {
        uc->uc_mcontext.MC_PC = (uintptr_t)__cp_cancel;
        return;
    }

    // TODO(kulakowski) Actually raise SIGCANCEL on __thread_get_tid().
}

void __testcancel(void) {
    pthread_t self = __pthread_self();
    if (self->cancel && !self->canceldisable)
        __cancel();
}

static void init_cancellation(void) {
    struct sigaction sa = {.sa_flags = SA_SIGINFO | SA_RESTART, .sa_sigaction = cancel_handler};
    memset(&sa.sa_mask, -1, _NSIG / 8);
    __libc_sigaction(SIGCANCEL, &sa, 0);
}

int pthread_cancel(pthread_t t) {
    static int init;
    if (!init) {
        init_cancellation();
        init = 1;
    }
    a_store(&t->cancel, 1);
    if (t == pthread_self() && !t->cancelasync)
        return 0;
    return pthread_kill(t, SIGCANCEL);
}
