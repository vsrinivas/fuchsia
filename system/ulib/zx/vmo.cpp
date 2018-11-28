// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zx/vmo.h>
#include <lib/zx/bti.h>

#include <zircon/syscalls.h>

namespace zx {

zx_status_t vmo::create(uint64_t size, uint32_t options, vmo* result) {
    return zx_vmo_create(size, options, result->reset_and_get_address());
}

zx_status_t vmo::create_contiguous(
    const bti& bti, size_t size, uint32_t alignment_log2, vmo* result) {
    return zx_vmo_create_contiguous(bti.get(), size, alignment_log2,
                                    result->reset_and_get_address());
}

zx_status_t vmo::create_physical(
    const resource& resource, zx_paddr_t paddr, size_t size, vmo* result) {
    return zx_vmo_create_physical(resource.get(), paddr, size, result->reset_and_get_address());
}

} // namespace zx
