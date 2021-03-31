// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "msm8x53-power.h"

#include <mock-mmio-reg/mock-mmio-reg.h>
#include <soc/msm8x53/msm8x53-power.h>
#include <zxtest/zxtest.h>

namespace {

static constexpr uint32_t kPmicArbCoreMmioRegCount = 768;
static constexpr uint32_t kPmicArbChnlsMmioRegCount = 768;
static constexpr uint32_t kPmicArbObsvrMmioRegCount = 768;
static constexpr uint32_t kDummyRegCount = 100;

}  // namespace

namespace power {

class Msm8x53PowerTest : public Msm8x53Power {
 public:
  Msm8x53PowerTest(ddk_mock::MockMmioRegRegion& core_mmio_registers,
                   ddk_mock::MockMmioRegRegion& chnls_mmio_registers,
                   ddk_mock::MockMmioRegRegion& obsvr_mmio_registers,
                   ddk_mock::MockMmioRegRegion& intr_mmio_registers,
                   ddk_mock::MockMmioRegRegion& cfg_mmio_registers)
      : Msm8x53Power(nullptr, ddk::MmioBuffer(core_mmio_registers.GetMmioBuffer()),
                     ddk::MmioBuffer(chnls_mmio_registers.GetMmioBuffer()),
                     ddk::MmioBuffer(obsvr_mmio_registers.GetMmioBuffer()),
                     ddk::MmioBuffer(intr_mmio_registers.GetMmioBuffer()),
                     ddk::MmioBuffer(cfg_mmio_registers.GetMmioBuffer())) {}
  zx_status_t InvokePmicArbInit() { return PmicArbInit(); }

  uint32_t GetApid(uint32_t ppid) { return ppid_to_apid_[ppid]; }

  void SetApid(uint32_t ppid, uint32_t apid) { ppid_to_apid_[ppid] = apid; }
};

template <class T>
ddk_mock::MockMmioReg& GetMockReg(ddk_mock::MockMmioRegRegion& registers) {
  return registers[T::Get().addr()];
}

template <class T>
ddk_mock::MockMmioReg& GetMockReg(int reg_offset, ddk_mock::MockMmioRegRegion& registers) {
  return registers[T::Get(reg_offset).addr()];
}

TEST(PowerTest, PmicArbInit) {
  ddk_mock::MockMmioReg core_reg_array[kPmicArbCoreMmioRegCount];
  ddk_mock::MockMmioRegRegion mock_core_regs(core_reg_array, sizeof(uint32_t),
                                             kPmicArbCoreMmioRegCount);

  ddk_mock::MockMmioReg dummy_reg_array[kDummyRegCount];
  ddk_mock::MockMmioRegRegion mock_dummy_regs(dummy_reg_array, sizeof(uint32_t), kDummyRegCount);

  Msm8x53PowerTest power(mock_core_regs, mock_dummy_regs, mock_dummy_regs, mock_dummy_regs,
                         mock_dummy_regs);

  // Check Version. If not 2, PmicArbInit fails.
  GetMockReg<PmicArbVersion>(mock_core_regs).ExpectRead(kPmicArbVersionTwo);

  // Check the APID -> PPID mapping for peripheral id 0, 1, 14
  mock_core_regs[PMIC_ARB_CORE_CHANNEL_INFO_OFFSET(0)].ExpectRead(
      PmicArbCoreChannelInfo().set_slave_id(0xe).set_periph_id(0x8).reg_value());
  mock_core_regs[PMIC_ARB_CORE_CHANNEL_INFO_OFFSET(1)].ExpectRead(
      PmicArbCoreChannelInfo().set_slave_id(0x0).set_periph_id(0x77).reg_value());
  mock_core_regs[PMIC_ARB_CORE_CHANNEL_INFO_OFFSET(14)].ExpectRead(
      PmicArbCoreChannelInfo().set_slave_id(0x2).set_periph_id(0x1b).reg_value());

  EXPECT_OK(power.InvokePmicArbInit());
  ASSERT_EQ(power.GetApid(PPID(0xe, 0x8)), 0);
  ASSERT_EQ(power.GetApid(PPID(0x0, 0x77)), 1);
  ASSERT_EQ(power.GetApid(PPID(0x2, 0x1b)), 14);
}

TEST(PowerTest, PmicWritePmicCtrlReg) {
  ddk_mock::MockMmioReg chnls_reg_array[kPmicArbChnlsMmioRegCount];
  ddk_mock::MockMmioRegRegion mock_chnls_regs(chnls_reg_array, sizeof(uint32_t),
                                              kPmicArbChnlsMmioRegCount);

  ddk_mock::MockMmioReg obsvr_reg_array[kPmicArbObsvrMmioRegCount];
  ddk_mock::MockMmioRegRegion mock_obsvr_regs(obsvr_reg_array, sizeof(uint32_t),
                                              kPmicArbObsvrMmioRegCount);

  ddk_mock::MockMmioReg dummy_reg_array[kDummyRegCount];
  ddk_mock::MockMmioRegRegion mock_dummy_regs(dummy_reg_array, sizeof(uint32_t), kDummyRegCount);

  Msm8x53PowerTest power(mock_dummy_regs, mock_chnls_regs, mock_obsvr_regs, mock_dummy_regs,
                         mock_dummy_regs);

  uint32_t apid = 0;
  // Disabling the interrupt
  uint32_t cmd_cfg_offset = PMIC_ARB_CHANNEL_CMD_CONFIG_OFFSET(apid);
  mock_chnls_regs[cmd_cfg_offset].ExpectRead(0xFFFFFFFF).ExpectWrite(0x0);
  // Set the ppid to apid mapping
  uint32_t slave_id = 0x3;
  uint32_t periph_id = 0xc0;
  uint32_t write_value = 0x80;
  uint32_t reg_offset = 0x70;
  uint32_t write_addr = 0x3c070;
  power.SetApid(PPID(slave_id, periph_id), apid);

  // Write first 4 bytes to WDATA0
  mock_chnls_regs[PMIC_ARB_CHANNEL_CMD_WDATA0_OFFSET(apid)]
      .ExpectRead(0x00000000)
      .ExpectWrite(PmicArbCoreChannelCmdWData().set_data(write_value).reg_value());

  // Write the CMD
  uint32_t cmd_offset = PMIC_ARB_CHANNEL_CMD_OFFSET(apid);
  mock_chnls_regs[cmd_offset]
      .ExpectRead(0x00000000)
      .ExpectWrite(PmicArbCoreChannelCmdInfo()
                       .set_byte_cnt(0)
                       .set_reg_offset_addr(reg_offset)
                       .set_periph_id(periph_id)
                       .set_slave_id(slave_id)
                       .set_priority(0)
                       .set_opcode(kSpmiCmdRegWriteOpcode)
                       .reg_value());

  // Write CMD Completion
  mock_chnls_regs[PMIC_ARB_CHANNEL_CMD_STATUS_OFFSET(apid)].ExpectRead(
      PmicArbCoreChannelCmdStatus()
          .set_status(PmicArbCoreChannelCmdStatus::kPmicArbCmdDone)
          .reg_value());

  EXPECT_OK(power.PowerImplWritePmicCtrlReg(kPmicCtrlReg, write_addr, write_value));
  mock_chnls_regs.VerifyAll();
}

TEST(PowerTest, PmicReadPmicCtrlReg) {
  ddk_mock::MockMmioReg obsvr_reg_array[kPmicArbObsvrMmioRegCount];
  ddk_mock::MockMmioRegRegion mock_obsvr_regs(obsvr_reg_array, sizeof(uint32_t),
                                              kPmicArbObsvrMmioRegCount);

  ddk_mock::MockMmioReg dummy_reg_array[kDummyRegCount];
  ddk_mock::MockMmioRegRegion mock_dummy_regs(dummy_reg_array, sizeof(uint32_t), kDummyRegCount);

  Msm8x53PowerTest power(mock_dummy_regs, mock_dummy_regs, mock_obsvr_regs, mock_dummy_regs,
                         mock_dummy_regs);

  uint32_t apid = 0;
  // Disabling the interrupt
  uint32_t cmd_cfg_offset = PMIC_ARB_CHANNEL_CMD_CONFIG_OFFSET(apid);
  mock_obsvr_regs[cmd_cfg_offset].ExpectRead(0xFFFFFFFF).ExpectWrite(0x0);
  // Set the ppid to apid mapping
  uint32_t slave_id = 0x3;
  uint32_t periph_id = 0xc0;
  uint32_t expected_value = 0x80;
  uint32_t reg_offset = 0x70;
  uint32_t read_addr = 0x3c070;
  uint32_t read_data;
  power.SetApid(PPID(slave_id, periph_id), apid);

  // Write the Read CMD
  uint32_t cmd_offset = PMIC_ARB_CHANNEL_CMD_OFFSET(apid);
  mock_obsvr_regs[cmd_offset]
      .ExpectRead(0x00000000)
      .ExpectWrite(PmicArbCoreChannelCmdInfo()
                       .set_byte_cnt(0)
                       .set_reg_offset_addr(reg_offset)
                       .set_periph_id(periph_id)
                       .set_slave_id(slave_id)
                       .set_priority(0)
                       .set_opcode(kSpmiCmdRegReadOpcode)
                       .reg_value());

  // Write CMD Completion to unblock the test
  mock_obsvr_regs[PMIC_ARB_CHANNEL_CMD_STATUS_OFFSET(apid)].ExpectRead(
      PmicArbCoreChannelCmdStatus()
          .set_status(PmicArbCoreChannelCmdStatus::kPmicArbCmdDone)
          .reg_value());
  // Read first 4 bytes to RDATA0
  mock_obsvr_regs[PMIC_ARB_CHANNEL_CMD_RDATA0_OFFSET(apid)].ExpectRead(
      PmicArbCoreChannelCmdRData().set_data(expected_value).reg_value());

  EXPECT_OK(power.PowerImplReadPmicCtrlReg(kPmicCtrlReg, read_addr, &read_data));
  mock_obsvr_regs.VerifyAll();
  ASSERT_EQ(read_data, expected_value);
}

}  // namespace power
