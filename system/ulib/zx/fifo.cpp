// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zx/fifo.h>

#include <zircon/syscalls.h>

namespace zx {

zx_status_t fifo::create(uint32_t elem_count, uint32_t elem_size,
                         uint32_t options, fifo* out0, fifo* out1) {
    // Ensure aliasing of both out parameters to the same container
    // has a well-defined result, and does not leak.
    fifo h0;
    fifo h1;
    zx_status_t status = zx_fifo_create(
        elem_count, elem_size, options, h0.reset_and_get_address(),
        h1.reset_and_get_address());
    out0->reset(h0.release());
    out1->reset(h1.release());
    return status;
}

} // namespace zx
