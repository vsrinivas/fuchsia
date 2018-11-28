// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zx/bti.h>

#include <zircon/syscalls.h>

namespace zx {

zx_status_t bti::create(const iommu& iommu, uint32_t options, uint64_t bti_id, bti* result) {
    return zx_bti_create(iommu.get(), options, bti_id, result->reset_and_get_address());
}

} // namespace zx
