// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_DEV_PWM_AML_PWM_AML_PWM_REGS_H_
#define ZIRCON_SYSTEM_DEV_PWM_AML_PWM_AML_PWM_REGS_H_

#include <zircon/types.h>

#include <hwreg/bitfields.h>
#include <soc/aml-common/aml-pwm-regs.h>

namespace pwm {

using namespace aml_pwm;

class DutyCycleReg : public hwreg::RegisterBase<DutyCycleReg, uint32_t> {
 public:
  DEF_FIELD(31, 16, high);
  DEF_FIELD(15, 0, low);

  static auto GetA() { return hwreg::RegisterAddr<DutyCycleReg>(kAOffset); }
  static auto GetB() { return hwreg::RegisterAddr<DutyCycleReg>(kBOffset); }
  static auto GetA2() { return hwreg::RegisterAddr<DutyCycleReg>(kA2Offset); }
  static auto GetB2() { return hwreg::RegisterAddr<DutyCycleReg>(kB2Offset); }
};

class MiscReg : public hwreg::RegisterBase<MiscReg, uint32_t> {
 public:
  DEF_BIT(31, hiz_b);
  DEF_BIT(30, hiz_a);
  DEF_BIT(29, constant_en_b);
  DEF_BIT(28, constant_en_a);
  DEF_BIT(27, inv_en_b);
  DEF_BIT(26, inv_en_a);
  DEF_BIT(25, en_a2);
  DEF_BIT(24, en_b2);
  DEF_BIT(23, clk_en_b);
  DEF_FIELD(22, 16, clk_div_b);
  DEF_BIT(15, clk_en_a);
  DEF_FIELD(14, 8, clk_div_a);
  DEF_FIELD(7, 6, clk_sel_b);
  DEF_FIELD(5, 4, clk_sel_a);
  DEF_BIT(3, ds_en_b);
  DEF_BIT(2, ds_en_a);
  DEF_BIT(1, en_b);
  DEF_BIT(0, en_a);

  static auto Get() { return hwreg::RegisterAddr<MiscReg>(kMiscOffset); }
};

class DeltaSigmaReg : public hwreg::RegisterBase<DeltaSigmaReg, uint32_t> {
 public:
  DEF_FIELD(31, 16, b);
  DEF_FIELD(15, 0, a);

  static auto Get() { return hwreg::RegisterAddr<DeltaSigmaReg>(kDSOffset); }
};

class TimeReg : public hwreg::RegisterBase<TimeReg, uint32_t> {
 public:
  DEF_FIELD(31, 24, a1);
  DEF_FIELD(23, 16, a2);
  DEF_FIELD(15, 8, b1);
  DEF_FIELD(7, 0, b2);

  static auto Get() { return hwreg::RegisterAddr<TimeReg>(kTimeOffset); }
};

class BlinkReg : public hwreg::RegisterBase<BlinkReg, uint32_t> {
 public:
  DEF_BIT(9, enable_b);
  DEF_BIT(8, enable_a);
  DEF_FIELD(7, 4, times_b);
  DEF_FIELD(3, 0, times_a);

  static auto Get() { return hwreg::RegisterAddr<BlinkReg>(kBlinkOffset); }
};

}  // namespace pwm

#endif  // ZIRCON_SYSTEM_DEV_PWM_AML_PWM_AML_PWM_REGS_H_
