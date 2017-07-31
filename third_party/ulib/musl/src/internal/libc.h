#pragma once

#include <limits.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <magenta/compiler.h>
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
    atomic_int thread_count;
    struct tls_module* tls_head;
    size_t tls_size, tls_align, tls_cnt;
    size_t stack_size;
    size_t page_size;
    struct __locale_struct global_locale;
};

#ifdef __PIC__
#define ATTR_LIBC_VISIBILITY __attribute__((visibility("hidden")))
#else
#define ATTR_LIBC_VISIBILITY
#endif

// Put this on things that are touched only during dynamic linker startup.
#define ATTR_RELRO __SECTION(".data.rel.ro")

extern struct __libc __libc ATTR_LIBC_VISIBILITY;
#define libc __libc

extern size_t __hwcap ATTR_LIBC_VISIBILITY;
extern char *__progname, *__progname_full;

void __libc_start_init(void) ATTR_LIBC_VISIBILITY;

void __funcs_on_exit(void) ATTR_LIBC_VISIBILITY;
void __funcs_on_quick_exit(void) ATTR_LIBC_VISIBILITY;
void __libc_exit_fini(void) ATTR_LIBC_VISIBILITY;

void __dl_thread_cleanup(void) ATTR_LIBC_VISIBILITY;

void __tls_run_dtors(void) ATTR_LIBC_VISIBILITY;

// Registers the handles that mx_get_startup_handle() will return.
//
// This function takes ownership of the data, but not the memory: it assumes
// that the arrays are valid as long as the process is alive.
//
// |handles| and |handle_info| are parallel arrays and must have |nhandles|
//     entries.
// |handles| contains the actual handle values, or MX_HANDLE_INVALID if a
//     handle has already been claimed.
// |handle_info| contains the PA_HND value associated with the
//     corresponding element of |handles|, or zero if the handle has already
//     been claimed.
void __libc_startup_handles_init(uint32_t nhandles,
                                 mx_handle_t handles[],
                                 uint32_t handle_info[]) ATTR_LIBC_VISIBILITY;

_Noreturn void __libc_start_main(void* arg, int (*main)(int, char**, char**));

extern uintptr_t __stack_chk_guard;
void __stack_chk_fail(void);

int __lockfile(FILE*) ATTR_LIBC_VISIBILITY;
void __unlockfile(FILE*) ATTR_LIBC_VISIBILITY;

extern char** __environ;

#undef weak_alias
#define weak_alias(old, new) extern __typeof(old) new __attribute__((weak, alias(#old)))

#ifdef __clang__
#define NO_ASAN __attribute__((no_sanitize("address")))
#else
#define NO_ASAN
#endif
