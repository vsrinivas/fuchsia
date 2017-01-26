// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <magenta/compiler.h>
#include <magenta/syscalls.h>
#include <magenta/syscalls/object.h>

__BEGIN_CDECLS

#pragma GCC visibility push(hidden)

// Get and set the thread pointer.
static inline void* mxr_tp_get(void);
static inline void mxr_tp_set(mx_handle_t self, void* tp);

#if defined(__aarch64__)
static inline void* mxr_tp_get(void) {
    void* tp;
    __asm__ volatile("mrs %0, tpidr_el0"
                     : "=r"(tp));
    return tp;
}
static inline void mxr_tp_set(mx_handle_t self, void* tp) {
    __asm__ volatile("msr tpidr_el0, %0"
                     :
                     : "r"(tp));
}

#elif defined(__x86_64__)
static inline void* mxr_tp_get(void) {
    void* tp;
    __asm__ __volatile__("mov %%fs:0,%0"
                         : "=r"(tp));
    return tp;
}
static inline void mxr_tp_set(mx_handle_t self, void* tp) {
    mx_status_t status = _mx_object_set_property(
        self, MX_PROP_REGISTER_FS, (uintptr_t*)&tp, sizeof(uintptr_t));
    if (status != NO_ERROR)
        __builtin_trap();
}

#else
#error Unsupported architecture

#endif

#pragma GCC visibility pop

__END_CDECLS
