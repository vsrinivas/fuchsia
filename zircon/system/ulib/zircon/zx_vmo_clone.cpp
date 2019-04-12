// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "private.h"

// TODO(stevensd): Remove this stub once Go migrates to the new syscall.
zx_status_t _zx_vmo_clone(zx_handle_t handle, uint32_t options,
                          uint64_t offset, uint64_t size, zx_handle_t* out) {
    return SYSCALL_zx_vmo_create_child(handle, options, offset, size, out);
}

VDSO_INTERFACE_FUNCTION(zx_vmo_clone);
