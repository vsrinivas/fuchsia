#pragma once

#include <sys/syscall.h>
#include <unistd.h>

#ifndef SYSCALL_RLIM_INFINITY
#define SYSCALL_RLIM_INFINITY (~0ULL)
#endif

#ifndef SYSCALL_MMAP2_UNIT
#define SYSCALL_MMAP2_UNIT 4096ULL
#endif

typedef long syscall_arg_t;

long __linux_syscall(const char* fn, int ln, syscall_arg_t nr, ...);
long __syscall_ret(unsigned long r);

#define __syscall(nr...) __linux_syscall(__FILE__, __LINE__, nr)

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

#ifdef SYS_pread64
#undef SYS_pread
#undef SYS_pwrite
#define SYS_pread SYS_pread64
#define SYS_pwrite SYS_pwrite64
#endif

#ifdef SYS_fadvise64_64
#undef SYS_fadvise
#define SYS_fadvise SYS_fadvise64_64
#elif defined(SYS_fadvise64)
#undef SYS_fadvise
#define SYS_fadvise SYS_fadvise64
#endif

#define __sys_open2(x, pn, fl) __syscall(SYS_openat, AT_FDCWD, pn, (fl) | O_LARGEFILE)
#define __sys_open3(x, pn, fl, mo) __syscall(SYS_openat, AT_FDCWD, pn, (fl) | O_LARGEFILE, mo)

#define __sys_open(...) __SYSCALL_DISP(__sys_open, , __VA_ARGS__)
#define sys_open(...) __syscall_ret(__sys_open(__VA_ARGS__))
