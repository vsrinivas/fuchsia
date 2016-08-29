#pragma once

#include <limits.h>
#include <magenta/types.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <magenta/types.h>

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
    int secure;
    uintptr_t stack_base;
    size_t stack_size;
    mx_handle_t proc;
    atomic_int thread_count;
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

extern char** __environ;

#undef weak_alias
#define weak_alias(old, new) extern __typeof(old) new __attribute__((weak, alias(#old)))
