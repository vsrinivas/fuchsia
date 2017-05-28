// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <mx/fifo.h>

#include <magenta/syscalls.h>

namespace mx {

mx_status_t fifo::create(uint32_t elem_count, uint32_t elem_size,
                         uint32_t options, fifo* out0, fifo* out1) {
    mx_handle_t h0 = MX_HANDLE_INVALID, h1 = MX_HANDLE_INVALID;
    mx_status_t result = mx_fifo_create(elem_count, elem_size, options, &h0, &h1);
    out0->reset(h0);
    out1->reset(h1);
    return result;
}

} // namespace mx
