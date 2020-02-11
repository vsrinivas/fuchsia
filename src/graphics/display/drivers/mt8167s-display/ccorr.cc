// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ccorr.h"

#include <fbl/alloc_checker.h>

namespace mt8167s_display {

zx_status_t Ccorr::Init(zx_device_t* parent) {
  if (initialized_) {
    return ZX_OK;
  }

  zx_status_t status = device_get_protocol(parent, ZX_PROTOCOL_PDEV, &pdev_);
  if (status != ZX_OK) {
    return status;
  }

  // Map Ccorr MMIO
  mmio_buffer_t mmio;
  status = pdev_map_mmio_buffer(&pdev_, MMIO_DISP_CCORR, ZX_CACHE_POLICY_UNCACHED_DEVICE, &mmio);
  if (status != ZX_OK) {
    DISP_ERROR("Could not map CCORR mmio\n");
    return status;
  }
  fbl::AllocChecker ac;
  ccorr_mmio_ = fbl::make_unique_checked<ddk::MmioBuffer>(&ac, mmio);
  if (!ac.check()) {
    DISP_ERROR("Could not map CCORR mmio\n");
    return ZX_ERR_NO_MEMORY;
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
  zxlogf(INFO, "Dumping Ccorr Registers\n");
  zxlogf(INFO, "######################\n\n");
  zxlogf(INFO, "CCORR_EN = 0x%x\n", ccorr_mmio_->Read32(CCORR_EN));
  zxlogf(INFO, "CCORR_CFG = 0x%x\n", ccorr_mmio_->Read32(CCORR_CFG));
  zxlogf(INFO, "CCORR_SIZE = 0x%x\n", ccorr_mmio_->Read32(CCORR_SIZE));
  zxlogf(INFO, "######################\n\n");
}

}  // namespace mt8167s_display
