// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_NAND_DRIVERS_CADENCE_HPNFC_CADENCE_HPNFC_REG_H_
#define SRC_STORAGE_NAND_DRIVERS_CADENCE_HPNFC_CADENCE_HPNFC_REG_H_

#include <hwreg/bitfields.h>

namespace rawnand {

static constexpr uint32_t kInstructionTypeData = 2;
static constexpr uint32_t kInstructionTypeReadId = 27;
static constexpr uint32_t kInstructionTypeReadParameterPage = 28;

class CmdReg0 : public hwreg::RegisterBase<CmdReg0, uint32_t> {
 public:
  static constexpr uint32_t kCommandTypePio = 1;
  static constexpr uint32_t kCommandTypeGeneric = 3;

  static constexpr uint32_t kCommandCodeEraseBlock = 0x1000;
  static constexpr uint32_t kCommandCodeReset = 0x1100;
  static constexpr uint32_t kCommandCodeProgramPage = 0x2100;
  static constexpr uint32_t kCommandCodeReadPage = 0x2200;

  static auto Get() { return hwreg::RegisterAddr<CmdReg0>(0x0000); }

  DEF_FIELD(31, 30, command_type);
  DEF_FIELD(25, 24, thread_number);
  DEF_BIT(21, dma_sel);
  DEF_BIT(20, interrupt_enable);
  DEF_FIELD(19, 16, volume_id);
  DEF_FIELD(15, 0, command_code);
};

class CmdReg1 : public hwreg::RegisterBase<CmdReg1, uint32_t> {
 public:
  static auto Get() { return hwreg::RegisterAddr<CmdReg1>(0x0004); }

  DEF_FIELD(25, 24, bank_number);
  DEF_FIELD(23, 0, address);  // Row address or feature address.
};

class CmdReg2Command : public hwreg::RegisterBase<CmdReg2Command, uint32_t> {
 public:
  static auto Get() { return hwreg::RegisterAddr<CmdReg2Command>(0x0008); }

  DEF_FIELD(31, 16, address_low);
  DEF_FIELD(10, 8, chip_select);
  DEF_BIT(6, wait_for_twb);
  DEF_FIELD(5, 0, instruction_type);
};

class CmdReg2Data : public hwreg::RegisterBase<CmdReg2Data, uint32_t> {
 public:
  static auto Get() { return hwreg::RegisterAddr<CmdReg2Data>(0x0008); }

  DEF_FIELD(31, 16, sector_size);
  DEF_BIT(14, erase_page_detection_enable);
  DEF_BIT(13, scrambler_enable);
  DEF_BIT(12, ecc_enable);
  DEF_BIT(11, data_write);
  DEF_FIELD(10, 8, chip_select);
  DEF_BIT(6, wait_for_twb);
  DEF_FIELD(5, 0, instruction_type);
};

class CmdReg2Dma : public hwreg::RegisterBase<CmdReg2Dma, uint32_t> {
 public:
  static auto Get() { return hwreg::RegisterAddr<CmdReg2Dma>(0x0008); }

  DEF_FIELD(31, 0, dma_address);
};

class CmdReg3 : public hwreg::RegisterBase<CmdReg3, uint32_t> {
 public:
  static auto Get() { return hwreg::RegisterAddr<CmdReg3>(0x000c); }

  DEF_FIELD(25, 24, correction_capability);
  DEF_FIELD(23, 8, last_sector_size);
  DEF_FIELD(7, 0, sector_count);
};

class CmdStatusPtr : public hwreg::RegisterBase<CmdStatusPtr, uint32_t> {
 public:
  static auto Get() { return hwreg::RegisterAddr<CmdStatusPtr>(0x0010); }

  DEF_FIELD(2, 0, thread_status_select);
};

class CmdStatus : public hwreg::RegisterBase<CmdStatus, uint32_t> {
 public:
  static auto Get() { return hwreg::RegisterAddr<CmdStatus>(0x0014); }

  DEF_BIT(16, bus_error);
  DEF_BIT(15, complete);
  DEF_BIT(14, fail);
  DEF_BIT(12, dev_error);
  DEF_FIELD(9, 2, max_errors);
  DEF_BIT(1, ecc_error);
  DEF_BIT(0, cmd_error);
};

class IntrStatus : public hwreg::RegisterBase<IntrStatus, uint32_t> {
 public:
  static auto Get() { return hwreg::RegisterAddr<IntrStatus>(0x0110); }

  auto& clear() {
    set_sdma_error(1);
    set_sdma_trigger(1);
    set_cmd_ignored(1);
    set_ddma_target_error(1);
    set_cdma_target_error(1);
    set_cdma_idle(1);
    return *this;
  }

  DEF_BIT(22, sdma_error);
  DEF_BIT(21, sdma_trigger);
  DEF_BIT(20, cmd_ignored);
  DEF_BIT(18, ddma_target_error);
  DEF_BIT(17, cdma_target_error);
  DEF_BIT(16, cdma_idle);
};

class IntrEnable : public hwreg::RegisterBase<IntrEnable, uint32_t> {
 public:
  static auto Get() { return hwreg::RegisterAddr<IntrEnable>(0x0114); }

  DEF_BIT(31, interrupts_enable);
  DEF_BIT(22, sdma_error_enable);
  DEF_BIT(21, sdma_trigger_enable);
  DEF_BIT(20, cmd_ignored_enable);
  DEF_BIT(18, ddma_target_error_enable);
  DEF_BIT(17, cdma_target_error_enable);
  DEF_BIT(16, cdma_idle_enable);
};

class TrdStatus : public hwreg::RegisterBase<TrdStatus, uint32_t> {
 public:
  static auto Get() { return hwreg::RegisterAddr<TrdStatus>(0x0120); }

  bool thread_busy(uint32_t thread) { return reg_value() & (1 << thread); }
};

class TrdCompIntrStatus : public hwreg::RegisterBase<TrdCompIntrStatus, uint32_t> {
 public:
  static auto Get() { return hwreg::RegisterAddr<TrdCompIntrStatus>(0x0138); }

  bool thread_complete(uint32_t thread) { return reg_value() & (1 << thread); }
};

class TransferCfg0 : public hwreg::RegisterBase<TransferCfg0, uint32_t> {
 public:
  static auto Get() { return hwreg::RegisterAddr<TransferCfg0>(0x0400); }

  DEF_FIELD(31, 16, offset);
  DEF_FIELD(7, 0, sector_count);
};

class TransferCfg1 : public hwreg::RegisterBase<TransferCfg1, uint32_t> {
 public:
  static auto Get() { return hwreg::RegisterAddr<TransferCfg1>(0x0404); }

  DEF_FIELD(31, 16, last_sector_size);
  DEF_FIELD(15, 0, sector_size);
};

class NfDevLayout : public hwreg::RegisterBase<NfDevLayout, uint32_t> {
 public:
  static auto Get() { return hwreg::RegisterAddr<NfDevLayout>(0x0424); }

  DEF_FIELD(31, 27, block_addr_idx);
  DEF_FIELD(23, 20, lun_count);
  DEF_FIELD(15, 0, pages_per_block);
};

class EccConfig0 : public hwreg::RegisterBase<EccConfig0, uint32_t> {
 public:
  static auto Get() { return hwreg::RegisterAddr<EccConfig0>(0x0428); }

  DEF_FIELD(10, 8, correction_strength);
  DEF_BIT(4, scrambler_enable);
  DEF_BIT(1, erase_detection_enable);
  DEF_BIT(0, ecc_enable);
};

class EccConfig1 : public hwreg::RegisterBase<EccConfig1, uint32_t> {
 public:
  static auto Get() { return hwreg::RegisterAddr<EccConfig1>(0x042c); }

  DEF_FIELD(7, 0, erase_detection_level);
};

class SdmaSize : public hwreg::RegisterBase<SdmaSize, uint32_t> {
 public:
  static auto Get() { return hwreg::RegisterAddr<SdmaSize>(0x0440); }
};

class RbnSettings : public hwreg::RegisterBase<RbnSettings, uint32_t> {
 public:
  static auto Get() { return hwreg::RegisterAddr<RbnSettings>(0x1004); }

  DEF_BIT(0, rbn);
};

}  // namespace rawnand

#endif  // SRC_STORAGE_NAND_DRIVERS_CADENCE_HPNFC_CADENCE_HPNFC_REG_H_
