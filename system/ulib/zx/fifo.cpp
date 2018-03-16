// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zx/fifo.h>

#include <zircon/syscalls.h>

namespace zx {

zx_status_t fifo::create(uint32_t elem_count, uint32_t elem_size,
                         uint32_t options, fifo* out0, fifo* out1) {
    zx_handle_t h0 = ZX_HANDLE_INVALID, h1 = ZX_HANDLE_INVALID;
    zx_status_t result = zx_fifo_create(elem_count, elem_size, options, &h0, &h1);
    out0->reset(h0);
    out1->reset(h1);
    return result;
}

} // namespace zx
