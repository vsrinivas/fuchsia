// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <magenta/internal.h>
#include <magenta/types.h>
#include <magenta/syscalls/types.h>

__BEGIN_CDECLS

// Define all of the syscalls from the syscall list header.
// User space syscall veneer routines are all prefixed with mx_.
#define MAGENTA_VDSOCALL_DEF(ret, name, args...) \
    extern ret _mx_##name(args); \
    extern ret mx_##name(args);
#define MAGENTA_VDSOCALL_DEF_WITH_ATTRS(ret, name, attrs, args...) \
    extern ret _mx_##name(args) __attribute__(attrs); \
    extern ret mx_##name(args) __attribute__(attrs);

#define MAGENTA_SYSCALL_DEF(nargs64, nargs32, n, ret, name, args...) \
    MAGENTA_VDSOCALL_DEF(ret, name, args)
#define MAGENTA_SYSCALL_DEF_WITH_ATTRS(nargs64, nargs32, n, ret, name, attrs, args...) \
    MAGENTA_VDSOCALL_DEF_WITH_ATTRS(ret, name, attrs, args)

#include <magenta/syscalls.inc>

// Accessors for state provided by the language runtime (eg. libc)

#define mx_process_self _mx_process_self
static inline mx_handle_t _mx_process_self(void) {
  return __magenta_process_self;
}

#define mx_vmar_root_self _mx_vmar_root_self
static inline mx_handle_t _mx_vmar_root_self(void) {
  return __magenta_vmar_root_self;
}

__END_CDECLS
