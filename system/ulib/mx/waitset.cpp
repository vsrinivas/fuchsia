// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <mx/waitset.h>

#include <magenta/syscalls.h>

namespace mx {

mx_status_t waitset::create(uint32_t options, waitset* result) {
    mx_handle_t h = MX_HANDLE_INVALID;
    mx_status_t status = mx_waitset_create(options, &h);
    result->reset(h);
    return status;
}

} // namespace mx
