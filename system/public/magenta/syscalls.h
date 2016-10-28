// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <magenta/process.h>
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

// Compatibility Wrappers

#define mx_msgpipe_create _mx_msgpipe_create
static inline mx_status_t _mx_msgpipe_create(mx_handle_t* out, uint32_t flags) {
    return mx_channel_create(flags, &out[0], &out[1]);
}

#define mx_msgpipe_read _mx_msgpipe_read
static inline mx_status_t _mx_msgpipe_read(mx_handle_t handle,
                                           void* bytes, uint32_t* num_bytes,
                                           mx_handle_t* handles, uint32_t* num_handles,
                                           uint32_t flags) {
    uint32_t nb = num_bytes ? *num_bytes : 0;
    uint32_t nh = num_handles ? *num_handles : 0;
    return mx_channel_read(handle, flags, bytes, nb, num_bytes, handles, nh, num_handles);
}

#define mx_msgpipe_write _mx_msgpipe_write
static inline mx_status_t _mx_msgpipe_write(mx_handle_t handle,
                                            const void* bytes, uint32_t num_bytes,
                                            const mx_handle_t* handles, uint32_t num_handles,
                                            uint32_t flags) {
    return mx_channel_write(handle, flags, bytes, num_bytes, handles, num_handles);
}

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

__END_CDECLS
