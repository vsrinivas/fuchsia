// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_LIB_AS370_INCLUDE_SOC_AS370_AS370_CLK_REGS_H_
#define SRC_DEVICES_LIB_AS370_INCLUDE_SOC_AS370_AS370_CLK_REGS_H_

#include <zircon/types.h>

#include <hwreg/bitfields.h>

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

// CPU Sybsystem Registers.

class CPU_WRP_PLL_REG_ctrl : public hwreg::RegisterBase<CPU_WRP_PLL_REG_ctrl, uint32_t> {
 public:
  DEF_BIT(22, FRAC_READY);
  DEF_BIT(21, READY_BP);
  DEF_FIELD(20, 19, MODE);
  DEF_FIELD(18, 8, DN);
  DEF_FIELD(7, 2, DM);
  DEF_BIT(1, RESETN);
  DEF_BIT(0, PD);
  static auto Get() { return hwreg::RegisterAddr<CPU_WRP_PLL_REG_ctrl>(0x2000); }
};

struct CPU_WRP_PLL_REG_ctrl1 : public hwreg::RegisterBase<CPU_WRP_PLL_REG_ctrl1, uint32_t> {
  DEF_FIELD(23, 0, FRAC);
  static auto Get() { return hwreg::RegisterAddr<CPU_WRP_PLL_REG_ctrl1>(0x2004); }
};

struct CPU_WRP_PLL_REG_ctrl2 : public hwreg::RegisterBase<CPU_WRP_PLL_REG_ctrl2, uint32_t> {
  DEF_FIELD(10, 0, SSRATE);
  static auto Get() { return hwreg::RegisterAddr<CPU_WRP_PLL_REG_ctrl2>(0x2008); }
};

struct CPU_WRP_PLL_REG_ctrl3 : public hwreg::RegisterBase<CPU_WRP_PLL_REG_ctrl3, uint32_t> {
  DEF_FIELD(31, 29, DP1);
  DEF_BIT(28, PDDP1);
  DEF_FIELD(27, 25, DP);
  DEF_BIT(24, PDDP);
  DEF_FIELD(23, 0, SLOPE);
  static auto Get() { return hwreg::RegisterAddr<CPU_WRP_PLL_REG_ctrl3>(0x200c); }
};

#endif  // SRC_DEVICES_LIB_AS370_INCLUDE_SOC_AS370_AS370_CLK_REGS_H_
