// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <magenta/processargs.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// The mx_tls_root_t describes the object pointed to by the thread-local
// storage (TLS) register in a Magenta process. It holds TLS slots that
// are used by libraries and language runtimes.
//
// To claim a slot, increment the next_tls_slot field in mx_proc_info
// with a compare-and-swap and take the old value as an index into the
// slots array.
//
// Any user code that calls the mx_thread_create system call is
// responsible for allocating an mx_tls_root_t object with at least 8
// slots, setting its fields correctly, and placing a pointer to it in
// the TLS register appropriately for the current architecture.
//
// On X86_64, *mx_tls_root_t should be loaded into the FS register.
// On ARM64, *mx_tls_root_t should be loaded into the TPIDR_EL0 register.
// On ARM32, *mx_tls_root_t should be loaded into the CP15 readonly register.

typedef struct mx_tls_root mx_tls_root_t;

struct mx_tls_root {
    mx_tls_root_t* self;
    mx_proc_info_t* proc;
    uint32_t magic;    // MX_TLS_ROOT_MAGIC
    uint16_t flags;    // Reserved for future use.
    uint16_t maxslots; // Number of slots in this object, minimum 8.
    void* slots[1];    // TLS slots. Has length maxslots.
};

#define MX_TLS_ROOT_MAGIC 0x2facef0e
#define MX_TLS_MIN_SLOTS 8

#define MX_TLS_ROOT_SIZE(x) \
    (sizeof(mx_tls_root) + sizeof(void*) * (x->maxslots - 1))

#ifdef __cplusplus
}
#endif
