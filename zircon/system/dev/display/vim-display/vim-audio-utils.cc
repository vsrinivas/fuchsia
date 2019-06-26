// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vim-audio-utils.h"

#include <fbl/alloc_checker.h>

#include <utility>

namespace audio {
namespace vim2 {

fbl::RefPtr<Registers> Registers::Create(const pdev_protocol_t* pdev, uint32_t which_mmio,
                                         zx_status_t* out_res) {
  ZX_DEBUG_ASSERT(pdev != nullptr);

  mmio_buffer_t mmio;
  *out_res = pdev_map_mmio_buffer(pdev, which_mmio, ZX_CACHE_POLICY_UNCACHED_DEVICE, &mmio);
  if (*out_res != ZX_OK) {
    return nullptr;
  }

  fbl::AllocChecker ac;
  auto ret = fbl::AdoptRef(new (&ac) Registers(mmio));
  if (!ac.check()) {
    *out_res = ZX_ERR_NO_MEMORY;
    return nullptr;
  }

  return ret;
}

fbl::RefPtr<RefCountedVmo> RefCountedVmo::Create(zx::vmo vmo) {
  if (!vmo.is_valid()) {
    return nullptr;
  }

  fbl::AllocChecker ac;
  auto ret = fbl::AdoptRef(new (&ac) RefCountedVmo(std::move(vmo)));
  if (!ac.check()) {
    return nullptr;
  }

  return ret;
}

}  // namespace vim2
}  // namespace audio
