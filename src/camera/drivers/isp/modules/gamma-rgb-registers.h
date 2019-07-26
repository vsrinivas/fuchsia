// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CAMERA_DRIVERS_ISP_MODULES_GAMMA_RGB_REGISTERS_H_
#define SRC_CAMERA_DRIVERS_ISP_MODULES_GAMMA_RGB_REGISTERS_H_

#include "../mali-009/pingpong_regs.h"
#include "isp-block-registers.h"

namespace camera {

constexpr bool kEnableDefault = true;
constexpr uint32_t kGainDefaultR = 256;
constexpr uint32_t kGainDefaultG = 256;
constexpr uint32_t kGainDefaultB = 256;
constexpr uint32_t kOffsetDefaultR = 0;
constexpr uint32_t kOffsetDefaultG = 0;
constexpr uint32_t kOffsetDefaultB = 0;

struct GammaRgbRegisterDefs {
  static hwreg::RegisterAddr<GammaRgb_Enable> enable() { return GammaRgb_Enable::Get(0x00); }
  static hwreg::RegisterAddr<GammaRgb_Gain> gain_rg() { return GammaRgb_Gain::Get(0x04); }
  static hwreg::RegisterAddr<GammaRgb_GainB> gain_b() { return GammaRgb_GainB::Get(0x08); }
  static hwreg::RegisterAddr<GammaRgb_Offset> offset_rg() { return GammaRgb_Offset::Get(0x0c); }
  static hwreg::RegisterAddr<GammaRgb_OffsetB> offset_b() { return GammaRgb_OffsetB::Get(0x10); }
};

// Logical data structure to store registers that are used by the GammaRgb
// Module.
class GammaRgbRegisters : IspBlockRegisters<GammaRgbRegisterDefs> {
 public:
  // Creates a new |GammaRgbRegisters| instance with knowledge of register
  // addresses and where to write.
  GammaRgbRegisters(ddk::MmioView local_mmio)
      : IspBlockRegisters(local_mmio, GammaRgbRegisterDefs()),
        enable_(kEnableDefault),
        gain_r_(kGainDefaultR),
        gain_g_(kGainDefaultG),
        gain_b_(kGainDefaultB),
        offset_r_(kOffsetDefaultR),
        offset_g_(kOffsetDefaultG),
        offset_b_(kOffsetDefaultB) {}

  ~GammaRgbRegisters() override {}

  // Initializes the registers with their starting values.
  void Init() override { IspBlockRegisters::Init(); }

  // Sets the enable bit in the registers.
  // |enable|: Gamma enable: 0=off 1=on.
  void SetEnable(bool enable) { enable_ = enable; }

  // Sets the gain_r value in the registers.
  // |gain_r|: Gain applied to the R channel.
  void SetGainR(uint32_t gain_r) { gain_r_ = gain_r; }

  // Sets the gain_g value in the registers.
  // |gain_g|: Gain applied to the G channel.
  void SetGainG(uint32_t gain_g) { gain_g_ = gain_g; }

  // Sets the gain_b value in the registers.
  // |gain_b|: Gain applied to the B channel.
  void SetGainB(uint32_t gain_b) { gain_b_ = gain_b; }

  // Sets the offset_r value in the registers.
  // |offset_r|: Offset subtracted from the R channel.
  void SetOffsetR(uint32_t offset_r) { offset_r_ = offset_r; }

  // Sets the offset_g value in the registers.
  // |offset_g|: Offset subtracted from the G channel.
  void SetOffsetG(uint32_t offset_g) { offset_g_ = offset_g; }

  // Sets the offset_b value in the registers.
  // |offset_b|: Offset subtracted from the B channel.
  void SetOffsetB(uint32_t offset_b) { offset_b_ = offset_b; }

  // Writes the values currently set in the data structure to the registers.
  void WriteRegisters() override;

 private:
  bool enable_;
  uint32_t gain_r_;
  uint32_t gain_g_;
  uint32_t gain_b_;
  uint32_t offset_r_;
  uint32_t offset_g_;
  uint32_t offset_b_;
};

}  // namespace camera

#endif  // SRC_CAMERA_DRIVERS_ISP_MODULES_GAMMA_RGB_REGISTERS_H_
