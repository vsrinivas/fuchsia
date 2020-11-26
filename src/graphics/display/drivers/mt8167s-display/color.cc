// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "color.h"

#include <fbl/alloc_checker.h>

namespace mt8167s_display {

namespace {
constexpr uint32_t kColorMainCfg = 0x200032bc;
}  // namespace

zx_status_t Color::Init(ddk::PDev& pdev) {
  if (initialized_) {
    return ZX_OK;
  }

  // Map COLOR MMIO
  zx_status_t status = pdev.MapMmio(MMIO_DISP_COLOR, &color_mmio_);
  if (status != ZX_OK) {
    DISP_ERROR("Could not map COLOR mmio\n");
    return status;
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
  zxlogf(INFO, "Dumping Color Registers");
  zxlogf(INFO, "######################\n");
  zxlogf(INFO, "COLOR_MAIN = 0x%x", color_mmio_->Read32(COLOR_MAIN));
  zxlogf(INFO, "COLOR_START = 0x%x", color_mmio_->Read32(COLOR_START));
  zxlogf(INFO, "COLOR_WIDTH = 0x%x", color_mmio_->Read32(COLOR_WIDTH));
  zxlogf(INFO, "COLOR_HEIGHT = 0x%x", color_mmio_->Read32(COLOR_HEIGHT));
  zxlogf(INFO, "COLOR_CM1_EN = 0x%x", color_mmio_->Read32(COLOR_CM1_EN));
  zxlogf(INFO, "COLOR_CM2_EN = 0x%x", color_mmio_->Read32(COLOR_CM2_EN));
  zxlogf(INFO, "######################\n");
}

}  // namespace mt8167s_display
