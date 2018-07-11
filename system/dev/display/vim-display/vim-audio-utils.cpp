// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fbl/alloc_checker.h>

#include "vim-audio-utils.h"

namespace audio {
namespace vim2 {

fbl::RefPtr<Registers> Registers::Create(const platform_device_protocol_t* pdev,
                                         uint32_t which_mmio,
                                         zx_status_t* out_res) {
    fbl::AllocChecker ac;
    auto ret = fbl::AdoptRef(new (&ac) Registers());
    if (!ac.check()) {
        *out_res = ZX_ERR_NO_MEMORY;
    } else {
        *out_res = ret->Map(pdev, which_mmio);
    }

    return (*out_res != ZX_OK) ? nullptr : ret;
}

Registers::~Registers() {
    io_buffer_release(&buf_);
}

zx_status_t Registers::Map(const platform_device_protocol_t* pdev, uint32_t which_mmio) {
    ZX_DEBUG_ASSERT(pdev != nullptr);
    ZX_DEBUG_ASSERT(buf_.virt == nullptr);
    zx_status_t res;

    res = pdev_map_mmio_buffer(pdev, which_mmio, ZX_CACHE_POLICY_UNCACHED_DEVICE, &buf_);
    if (res == ZX_OK) {
        base_ = reinterpret_cast<volatile uint32_t*>(
                reinterpret_cast<uintptr_t>(buf_.virt) + buf_.offset);
    }

    return res;
}

fbl::RefPtr<RefCountedVmo> RefCountedVmo::Create(zx::vmo vmo) {
    if (!vmo.is_valid()) {
        return nullptr;
    }

    fbl::AllocChecker ac;
    auto ret = fbl::AdoptRef(new (&ac) RefCountedVmo(fbl::move(vmo)));
    if (!ac.check()) {
        return nullptr;
    }

    return ret;
}

}  // namespace vim2
}  // namespace audio
