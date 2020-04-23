// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_LIB_MT8167_INCLUDE_SOC_MT8167_MT8167_POWER_REGS_H_
#define SRC_DEVICES_LIB_MT8167_INCLUDE_SOC_MT8167_MT8167_POWER_REGS_H_

#include <zircon/types.h>

#include <hwreg/bitfields.h>
#include <soc/mt8167/mt8167-hw.h>

// PMIC WRAP REGISTERS
#define PMIC_WRAP_WACS2_CMD_OFFSET 0x00A0
#define PMIC_WRAP_WACS2_RDATA_OFFSET 0x00A4
#define PMIC_WRAP_WACS2_VLDCLR_OFFSET 0x00A8

class PmicWacs2Cmd : public hwreg::RegisterBase<PmicWacs2Cmd, uint32_t> {
 public:
  static auto Get() { return hwreg::RegisterAddr<PmicWacs2Cmd>(PMIC_WRAP_WACS2_CMD_OFFSET); }
  // Data reag/to write in the above reg address.
  DEF_FIELD(15, 0, wacs2_data);
  // Register address in pmic
  DEF_FIELD(30, 16, wacs2_addr);
  // Read/Write direction
  DEF_BIT(31, wacs2_write);
};

class PmicWacs2RData : public hwreg::RegisterBase<PmicWacs2RData, uint32_t> {
 public:
  static constexpr uint32_t kFsmStateIdle = 0x0;
  static constexpr uint32_t kFsmStateReq = 0x2;
  static constexpr uint32_t kFsmStateWfIdle = 0x4;
  static constexpr uint32_t kFsmStateWfVldClear = 0x6;

  static auto Get() { return hwreg::RegisterAddr<PmicWacs2RData>(PMIC_WRAP_WACS2_RDATA_OFFSET); }

  // Check valid flag befor reading this data
  DEF_FIELD(15, 0, wacs2_rdata);
  // Current fsm state
  DEF_FIELD(18, 16, wacs2_fsm);
  // Is there a req awaiting grant?
  DEF_BIT(19, wacs2_req);
  // Is sync module idle?
  DEF_BIT(20, sync_idle);
  // Is Init done?
  DEF_BIT(21, init_done);
  // Is pmic_wrap idle?
  DEF_BIT(22, sys_idle);
};

class PmicWacs2VldClear : public hwreg::RegisterBase<PmicWacs2VldClear, uint32_t> {
 public:
  static auto Get() {
    return hwreg::RegisterAddr<PmicWacs2VldClear>(PMIC_WRAP_WACS2_VLDCLR_OFFSET);
  }
  DEF_BIT(0, wacs2_vldclr);
};

#endif  // SRC_DEVICES_LIB_MT8167_INCLUDE_SOC_MT8167_MT8167_POWER_REGS_H_
