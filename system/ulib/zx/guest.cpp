// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zx/guest.h>

#include <zircon/syscalls.h>

#include <lib/zx/vmar.h>

namespace zx {

zx_status_t guest::create(const resource& resource, uint32_t options,
                          guest* guest, vmar* vmar) {
    // Assume |resource|, |guest| and |vmar| must refer to different containers,
    // due to strict aliasing.
    return zx_guest_create(
        resource.get(), options, guest->reset_and_get_address(),
        vmar->reset_and_get_address());
}

} // namespace zx
