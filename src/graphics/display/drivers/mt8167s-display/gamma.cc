// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gamma.h"

#include <fbl/alloc_checker.h>

namespace mt8167s_display {

zx_status_t Gamma::Init(ddk::PDev& pdev) {
  if (initialized_) {
    return ZX_OK;
  }

  // Map GAMMA MMIO
  zx_status_t status = pdev.MapMmio(MMIO_DISP_GAMMA, &gamma_mmio_);
  if (status != ZX_OK) {
    DISP_ERROR("Could not map GAMMA mmio\n");
    return status;
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
  zxlogf(INFO, "Dumping Gamma Registers");
  zxlogf(INFO, "######################\n");
  zxlogf(INFO, "GAMMA_EN = 0x%x", gamma_mmio_->Read32(GAMMA_EN));
  zxlogf(INFO, "GAMMA_CFG = 0x%x", gamma_mmio_->Read32(GAMMA_CFG));
  zxlogf(INFO, "GAMMA_SIZE = 0x%x", gamma_mmio_->Read32(GAMMA_SIZE));
  zxlogf(INFO, "######################\n");
}

}  // namespace mt8167s_display
