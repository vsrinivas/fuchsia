#ifndef LIBC_H
#define LIBC_H

#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

struct __locale_map;

struct __locale_struct {
    const struct __locale_map* volatile cat[6];
};

struct tls_module {
    struct tls_module* next;
    void* image;
    size_t len, size, align, offset;
};

struct __libc {
    int threaded;
    int secure;
    size_t* auxv;
    struct tls_module* tls_head;
    size_t tls_size, tls_align, tls_cnt;
    size_t page_size;
    struct __locale_struct global_locale;
};

#ifdef __PIC__
#define ATTR_LIBC_VISIBILITY __attribute__((visibility("hidden")))
#else
#define ATTR_LIBC_VISIBILITY
#endif

extern struct __libc __libc ATTR_LIBC_VISIBILITY;
#define libc __libc

extern size_t __hwcap ATTR_LIBC_VISIBILITY;
extern char *__progname, *__progname_full;

int __lockfile(FILE*) ATTR_LIBC_VISIBILITY;
void __unlockfile(FILE*) ATTR_LIBC_VISIBILITY;

int __setxid(int, int, int, int);

extern char** __environ;

ssize_t __magenta_io_write(int fd, const void* buf, size_t count);

#undef weak_alias
#define weak_alias(old, new) extern __typeof(old) new __attribute__((weak, alias(#old)))

#undef LFS64_2
#define LFS64_2(x, y) weak_alias(x, y)

#undef LFS64
#define LFS64(x) LFS64_2(x, x##64)

#endif
