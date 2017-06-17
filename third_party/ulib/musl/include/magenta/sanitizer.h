// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

// Interfaces declared in this file are intended for the use of sanitizer
// runtime library implementation code.  Each sanitizer runtime works only
// with the appropriately sanitized build of libc.  These functions should
// never be called when using the unsanitized libc.  But these names are
// always exported so that the libc ABI is uniform across sanitized and
// unsanitized builds (only unsanitized shared library binaries are used at
// link time, including linking the sanitizer runtime shared libraries).

#include <magenta/compiler.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <threads.h>

__BEGIN_CDECLS

// These are aliases for the functions defined in libc, which are always
// the unsanitized versions.  The sanitizer runtimes can call them by these
// aliases when they are overriding libc's definitions of the unadorned
// symbols.
__typeof(memcpy) __unsanitized_memcpy;
__typeof(memmove) __unsanitized_memmove;
__typeof(memset) __unsanitized_memset;

// The sanitized libc allocates the shadow memory in the appropriate ratio for
// the particular sanitizer (shadow_base == shadow_limit >> SHADOW_SCALE)
// early during startup, before any other address space allocations can occur.
// Shadow memory always starts at address zero:
//     [memory_limit,   UINTPTR_MAX)    Address space reserved by the system.
//     [shadow_limit,   memory_limit)   Address space available to the user.
//     [shadow_base,    shadow_limit)   Shadow memory, preallocated.
//     [0,              shadow_base)    Shadow gap, cannot be allocated.
typedef struct {
    uintptr_t shadow_base;
    uintptr_t shadow_limit;
    uintptr_t memory_limit;
} sanitizer_shadow_bounds_t;
sanitizer_shadow_bounds_t __sanitizer_shadow_bounds(void);


// The "hook" interfaces are functions that the sanitizer runtime library
// can define and libc will call.  There are default definitions in libc
// which do nothing, but any other definitions will override those.  These
// declarations use __EXPORT (i.e. explicit STV_DEFAULT) to ensure any user
// definitions are seen by libc even if the user code is being compiled
// with -fvisibility=hidden or equivalent.

// This is called when a new thread has been created but is not yet
// running.  Its C11 thrd_t value has been determined and its stack has
// been allocated.  All that remains is to actually start the thread
// running (which can fail only in catastrophic bug situations).  Its
// return value will be passed to __sanitizer_thread_create_hook, below.
__EXPORT void *__sanitizer_before_thread_create_hook(
    thrd_t thread, bool detached, const char* name,
    void* stack_base, size_t stack_size);

// This is called after a new thread has been created or creation has
// failed at the final stage; __sanitizer_before_thread_create_hook has
// been called first, and its return value is the first argument here.
// The second argument is what the return value of C11 thrd_create would
// be for this creation attempt (which might have been instigated by
// either thrd_create or pthread_create).  If it's thrd_success, then
// the new thread has now started running.  Otherwise (it's a different
// <threads.h> thrd_* value), thread creation has failed and the thread
// details reported to __sanitizer_before_thread_create_hook will be
// freed without the thread ever starting.
__EXPORT void __sanitizer_thread_create_hook(
    void* hook, thrd_t thread, int error);

// This is called in each new thread as it starts up.  The argument is
// the same one returned by __sanitizer_before_thread_create_hook and
// previously passed to __sanitizer_thread_create_hook.
__EXPORT void __sanitizer_thread_start_hook(void* hook, thrd_t self);

// This is called in each thread just before it dies.
// All thread-specific destructors have been run.
// The argument is the same one passed to __sanitizer_thread_start_hook.
__EXPORT void __sanitizer_thread_exit_hook(void* hook, thrd_t self);

__END_CDECLS
