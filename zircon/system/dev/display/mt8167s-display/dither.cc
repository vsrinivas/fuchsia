// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dither.h"

#include <fbl/alloc_checker.h>

namespace mt8167s_display {

namespace {
constexpr uint32_t kDitherReg0Default = 0x00000001;
constexpr uint32_t kDitherReg5Default = 0x00000000;
constexpr uint32_t kDitherReg6Default = 0x00003004;
constexpr uint32_t kDitherReg7Default = 0x00000000;
constexpr uint32_t kDitherReg8Default = 0x00000000;
constexpr uint32_t kDitherReg9Default = 0x00000000;
constexpr uint32_t kDitherReg10Default = 0x00000000;
constexpr uint32_t kDitherReg11Default = 0x00000000;
constexpr uint32_t kDitherReg12Default = 0x00000011;
constexpr uint32_t kDitherReg13Default = 0x00000000;
constexpr uint32_t kDitherReg14Default = 0x00000000;
constexpr uint32_t kDitherReg15Default = 0x20200001;
constexpr uint32_t kDitherReg16Default = 0x20202020;
}  // namespace

zx_status_t Dither::Init(zx_device_t* parent) {
  if (initialized_) {
    return ZX_OK;
  }

  zx_status_t status = device_get_protocol(parent, ZX_PROTOCOL_PDEV, &pdev_);
  if (status != ZX_OK) {
    return status;
  }

  // Map Sys Config MMIO
  mmio_buffer_t mmio;
  status = pdev_map_mmio_buffer(&pdev_, MMIO_DISP_DITHER, ZX_CACHE_POLICY_UNCACHED_DEVICE, &mmio);
  if (status != ZX_OK) {
    DISP_ERROR("Could not map DITHER mmio\n");
    return status;
  }
  fbl::AllocChecker ac;
  dither_mmio_ = fbl::make_unique_checked<ddk::MmioBuffer>(&ac, mmio);
  if (!ac.check()) {
    DISP_ERROR("Could not map DITHER mmio\n");
    return ZX_ERR_NO_MEMORY;
  }

  // DITHER is ready to be used
  initialized_ = true;
  return ZX_OK;
}
zx_status_t Dither::Config() {
  ZX_DEBUG_ASSERT(initialized_);

  DitherDReg::Get(5).FromValue(kDitherReg5Default).WriteTo(&(*dither_mmio_));
  DitherDReg::Get(6).FromValue(kDitherReg6Default).WriteTo(&(*dither_mmio_));
  DitherDReg::Get(7).FromValue(kDitherReg7Default).WriteTo(&(*dither_mmio_));
  DitherDReg::Get(8).FromValue(kDitherReg8Default).WriteTo(&(*dither_mmio_));
  DitherDReg::Get(9).FromValue(kDitherReg9Default).WriteTo(&(*dither_mmio_));
  DitherDReg::Get(10).FromValue(kDitherReg10Default).WriteTo(&(*dither_mmio_));
  DitherDReg::Get(11).FromValue(kDitherReg11Default).WriteTo(&(*dither_mmio_));
  DitherDReg::Get(12).FromValue(kDitherReg12Default).WriteTo(&(*dither_mmio_));
  DitherDReg::Get(13).FromValue(kDitherReg13Default).WriteTo(&(*dither_mmio_));
  DitherDReg::Get(14).FromValue(kDitherReg14Default).WriteTo(&(*dither_mmio_));
  DitherDReg::Get(15).FromValue(kDitherReg15Default).WriteTo(&(*dither_mmio_));
  DitherDReg::Get(16).FromValue(kDitherReg16Default).WriteTo(&(*dither_mmio_));
  DitherDReg::Get(0).FromValue(kDitherReg0Default).WriteTo(&(*dither_mmio_));

  // Set to bypass mode
  DitherEnReg::Get().ReadFrom(&(*dither_mmio_)).set_enable(1).WriteTo(&(*dither_mmio_));
  DitherSizeReg::Get()
      .ReadFrom(&(*dither_mmio_))
      .set_vsize(height_)
      .set_hsize(width_)
      .WriteTo(&(*dither_mmio_));
  DitherCfgReg::Get()
      .ReadFrom(&(*dither_mmio_))
      .set_dither_engine_en(1)
      .set_relay(1)
      .WriteTo(&(*dither_mmio_));
  return ZX_OK;
}

void Dither::PrintRegisters() {
  ZX_DEBUG_ASSERT(initialized_);
  zxlogf(INFO, "Dumping Dither Registers\n");
  zxlogf(INFO, "######################\n\n");
  zxlogf(INFO, "DITHER_EN = 0x%x\n", dither_mmio_->Read32(DITHER_EN));
  zxlogf(INFO, "DITHER_CFG = 0x%x\n", dither_mmio_->Read32(DITHER_CFG));
  zxlogf(INFO, "DITHER_SIZE = 0x%x\n", dither_mmio_->Read32(DITHER_SIZE));
  zxlogf(INFO, "DISP_REG_DITHER_0 = 0x%x\n", dither_mmio_->Read32(DISP_REG_DITHER(0)));
  zxlogf(INFO, "DISP_REG_DITHER_5 = 0x%x\n", dither_mmio_->Read32(DISP_REG_DITHER(5)));
  zxlogf(INFO, "DISP_REG_DITHER_6 = 0x%x\n", dither_mmio_->Read32(DISP_REG_DITHER(6)));
  zxlogf(INFO, "DISP_REG_DITHER_7 = 0x%x\n", dither_mmio_->Read32(DISP_REG_DITHER(7)));
  zxlogf(INFO, "DISP_REG_DITHER_8 = 0x%x\n", dither_mmio_->Read32(DISP_REG_DITHER(8)));
  zxlogf(INFO, "DISP_REG_DITHER_9 = 0x%x\n", dither_mmio_->Read32(DISP_REG_DITHER(9)));
  zxlogf(INFO, "DISP_REG_DITHER_10 = 0x%x\n", dither_mmio_->Read32(DISP_REG_DITHER(10)));
  zxlogf(INFO, "DISP_REG_DITHER_11 = 0x%x\n", dither_mmio_->Read32(DISP_REG_DITHER(11)));
  zxlogf(INFO, "DISP_REG_DITHER_12 = 0x%x\n", dither_mmio_->Read32(DISP_REG_DITHER(12)));
  zxlogf(INFO, "DISP_REG_DITHER_13 = 0x%x\n", dither_mmio_->Read32(DISP_REG_DITHER(13)));
  zxlogf(INFO, "DISP_REG_DITHER_14 = 0x%x\n", dither_mmio_->Read32(DISP_REG_DITHER(14)));
  zxlogf(INFO, "DISP_REG_DITHER_15 = 0x%x\n", dither_mmio_->Read32(DISP_REG_DITHER(15)));
  zxlogf(INFO, "DISP_REG_DITHER_16 = 0x%x\n", dither_mmio_->Read32(DISP_REG_DITHER(16)));
  zxlogf(INFO, "DISP_REG_DITHER_17 = 0x%x\n", dither_mmio_->Read32(DISP_REG_DITHER(17)));
  zxlogf(INFO, "######################\n\n");
}

}  // namespace mt8167s_display
