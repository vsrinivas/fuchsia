// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <hwreg/bitfields.h>

namespace thermal {

class PvtCtrl : public hwreg::RegisterBase<PvtCtrl, uint32_t> {
 public:
  static auto Get() { return hwreg::RegisterAddr<PvtCtrl>(0x10); }

  DEF_BIT(9, pmos_sel);
  DEF_BIT(8, nmos_sel);
  DEF_BIT(3, voltage_sel);
  DEF_BIT(2, temperature_sel);
  DEF_BIT(1, enable);
  DEF_BIT(0, power_down);
};

class PvtStatus : public hwreg::RegisterBase<PvtStatus, uint32_t> {
 public:
  static auto Get() { return hwreg::RegisterAddr<PvtStatus>(0x14); }

  DEF_BIT(12, eoc);
  DEF_FIELD(11, 0, data);
};

}  // namespace thermal
