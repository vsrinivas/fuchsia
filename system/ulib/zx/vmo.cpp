// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zx/vmo.h>

#include <zircon/syscalls.h>

namespace zx {

zx_status_t vmo::create(uint64_t size, uint32_t options, vmo* result) {
    return zx_vmo_create(size, options, result->reset_and_get_address());
}

} // namespace zx
