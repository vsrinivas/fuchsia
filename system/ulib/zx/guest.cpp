// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zx/guest.h>

#include <zircon/syscalls.h>

namespace zx {

zx_status_t guest::create(const resource& resource, uint32_t options,
                          const vmo& physmem, guest* result) {
    // Assume |result| uses a distinct container from |resource| and
    // |physmem|, due to strict aliasing.
    return zx_guest_create(
        resource.get(), options, physmem.get(),
        result->reset_and_get_address());
}

} // namespace zx
