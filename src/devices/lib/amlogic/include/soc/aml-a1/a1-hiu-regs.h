// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_LIB_AMLOGIC_INCLUDE_SOC_AML_A1_A1_HIU_REGS_H_
#define SRC_DEVICES_LIB_AMLOGIC_INCLUDE_SOC_AML_A1_A1_HIU_REGS_H_

#include <hwreg/bitfields.h>
#include <soc/aml-a1/a1-hw.h>

namespace amlogic_clock::a1 {

// fit for SysPll, HifiPll
class PllCtrl0 : public hwreg::RegisterBase<PllCtrl0, uint32_t> {
 public:
  static constexpr uint32_t offset = 0x0 << 2;

  DEF_BIT(28, enable);
  DEF_FIELD(14, 10, n);
  DEF_FIELD(7, 0, m);

  static auto Get() { return hwreg::RegisterAddr<PllCtrl0>(offset); }
};

class PllCtrl1 : public hwreg::RegisterBase<PllCtrl1, uint32_t> {
 public:
  static constexpr uint32_t offset = 0x1 << 2;

  DEF_FIELD(18, 0, frac);

  static auto Get() { return hwreg::RegisterAddr<PllCtrl1>(offset); }
};

class PllSts : public hwreg::RegisterBase<PllSts, uint32_t> {
 public:
  DEF_BIT(31, lock);

  static auto Get() { return hwreg::RegisterAddr<PllSts>(0x14); }
};

}  // namespace amlogic_clock::a1

#endif  // SRC_DEVICES_LIB_AMLOGIC_INCLUDE_SOC_AML_A1_A1_HIU_REGS_H_
