// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gamma.h"

#include <fbl/alloc_checker.h>

namespace mt8167s_display {

zx_status_t Gamma::Init(zx_device_t* parent) {
  if (initialized_) {
    return ZX_OK;
  }

  zx_status_t status = device_get_protocol(parent, ZX_PROTOCOL_PDEV, &pdev_);
  if (status != ZX_OK) {
    return status;
  }

  // Map GAMMA MMIO
  mmio_buffer_t mmio;
  status = pdev_map_mmio_buffer(&pdev_, MMIO_DISP_GAMMA, ZX_CACHE_POLICY_UNCACHED_DEVICE, &mmio);
  if (status != ZX_OK) {
    DISP_ERROR("Could not map GAMMA mmio\n");
    return status;
  }
  fbl::AllocChecker ac;
  gamma_mmio_ = fbl::make_unique_checked<ddk::MmioBuffer>(&ac, mmio);
  if (!ac.check()) {
    DISP_ERROR("Could not map GAMMA mmio\n");
    return ZX_ERR_NO_MEMORY;
  }

  // GAMMA is ready to be used
  initialized_ = true;
  return ZX_OK;
}
zx_status_t Gamma::Config() {
  ZX_DEBUG_ASSERT(initialized_);
  GammaEnReg::Get().ReadFrom(&(*gamma_mmio_)).set_enable(0).WriteTo(&(*gamma_mmio_));
  GammaSizeReg::Get()
      .ReadFrom(&(*gamma_mmio_))
      .set_vsize(height_)
      .set_hsize(width_)
      .WriteTo(&(*gamma_mmio_));
  return ZX_OK;
}

void Gamma::PrintRegisters() {
  ZX_DEBUG_ASSERT(initialized_);
  zxlogf(INFO, "Dumping Gamma Registers\n");
  zxlogf(INFO, "######################\n\n");
  zxlogf(INFO, "GAMMA_EN = 0x%x\n", gamma_mmio_->Read32(GAMMA_EN));
  zxlogf(INFO, "GAMMA_CFG = 0x%x\n", gamma_mmio_->Read32(GAMMA_CFG));
  zxlogf(INFO, "GAMMA_SIZE = 0x%x\n", gamma_mmio_->Read32(GAMMA_SIZE));
  zxlogf(INFO, "######################\n\n");
}

}  // namespace mt8167s_display
