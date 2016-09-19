// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <magenta/compiler.h>
#include <magenta/prctl.h>
#include <magenta/syscalls.h>

__BEGIN_CDECLS

#pragma GCC visibility push(hidden)

// Get and set the thread pointer.
static inline void* mxr_tp_get(void);
static inline void mxr_tp_set(void* tp);

#if defined(__aarch64__)
static inline void* mxr_tp_get(void) {
    void* tp;
    __asm__ volatile("mrs %0, tpidr_el0"
                     : "=r"(tp));
    return tp;
}
static inline void mxr_tp_set(void* tp) {
    __asm__ volatile("msr tpidr_el0, %0"
                     :
                     : "r"(tp));
}

#elif defined(__arm__)
static inline void* mxr_tp_get(void) {
    void* tp;
    __asm__ __volatile__("mrc p15, 0, %0, c13, c0, 3"
                         : "=r"(tp));
    return tp;
}
static inline void mxr_tp_set(void* tp) {
    // TODO(kulakowski) Thread self handle.
    mx_handle_t self = 0;
    mx_status_t status = _mx_thread_arch_prctl(
        self, ARCH_SET_CP15_READONLY, (uintptr_t*)&tp);
    if (status != NO_ERROR)
        __builtin_trap();
}

#elif defined(__x86_64__)
static inline void* mxr_tp_get(void) {
    void* tp;
    __asm__ __volatile__("mov %%fs:0,%0"
                         : "=r"(tp));
    return tp;
}
static inline void mxr_tp_set(void* tp) {
    // TODO(kulakowski) Thread self handle.
    mx_handle_t self = 0;
    mx_status_t status = _mx_thread_arch_prctl(
        self, ARCH_SET_FS, (uintptr_t*)&tp);
    if (status != NO_ERROR)
        __builtin_trap();
}

#else
#error Unsupported architecture

#endif

#pragma GCC visibility pop

__END_CDECLS
