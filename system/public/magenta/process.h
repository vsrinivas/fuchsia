// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <magenta/types.h>
#include <stdint.h>

__BEGIN_CDECLS

// Accessors for process state provided by the language runtime (eg. libc)

// Examines the set of handles received at process startup for one matching
// |hnd_info|.  If one is found, atomically returns it and removes it from the
// set available to future calls.
// |hnd_info| is a value returned by MX_HND_INFO().
mx_handle_t mx_get_startup_handle(uint32_t hnd_info);

// Accessors for Magenta-specific state maintained by the a language runtime

#define mx_process_self _mx_process_self
static inline mx_handle_t _mx_process_self(void) {
    extern mx_handle_t __magenta_process_self;
    return __magenta_process_self;
}

#define mx_vmar_root_self _mx_vmar_root_self
static inline mx_handle_t _mx_vmar_root_self(void) {
    extern mx_handle_t __magenta_vmar_root_self;
    return __magenta_vmar_root_self;
}

#define mx_job_default _mx_job_default
static inline mx_handle_t _mx_job_default(void) {
    extern mx_handle_t __magenta_job_default;
    return __magenta_job_default;
}

__END_CDECLS
