// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "aal.h"

#include <fbl/alloc_checker.h>

namespace mt8167s_display {

zx_status_t Aal::Init(ddk::PDev& pdev) {
  if (initialized_) {
    return ZX_OK;
  }

  // Map Aal MMIO
  zx_status_t status = pdev.MapMmio(MMIO_DISP_AAL, &aal_mmio_);
  if (status != ZX_OK) {
    DISP_ERROR("Could not map AAL mmio\n");
    return status;
  }

  // AAL is ready to be used
  initialized_ = true;
  return ZX_OK;
}

zx_status_t Aal::Config() {
  // Set to bypass mode
  ZX_DEBUG_ASSERT(initialized_);
  AalEnReg::Get().ReadFrom(&(*aal_mmio_)).set_enable(1).WriteTo(&(*aal_mmio_));
  AalSizeReg::Get()
      .ReadFrom(&(*aal_mmio_))
      .set_vsize(height_)
      .set_hsize(width_)
      .WriteTo(&(*aal_mmio_));
  AalCfgReg::Get().ReadFrom(&(*aal_mmio_)).set_relay(1).WriteTo(&(*aal_mmio_));
  return ZX_OK;
}

void Aal::PrintRegisters() {
  ZX_DEBUG_ASSERT(initialized_);
  zxlogf(INFO, "Dumping Aal Registers");
  zxlogf(INFO, "######################\n");
  zxlogf(INFO, "AAL_EN = 0x%x", aal_mmio_->Read32(AAL_EN));
  zxlogf(INFO, "AAL_CFG = 0x%x", aal_mmio_->Read32(AAL_CFG));
  zxlogf(INFO, "AAL_SIZE = 0x%x", aal_mmio_->Read32(AAL_SIZE));
  zxlogf(INFO, "######################\n");
}

}  // namespace mt8167s_display
