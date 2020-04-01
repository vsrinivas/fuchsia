// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_THERMAL_DRIVERS_VS680_THERMAL_VS680_THERMAL_REG_H_
#define SRC_DEVICES_THERMAL_DRIVERS_VS680_THERMAL_VS680_THERMAL_REG_H_

#include <hwreg/bitfields.h>

namespace thermal {


class TsenCtrl : public hwreg::RegisterBase<TsenCtrl, uint32_t> {
 public:
  static constexpr uint32_t kPSampleTemperature = 0b00;

  static auto Get() { return hwreg::RegisterAddr<TsenCtrl>(0x100); }

  DEF_BIT(1, clk_en);
  DEF_BIT(0, ena);
};

class TsenStatus : public hwreg::RegisterBase<TsenStatus, uint32_t> {
 public:
  static auto Get() { return hwreg::RegisterAddr<TsenStatus>(0x104); }

  DEF_BIT(1, int_en);
  DEF_BIT(0, data_rdy);
};

class TsenData : public hwreg::RegisterBase<TsenData, uint32_t> {
 public:
  static auto Get() { return hwreg::RegisterAddr<TsenData>(0x108); }

  DEF_FIELD(9, 0, data);
};

class TsenChkCtrl : public hwreg::RegisterBase<TsenChkCtrl, uint32_t> {
 public:
  static auto Get() { return hwreg::RegisterAddr<TsenChkCtrl>(0x10c); }

  DEF_BIT(20, overheat_sel);
  DEF_FIELD(19, 10, data_min);
  DEF_FIELD(9, 0, data_max);
};

class TsenDataStatus : public hwreg::RegisterBase<TsenDataStatus, uint32_t> {
 public:
  static auto Get() { return hwreg::RegisterAddr<TsenDataStatus>(0x110); }

  DEF_BIT(1, min_fail);
  DEF_BIT(0, max_fail);
};

}  // namespace thermal

#endif  // SRC_DEVICES_THERMAL_DRIVERS_VS680_THERMAL_VS680_THERMAL_REG_H_
