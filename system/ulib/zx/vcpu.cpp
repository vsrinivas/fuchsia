// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zx/vcpu.h>

#include <zircon/syscalls.h>

namespace zx {

zx_status_t vcpu::create(const guest& guest, uint32_t options,
                         zx_gpaddr_t entry, vcpu* vcpu) {
    // Assume |guest| and |vcpu| must refer to different containers, due to
    // strict aliasing.
    return zx_vcpu_create(guest.get(), options, entry,
                          vcpu->reset_and_get_address());
}

} // namespace zx
