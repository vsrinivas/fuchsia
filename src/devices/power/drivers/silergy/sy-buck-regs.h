// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_POWER_DRIVERS_SILERGY_SY_BUCK_REGS_H_
#define SRC_DEVICES_POWER_DRIVERS_SILERGY_SY_BUCK_REGS_H_

#include <hwreg/i2c.h>

namespace silergy {

// clang-format off
enum class Vsel : uint32_t {
  Vsel0 = 0,
  Vsel1 = 1,
};

constexpr uint32_t kControlOffset = 0x02;
constexpr uint32_t kId1Offset     = 0x03;
constexpr uint32_t kId2Offset     = 0x04;
constexpr uint32_t kPGoodOffset   = 0x05;
// clang-format on

class VselReg : public hwreg::I2cRegisterBase<VselReg, uint8_t, 1> {
 public:
  DEF_BIT(7, buck_en);
  DEF_BIT(6, mode);
  DEF_FIELD(5, 0, n_sel);
  static auto Get(const Vsel v) {
    const uint32_t n = static_cast<uint32_t>(v);
    return hwreg::I2cRegisterAddr<VselReg>(n);
  }
};

class ControlReg : public hwreg::I2cRegisterBase<ControlReg, uint8_t, 1> {
 public:
  DEF_BIT(7, output_discharge);
  DEF_FIELD(6, 4, slew_rate);
  static auto Get() { return hwreg::I2cRegisterAddr<ControlReg>(kControlOffset); }
};

class Id1Reg : public hwreg::I2cRegisterBase<Id1Reg, uint8_t, 1> {
 public:
  DEF_FIELD(7, 5, vendor);
  DEF_FIELD(3, 0, die_id);
  static auto Get() { return hwreg::I2cRegisterAddr<Id1Reg>(kId1Offset); }
};

class Id2Reg : public hwreg::I2cRegisterBase<Id2Reg, uint8_t, 1> {
 public:
  DEF_FIELD(7, 4, reserved);
  DEF_FIELD(3, 0, die_rev);
  static auto Get() { return hwreg::I2cRegisterAddr<Id2Reg>(kId2Offset); }
};

class PgoodReg : public hwreg::I2cRegisterBase<PgoodReg, uint8_t, 1> {
 public:
  DEF_BIT(7, p_good);
  static auto Get() { return hwreg::I2cRegisterAddr<PgoodReg>(kPGoodOffset); }
};

}  // namespace silergy

#endif  // SRC_DEVICES_POWER_DRIVERS_SILERGY_SY_BUCK_REGS_H_
