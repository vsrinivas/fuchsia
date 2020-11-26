// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ccorr.h"

#include <fbl/alloc_checker.h>

namespace mt8167s_display {

zx_status_t Ccorr::Init(ddk::PDev& pdev) {
  if (initialized_) {
    return ZX_OK;
  }

  // Map Ccorr MMIO
  zx_status_t status = pdev.MapMmio(MMIO_DISP_CCORR, &ccorr_mmio_);
  if (status != ZX_OK) {
    DISP_ERROR("Could not map CCORR mmio\n");
    return status;
  }

  // CCORR is ready to be used
  initialized_ = true;
  return ZX_OK;
}

zx_status_t Ccorr::Config() {
  ZX_DEBUG_ASSERT(initialized_);
  // Set to bypass mode
  CcorrEnReg::Get().ReadFrom(&(*ccorr_mmio_)).set_enable(1).WriteTo(&(*ccorr_mmio_));
  CcorrSizeReg::Get()
      .ReadFrom(&(*ccorr_mmio_))
      .set_vsize(height_)
      .set_hsize(width_)
      .WriteTo(&(*ccorr_mmio_));
  CcorrCfgReg::Get().ReadFrom(&(*ccorr_mmio_)).set_relay(1).WriteTo(&(*ccorr_mmio_));
  return ZX_OK;
}

void Ccorr::PrintRegisters() {
  ZX_DEBUG_ASSERT(initialized_);
  zxlogf(INFO, "Dumping Ccorr Registers");
  zxlogf(INFO, "######################\n");
  zxlogf(INFO, "CCORR_EN = 0x%x", ccorr_mmio_->Read32(CCORR_EN));
  zxlogf(INFO, "CCORR_CFG = 0x%x", ccorr_mmio_->Read32(CCORR_CFG));
  zxlogf(INFO, "CCORR_SIZE = 0x%x", ccorr_mmio_->Read32(CCORR_SIZE));
  zxlogf(INFO, "######################\n");
}

}  // namespace mt8167s_display
