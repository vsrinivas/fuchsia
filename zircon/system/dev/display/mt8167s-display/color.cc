// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "color.h"

#include <fbl/alloc_checker.h>

namespace mt8167s_display {

namespace {
constexpr uint32_t kColorMainCfg = 0x200032bc;
}  // namespace

zx_status_t Color::Init(zx_device_t* parent) {
  if (initialized_) {
    return ZX_OK;
  }

  zx_status_t status = device_get_protocol(parent, ZX_PROTOCOL_PDEV, &pdev_);
  if (status != ZX_OK) {
    return status;
  }

  // Map COLOR MMIO
  mmio_buffer_t mmio;
  status = pdev_map_mmio_buffer(&pdev_, MMIO_DISP_COLOR, ZX_CACHE_POLICY_UNCACHED_DEVICE, &mmio);
  if (status != ZX_OK) {
    DISP_ERROR("Could not map COLOR mmio\n");
    return status;
  }
  fbl::AllocChecker ac;
  color_mmio_ = fbl::make_unique_checked<ddk::MmioBuffer>(&ac, mmio);
  if (!ac.check()) {
    DISP_ERROR("Could not map COLOR mmio\n");
    return ZX_ERR_NO_MEMORY;
  }

  // COLOR is ready to be used
  initialized_ = true;
  return ZX_OK;
}

zx_status_t Color::Config() {
  ZX_DEBUG_ASSERT(initialized_);
  ColorWidthReg::Get().ReadFrom(&(*color_mmio_)).set_width(width_).WriteTo(&(*color_mmio_));
  ColorHeightReg::Get().ReadFrom(&(*color_mmio_)).set_height(height_).WriteTo(&(*color_mmio_));
  ColorMainReg::Get().FromValue(kColorMainCfg).WriteTo(&(*color_mmio_));
  ColorStartReg::Get()
      .ReadFrom(&(*color_mmio_))
      .set_out_sel(1)
      .set_start(1)
      .WriteTo(&(*color_mmio_));
  ColorCm1EnReg::Get().ReadFrom(&(*color_mmio_)).set_front_en(0).WriteTo(&(*color_mmio_));
  ColorCm2EnReg::Get().ReadFrom(&(*color_mmio_)).set_back_en(0).WriteTo(&(*color_mmio_));
  return ZX_OK;
}

void Color::PrintRegisters() {
  ZX_DEBUG_ASSERT(initialized_);
  zxlogf(INFO, "Dumping Color Registers\n");
  zxlogf(INFO, "######################\n\n");
  zxlogf(INFO, "COLOR_MAIN = 0x%x\n", color_mmio_->Read32(COLOR_MAIN));
  zxlogf(INFO, "COLOR_START = 0x%x\n", color_mmio_->Read32(COLOR_START));
  zxlogf(INFO, "COLOR_WIDTH = 0x%x\n", color_mmio_->Read32(COLOR_WIDTH));
  zxlogf(INFO, "COLOR_HEIGHT = 0x%x\n", color_mmio_->Read32(COLOR_HEIGHT));
  zxlogf(INFO, "COLOR_CM1_EN = 0x%x\n", color_mmio_->Read32(COLOR_CM1_EN));
  zxlogf(INFO, "COLOR_CM2_EN = 0x%x\n", color_mmio_->Read32(COLOR_CM2_EN));
  zxlogf(INFO, "######################\n\n");
}

}  // namespace mt8167s_display
