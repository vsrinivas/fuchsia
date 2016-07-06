// Copyright 2016 The Fuchsia Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include <magenta/prctl.h>
#include <magenta/syscalls.h>
#include <magenta/tlsroot.h>
#include <system/compiler.h>

__BEGIN_CDECLS

typedef uint32_t mxr_tls_t;

#define MXR_TLS_SLOT_MAX ((mxr_tls_t)256)
#define MXR_TLS_SLOT_SELF ((mxr_tls_t)0)
#define MXR_TLS_SLOT_ERRNO ((mxr_tls_t)1)
#define MXR_TLS_SLOT_INVALID ((mxr_tls_t)-1)

#if defined(__aarch64__)
static inline mx_tls_root_t* mxr_tls_root_get(void) {
    mx_tls_root_t* tlsroot;
    __asm__ volatile("mrs %0, tpidr_el0"
                     : "=r"(tlsroot));
    return tlsroot;
}
static inline mx_status_t mxr_tls_root_set(mx_tls_root_t* tlsroot) {
    __asm__ volatile("msr tpidr_el0, %0"
                     :
                     : "r"(tlsroot));
    return NO_ERROR;
}

#elif defined(__arm__)
static inline mx_tls_root_t* mxr_tls_root_get(void) {
    mx_tls_root_t* tlsroot;
    __asm__ __volatile__("mrc p15, 0, %0, c13, c0, 3"
                         : "=r"(tlsroot));
    return tlsroot;
}
static inline mx_status_t mxr_tls_root_set(mx_tls_root_t* tlsroot) {
    // TODO(kulakowski) Thread self handle.
    mx_handle_t self = 0;
    return _magenta_thread_arch_prctl(self, ARCH_SET_CP15_READONLY, (uintptr_t*)&tlsroot);
}

#elif defined(__x86_64__)
static inline mx_tls_root_t* mxr_tls_root_get(void) {
    mx_tls_root_t* tlsroot;
    __asm__ __volatile__("mov %%fs:0,%0"
                         : "=r"(tlsroot));
    return tlsroot;
}
static inline mx_status_t mxr_tls_root_set(mx_tls_root_t* tlsroot) {
    // TODO(kulakowski) Thread self handle.
    mx_handle_t self = 0;
    return _magenta_thread_arch_prctl(self, ARCH_SET_FS, (uintptr_t*)&tlsroot);
}

#else
#error Unsupported architecture

#endif

mxr_tls_t mxr_tls_allocate(void);

static inline void* mxr_tls_get(mxr_tls_t slot) {
    return mxr_tls_root_get()->slots[slot];
}

static inline void mxr_tls_set(mxr_tls_t slot, void* value) {
    mxr_tls_root_get()->slots[slot] = value;
}

__END_CDECLS
