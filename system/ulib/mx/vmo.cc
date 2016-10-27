// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <mx/vmo.h>

#include <magenta/syscalls.h>

namespace mx {

mx_status_t vmo::create(uint64_t size, uint32_t options, vmo* result) {
    mx_handle_t h = mx_vmo_create(size);
    if (h < 0) {
        result->reset(MX_HANDLE_INVALID);
        return h;
    } else {
        result->reset(h);
        return NO_ERROR;
    }
}

} // namespace mx
