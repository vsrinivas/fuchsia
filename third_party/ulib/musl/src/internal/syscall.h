#pragma once

#include <sys/syscall.h>
#include <threads.h>
#include <unistd.h>

#ifndef SYSCALL_RLIM_INFINITY
#define SYSCALL_RLIM_INFINITY (~0ULL)
#endif

#ifndef SYSCALL_MMAP2_UNIT
#define SYSCALL_MMAP2_UNIT 4096ULL
#endif

typedef long syscall_arg_t;

long __linux_syscall(const char* fn, int ln, once_flag* once, syscall_arg_t nr, ...);
long __syscall_ret(unsigned long r);

#define __syscall(nr...) ({                         \
    static once_flag once;                          \
    __linux_syscall(__FILE__, __LINE__, &once, nr); \
})

#define __SYSCALL_NARGS_X(a, b, c, d, e, f, g, h, n, ...) n
#define __SYSCALL_NARGS(...) __SYSCALL_NARGS_X(__VA_ARGS__, 7, 6, 5, 4, 3, 2, 1, 0, )
#define __SYSCALL_CONCAT_X(a, b) a##b
#define __SYSCALL_CONCAT(a, b) __SYSCALL_CONCAT_X(a, b)
#define __SYSCALL_DISP(b, ...)                        \
    __SYSCALL_CONCAT(b, __SYSCALL_NARGS(__VA_ARGS__)) \
    (__VA_ARGS__)

#define syscall(...) __syscall_ret(__syscall(__VA_ARGS__))

/* fixup legacy 32-bit-vs-lfs64 junk */

#ifdef SYS_ugetrlimit
#undef SYS_getrlimit
#define SYS_getrlimit SYS_ugetrlimit
#endif
