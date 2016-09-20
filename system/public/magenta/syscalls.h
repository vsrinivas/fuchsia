// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <magenta/types.h>
#include <magenta/process.h>
#include <stdbool.h>

#include <magenta/syscalls-types.h>

#ifdef __cplusplus
extern "C" {
#endif

// define all of the syscalls from the syscall list header.
// user space syscall veneer routines are all prefixed with mx_
#define MAGENTA_SYSCALL_DEF(nargs64, nargs32, n, ret, name, args...) \
    extern ret _mx_##name(args); \
    extern ret mx_##name(args);
#define MAGENTA_SYSCALL_DEF_WITH_ATTRS(nargs64, nargs32, n, ret, name, attrs, args...) \
    extern ret _mx_##name(args) __attribute__(attrs); \
    extern ret mx_##name(args) __attribute__(attrs);

#include <magenta/syscalls.inc>

#ifdef __cplusplus
}
#endif
