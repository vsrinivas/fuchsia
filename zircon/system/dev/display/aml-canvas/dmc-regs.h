// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_DEV_DISPLAY_AML_CANVAS_DMC_REGS_H_
#define ZIRCON_SYSTEM_DEV_DISPLAY_AML_CANVAS_DMC_REGS_H_

#include <hw/reg.h>
#include <hwreg/bitfields.h>

namespace aml_canvas {

constexpr uint32_t kDmcCavLutDataL = (0x12 << 2);
constexpr uint32_t kDmcCavLutDataH = (0x13 << 2);
constexpr uint32_t kDmcCavLutAddr = (0x14 << 2);
constexpr uint32_t kDmcCavMaxRegAddr = kDmcCavLutAddr;

// DMC_CAV_LUT_DATAL
class CanvasLutDataLow : public hwreg::RegisterBase<CanvasLutDataLow, uint32_t> {
 public:
  DEF_FIELD(31, 29, dmc_cav_width);
  DEF_FIELD(28, 0, dmc_cav_addr);

  static auto Get() { return hwreg::RegisterAddr<CanvasLutDataLow>(kDmcCavLutDataL); }

  void SetDmcCavWidth(uint32_t width) { set_dmc_cav_width(width & kDmcCavWidthLmask_); }

 private:
  // Mask to extract the bits of dmc_cav_width encoded in CanvasLutDataLow.
  // The remaining bits go in CanvasLutDataHigh.
  static constexpr uint32_t kDmcCavWidthLmask_ = 7;
};

// DMC_CAV_LUT_DATAH
class CanvasLutDataHigh : public hwreg::RegisterBase<CanvasLutDataHigh, uint32_t> {
 public:
  DEF_FIELD(29, 26, dmc_cav_endianness);
  DEF_FIELD(25, 24, dmc_cav_blkmode);
  DEF_BIT(23, dmc_cav_ywrap);
  DEF_BIT(22, dmc_cav_xwrap);
  DEF_FIELD(21, 9, dmc_cav_height);
  DEF_FIELD(8, 0, dmc_cav_width);

  static auto Get() { return hwreg::RegisterAddr<CanvasLutDataHigh>(kDmcCavLutDataH); }

  void SetDmcCavWidth(uint32_t width) { set_dmc_cav_width(width >> kDmcCavWidthLwidth_); }

  static constexpr uint32_t kDmcCavYwrap = (1 << 23);
  static constexpr uint32_t kDmcCavXwrap = (1 << 22);

 private:
  // Number of bits of dmc_cav_width encoded in CanvasLutDataLow.
  static constexpr uint32_t kDmcCavWidthLwidth_ = 3;
};

// DMC_CAV_LUT_ADDR
class CanvasLutAddr : public hwreg::RegisterBase<CanvasLutAddr, uint32_t> {
 public:
  DEF_BIT(9, dmc_cav_addr_wr);
  DEF_BIT(8, dmc_cav_addr_rd);
  DEF_FIELD(7, 0, dmc_cav_addr_index);

  static auto Get() { return hwreg::RegisterAddr<CanvasLutAddr>(kDmcCavLutAddr); }
};

}  // namespace aml_canvas

#endif  // ZIRCON_SYSTEM_DEV_DISPLAY_AML_CANVAS_DMC_REGS_H_
