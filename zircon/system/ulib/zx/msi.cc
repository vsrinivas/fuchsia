// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zx/interrupt.h>
#include <lib/zx/msi.h>
#include <zircon/syscalls.h>

namespace zx {

zx_status_t msi::allocate(const resource& resource, uint32_t count, msi* result) {
  return zx_msi_allocate(resource.get(), count, result->reset_and_get_address());
}

zx_status_t msi::create(const msi& msi, uint32_t options, uint32_t msi_id, const vmo& vmo,
                        size_t vmo_offset, interrupt* result) {
  return zx_msi_create(msi.get(), options, msi_id, vmo.get(), vmo_offset,
                       result->reset_and_get_address());
}

}  // namespace zx
