// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_CLOCK_DRIVERS_VS680_CLK_VS680_CLK_REG_H_
#define SRC_DEVICES_CLOCK_DRIVERS_VS680_CLK_VS680_CLK_REG_H_

#include <hwreg/bitfields.h>

namespace clk {

class PllCtrlA : public hwreg::RegisterBase<PllCtrlA, uint32_t> {
 public:
  static auto Get() { return hwreg::RegisterAddr<PllCtrlA>(0x00); }

  DEF_FIELD(5, 3, range);  // PLL filter range
  DEF_BIT(1, bypass);
  DEF_BIT(0, reset);
};

class PllCtrlC : public hwreg::RegisterBase<PllCtrlC, uint32_t> {
 public:
  static auto Get() { return hwreg::RegisterAddr<PllCtrlC>(0x08); }

  DEF_FIELD(8, 0, divr);  // Reference divider
};

class PllCtrlD : public hwreg::RegisterBase<PllCtrlD, uint32_t> {
 public:
  static auto Get() { return hwreg::RegisterAddr<PllCtrlD>(0x0c); }

  DEF_FIELD(8, 0, divfi);  // Integer divider
};

class PllCtrlE : public hwreg::RegisterBase<PllCtrlE, uint32_t> {
 public:
  static auto Get() { return hwreg::RegisterAddr<PllCtrlE>(0x10); }

  DEF_FIELD(23, 0, divff);  // Fractional divider
};

class PllCtrlF : public hwreg::RegisterBase<PllCtrlF, uint32_t> {
 public:
  static auto Get() { return hwreg::RegisterAddr<PllCtrlF>(0x14); }

  DEF_FIELD(4, 0, divq);  // Output divider for PLLOUT
};

class PllCtrlG : public hwreg::RegisterBase<PllCtrlG, uint32_t> {
 public:
  static auto Get() { return hwreg::RegisterAddr<PllCtrlG>(0x18); }

  DEF_FIELD(2, 0, divqf);  // Output divider for PLLOUT1
};

class PllStatus : public hwreg::RegisterBase<PllStatus, uint32_t> {
 public:
  static auto Get() { return hwreg::RegisterAddr<PllStatus>(0x1c); }

  DEF_BIT(0, lock);  // Output lock detection
};

class ClockMux : public hwreg::RegisterBase<ClockMux, uint32_t> {
 public:
  enum ClkSel : uint32_t {
    kDiv2 = 1,
    kDiv4 = 2,
    kDiv6 = 3,
    kDiv8 = 4,
    kDiv12 = 5,
    kDiv24 = 6,
    kDiv48 = 7,
  };

  static auto Get() { return hwreg::RegisterAddr<ClockMux>(0); }

  DEF_FIELD(9, 7, clk_sel);      // Divide input, see values above.
  DEF_BIT(6, clk_d3_switch);     // 0 - use clk_switch selection, 1 - divide input by 3
  DEF_BIT(5, clk_switch);        // 0 - don't divide input, 1 - divide input using clk_sel selection
  DEF_BIT(4, clk_pll_switch);    // 0 - use SYSPLL0 input, 1 - use clk_pll_sel selection
  DEF_FIELD(3, 1, clk_pll_sel);  // Selects between SYSPLL1/F/2/F/SYSPLL0F.
  DEF_BIT(0, clk_en);            // Clock enable, takes priority over all other settings.
};

}  // namespace clk

#endif  // SRC_DEVICES_CLOCK_DRIVERS_VS680_CLK_VS680_CLK_REG_H_
