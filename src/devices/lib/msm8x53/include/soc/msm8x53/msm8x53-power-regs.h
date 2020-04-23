// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_LIB_MSM8X53_INCLUDE_SOC_MSM8X53_MSM8X53_POWER_REGS_H_
#define SRC_DEVICES_LIB_MSM8X53_INCLUDE_SOC_MSM8X53_MSM8X53_POWER_REGS_H_

#include <zircon/types.h>

#include <hwreg/bitfields.h>
#include <soc/msm8x53/msm8x53-hw.h>

// PMIC WRAP MMIO Indices
static constexpr uint8_t kPmicArbCoreMmioIndex = 0;
static constexpr uint8_t kPmicArbChnlsMmioIndex = 1;
static constexpr uint8_t kPmicArbObsrvrMmioIndex = 2;
static constexpr uint8_t kPmicArbIntrMmioIndex = 3;
static constexpr uint8_t kPmicArbCnfgMmioIndex = 4;

static constexpr uint32_t kPmicArbVersionOffset = 0;
static constexpr uint32_t kPmicArbVersionTwo = 0x20010000;
static constexpr uint32_t kMaxPPIDEntries = 4096;

static constexpr uint32_t kSpmiCmdRegWriteOpcode = 0x00;
static constexpr uint32_t kSpmiCmdRegReadOpcode = 0x01;

#define PPID(sid, pid) ((sid << 8) | pid)

// V2 OFFSETS
#define PMIC_ARB_CORE_CHANNEL_INFO_OFFSET(n) (0x00000800 + 0x4 * (n))

#define PMIC_ARB_CHANNEL_CMD_OFFSET(n) (0x8000 * (n))
#define PMIC_ARB_CHANNEL_CMD_CONFIG_OFFSET(n) ((0x8000 * (n)) + 0x4)
#define PMIC_ARB_CHANNEL_CMD_STATUS_OFFSET(n) ((0x8000 * (n)) + 0x8)
#define PMIC_ARB_CHANNEL_CMD_WDATA0_OFFSET(n) ((0x8000 * (n)) + 0x10)
#define PMIC_ARB_CHANNEL_CMD_WDATA1_OFFSET(n) ((0x8000 * (n)) + 0x4 + 0x10)
#define PMIC_ARB_CHANNEL_CMD_RDATA0_OFFSET(n) ((0x8000 * (n)) + 0x18)
#define PMIC_ARB_CHANNEL_CMD_RDATA1_OFFSET(n) ((0x8000 * (n)) + 0x4 + 0x18)

class PmicArbVersion : public hwreg::RegisterBase<PmicArbVersion, uint32_t> {
 public:
  static auto Get() { return hwreg::RegisterAddr<PmicArbVersion>(kPmicArbVersionOffset); }

  DEF_FIELD(31, 0, arb_version);
};

class PmicArbCoreChannelInfo : public hwreg::RegisterBase<PmicArbCoreChannelInfo, uint32_t> {
 public:
  static auto Get(uint32_t chnl_offset) {
    return hwreg::RegisterAddr<PmicArbCoreChannelInfo>(chnl_offset);
  }

  DEF_FIELD(19, 16, slave_id);
  DEF_FIELD(15, 8, periph_id);
};

class PmicArbCoreChannelCmdInfo : public hwreg::RegisterBase<PmicArbCoreChannelCmdInfo, uint32_t> {
 public:
  static auto Get(uint32_t chnl_cmd_offset) {
    return hwreg::RegisterAddr<PmicArbCoreChannelCmdInfo>(chnl_cmd_offset);
  }
  DEF_FIELD(31, 27, opcode);
  DEF_BIT(26, priority);
  DEF_FIELD(23, 20, slave_id);
  DEF_FIELD(19, 12, periph_id);
  DEF_FIELD(11, 4, reg_offset_addr);
  DEF_FIELD(3, 0, byte_cnt);
};

class PmicArbCoreChannelCmdConfig
    : public hwreg::RegisterBase<PmicArbCoreChannelCmdConfig, uint32_t> {
 public:
  static auto Get(uint32_t chnl_cmd_cfg_offset) {
    return hwreg::RegisterAddr<PmicArbCoreChannelCmdConfig>(chnl_cmd_cfg_offset);
  }

  DEF_FIELD(31, 0, intr);
};

class PmicArbCoreChannelCmdWData
    : public hwreg::RegisterBase<PmicArbCoreChannelCmdWData, uint32_t> {
 public:
  static auto Get(uint32_t chnl_cmd_wdata_offset) {
    return hwreg::RegisterAddr<PmicArbCoreChannelCmdWData>(chnl_cmd_wdata_offset);
  }

  DEF_FIELD(31, 0, data);
};

class PmicArbCoreChannelCmdRData
    : public hwreg::RegisterBase<PmicArbCoreChannelCmdRData, uint32_t> {
 public:
  static auto Get(uint32_t chnl_cmd_rdata_offset) {
    return hwreg::RegisterAddr<PmicArbCoreChannelCmdWData>(chnl_cmd_rdata_offset);
  }

  DEF_FIELD(31, 0, data);
};

class PmicArbCoreChannelCmdStatus
    : public hwreg::RegisterBase<PmicArbCoreChannelCmdStatus, uint32_t> {
 public:
  static constexpr uint32_t kPmicArbCmdDone = 0x00000001;
  static constexpr uint32_t kPmicArbCmdFailure = 0x00000002;
  static constexpr uint32_t kPmicArbCmdDenied = 0x00000004;
  static constexpr uint32_t kPmicArbCmdDropped = 0x00000008;

  static auto Get(uint32_t chnl_cmd_status_offset) {
    return hwreg::RegisterAddr<PmicArbCoreChannelCmdStatus>(chnl_cmd_status_offset);
  }
  DEF_FIELD(31, 0, status);
};

class PmicRegAddr : public hwreg::RegisterBase<PmicRegAddr, uint32_t> {
 public:
  static auto Get() { return hwreg::RegisterAddr<PmicRegAddr>(0); }

  DEF_FIELD(19, 16, slave_id);
  DEF_FIELD(15, 8, periph_id);
  DEF_FIELD(7, 0, reg_offset);
};

#endif  // SRC_DEVICES_LIB_MSM8X53_INCLUDE_SOC_MSM8X53_MSM8X53_POWER_REGS_H_
