// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <magenta/internal.h>
#include <magenta/syscalls/types.h>

__BEGIN_CDECLS

// define all of the syscalls from the syscall list header.
// user space syscall veneer routines are all prefixed with mx_
#define MAGENTA_SYSCALL_DEF(nargs64, nargs32, n, ret, name, args...) \
    extern ret _mx_##name(args); \
    extern ret mx_##name(args);
#define MAGENTA_SYSCALL_DEF_WITH_ATTRS(nargs64, nargs32, n, ret, name, attrs, args...) \
    extern ret _mx_##name(args) __attribute__(attrs); \
    extern ret mx_##name(args) __attribute__(attrs);

#include <magenta/syscalls.inc>

// Accessors for state provided by the language runtime (eg. libc)

#define mx_process_self _mx_process_self
static inline mx_handle_t _mx_process_self(void) {
  return __magenta_process_self;
}

// Compatibility Wrappers

#ifndef WITHOUT_COMPAT_SYSCALLS

#define mx_exit _mx_exit
__attribute__((noreturn)) static inline void _mx_exit(int rc) {
    mx_process_exit(rc);
}

#define mx_current_time _mx_current_time
static inline mx_time_t mx_current_time(void) {
    return mx_time_get(MX_CLOCK_MONOTONIC);
}

#define mx_debug_read_memory _mx_debug_read_memory
static inline mx_ssize_t _mx_debug_read_memory(mx_handle_t proc, uintptr_t vaddr,
                                               mx_size_t len, void* buffer) {
    mx_status_t status = mx_process_read_memory(proc, vaddr, buffer, len, &len);
    if (status < 0) {
        return status;
    } else {
        return len;
    }
}

#define mx_debug_write_memory _mx_debug_write_memory
static inline mx_ssize_t _mx_debug_write_memory(mx_handle_t proc, uintptr_t vaddr,
                                                mx_size_t len, const void* buffer) {
    mx_status_t status = mx_process_write_memory(proc, vaddr, buffer, len, &len);
    if (status < 0) {
        return status;
    } else {
        return len;
    }
}

#endif

__END_CDECLS
