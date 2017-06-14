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
#include <stdint.h>
#include <string.h>

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

__END_CDECLS
