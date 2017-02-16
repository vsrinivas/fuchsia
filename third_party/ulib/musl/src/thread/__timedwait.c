#include "pthread_impl.h"
#include "syscall.h"
#include <errno.h>
#include <magenta/syscalls.h>
#include <pthread.h>
#include <time.h>

int __pthread_setcancelstate(int, int*);
int __clock_gettime(clockid_t, struct timespec*);

#define NS_PER_S (1000000000ull)

int __timedwait_cp(volatile int* addr, int val, clockid_t clk, const struct timespec* at) {
    struct timespec to;
    mx_time_t deadline = MX_TIME_INFINITE;

    if (at) {
        if (at->tv_nsec >= NS_PER_S)
            return EINVAL;
        if (__clock_gettime(clk, &to))
            return EINVAL;
        to.tv_sec = at->tv_sec - to.tv_sec;
        if ((to.tv_nsec = at->tv_nsec - to.tv_nsec) < 0) {
            to.tv_sec--;
            to.tv_nsec += NS_PER_S;
        }
        if (to.tv_sec < 0)
            return ETIMEDOUT;
        deadline = to.tv_sec * NS_PER_S;
        deadline += to.tv_nsec;
    }

    // mx_futex_wait will return ERR_BAD_STATE if someone modifying *addr
    // races with this call. But this is indistinguishable from
    // otherwise being woken up just before someone else changes the
    // value. Therefore this functions returns 0 in that case.
    switch (_mx_futex_wait((void*)addr, val, deadline)) {
    case NO_ERROR:
    case ERR_BAD_STATE:
        return 0;
    case ERR_TIMED_OUT:
        return ETIMEDOUT;
    case ERR_INVALID_ARGS:
    default:
        __builtin_trap();
    }
}

int __timedwait(volatile int* addr, int val, clockid_t clk, const struct timespec* at) {
    int cs, r;
    __pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &cs);
    r = __timedwait_cp(addr, val, clk, at);
    __pthread_setcancelstate(cs, 0);
    return r;
}
