// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_LIB_AMLOGIC_INCLUDE_SOC_AML_A5_A5_HIU_REGS_H_
#define SRC_DEVICES_LIB_AMLOGIC_INCLUDE_SOC_AML_A5_A5_HIU_REGS_H_

#include <hwreg/bitfields.h>
#include <soc/aml-a5/a5-hw.h>

namespace amlogic_clock::a5 {

class HifiPllCtrl : public hwreg::RegisterBase<HifiPllCtrl, uint32_t> {
 public:
  DEF_BIT(31, lock);
  DEF_BIT(29, reset);
  DEF_BIT(28, enable);
  DEF_FIELD(17, 16, od);
  DEF_FIELD(14, 10, n);
  DEF_FIELD(7, 0, m);

  static auto Get() { return hwreg::RegisterAddr<HifiPllCtrl>(0x0); }
};

class HifiPllCtrl2 : public hwreg::RegisterBase<HifiPllCtrl2, uint32_t> {
 public:
  DEF_FIELD(18, 0, frac);

  static auto Get() { return hwreg::RegisterAddr<HifiPllCtrl2>(0x4); }
};

class MpllCtrl : public hwreg::RegisterBase<MpllCtrl, uint32_t> {
 public:
  DEF_BIT(31, enable);
  DEF_BIT(30, sdm_en);
  DEF_FIELD(28, 20, n_in);
  DEF_FIELD(13, 0, sdm_in);

  static auto Get() { return hwreg::RegisterAddr<MpllCtrl>(0x0); }
};

}  // namespace amlogic_clock::a5

#endif  // SRC_DEVICES_LIB_AMLOGIC_INCLUDE_SOC_AML_A5_A5_HIU_REGS_H_
