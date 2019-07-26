// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zx/iommu.h>

#include <zircon/syscalls.h>

namespace zx {

zx_status_t iommu::create(const resource& resource, uint32_t type, const void* desc,
                          size_t desc_size, iommu* result) {
  return zx_iommu_create(resource.get(), type, desc, desc_size, result->reset_and_get_address());
}

}  // namespace zx
