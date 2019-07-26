// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <hwreg/bitfields.h>
#include <zircon/types.h>

// GBL Global Control Registers.

class sysPll_ctrl : public hwreg::RegisterBase<sysPll_ctrl, uint32_t> {
 public:
  DEF_BIT(22, FRAC_READY);
  DEF_BIT(21, READY_BP);
  DEF_FIELD(20, 19, MODE);
  DEF_FIELD(18, 8, DN);
  DEF_FIELD(7, 2, DM);
  DEF_BIT(1, RESETN);
  DEF_BIT(0, PD);
  static auto Get() { return hwreg::RegisterAddr<sysPll_ctrl>(0x0088); }
};

class avioSysClk_ctrl : public hwreg::RegisterBase<avioSysClk_ctrl, uint32_t> {
 public:
  DEF_FIELD(9, 7, ClkSel);
  DEF_BIT(6, ClkD3Switch);
  DEF_BIT(5, ClkSwitch);
  DEF_BIT(4, ClkPllSwitch);
  DEF_FIELD(3, 1, ClkPllSel);
  DEF_BIT(0, ClkEn);
  static auto Get() { return hwreg::RegisterAddr<avioSysClk_ctrl>(0x0530); }
};
