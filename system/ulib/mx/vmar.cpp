// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <mx/vmar.h>

#include <magenta/syscalls.h>

namespace mx {

mx_status_t vmar::allocate(size_t offset, size_t size, uint32_t flags,
                           vmar* child, uintptr_t* child_addr) const {
    mx_handle_t h;
    mx_status_t status = mx_vmar_allocate(get(), offset, size, flags, &h, child_addr);
    if (status == MX_OK) {
        child->reset(h);
    } else {
        child->reset(MX_HANDLE_INVALID);
    }
    return status;
}

} // namespace mx
