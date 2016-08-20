// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <runtime/tls.h>

#include <runtime/process.h>
#include <stdatomic.h>

mxr_tls_t mxr_tls_allocate(void) {
    mxr_tls_t* next_slot = &mxr_process_get_info()->next_tls_slot;
    mxr_tls_t slot = atomic_fetch_add(next_slot, 1);
    if (slot < MXR_TLS_SLOT_MAX)
        return slot;
    atomic_store(next_slot, MXR_TLS_SLOT_MAX);
    return MXR_TLS_SLOT_INVALID;
}
