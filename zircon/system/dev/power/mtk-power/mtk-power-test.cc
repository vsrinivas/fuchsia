// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "mtk-power.h"

#include <fbl/auto_call.h>
#include <mock-mmio-reg/mock-mmio-reg.h>
#include <zxtest/zxtest.h>

namespace {

static constexpr uint32_t kPmicMmioRegCount = 768;

}  // namespace

namespace power {

class MtkPowerTest : public MtkPower {
 public:
  MtkPowerTest(ddk_mock::MockMmioRegRegion& pmic_mmio)
      : MtkPower(nullptr, ddk::MmioBuffer(pmic_mmio.GetMmioBuffer())) {}
  void InitPowerRegulators() { InitializePowerDomains(); }
  MtkRegulator& GetPowerDomain(uint32_t index) { return *(power_domains_[index]); }
};

template <class T>
ddk_mock::MockMmioReg& GetMockReg(ddk_mock::MockMmioRegRegion& registers) {
  return registers[T::Get().addr()];
}

template <class T>
ddk_mock::MockMmioReg& GetMockReg(int reg_offset, ddk_mock::MockMmioRegRegion& registers) {
  return registers[T::Get(reg_offset).addr()];
}

void ReadPmicRegHelper(ddk_mock::MockMmioRegRegion& pmic_regs, uint32_t reg_addr,
                       uint32_t reg_value) {
  // Match idle
  GetMockReg<PmicWacs2RData>(pmic_regs).ExpectRead(
      PmicWacs2RData().set_wacs2_fsm(PmicWacs2RData::kFsmStateIdle).reg_value());
  // Match cmd
  GetMockReg<PmicWacs2Cmd>(pmic_regs).ExpectWrite(
      PmicWacs2Cmd().set_wacs2_write(0).set_wacs2_addr(reg_addr >> 1).reg_value());
  // Match vldclr
  GetMockReg<PmicWacs2RData>(pmic_regs).ExpectRead(
      PmicWacs2RData().set_wacs2_fsm(PmicWacs2RData::kFsmStateWfVldClear).reg_value());
  // Match data read
  GetMockReg<PmicWacs2RData>(pmic_regs).ExpectRead(reg_value);

  // Match vldclr
  GetMockReg<PmicWacs2VldClear>(pmic_regs).ExpectRead(0).ExpectWrite(
      PmicWacs2VldClear().set_wacs2_vldclr(1).reg_value());
}

void WritePmicRegHelper(ddk_mock::MockMmioRegRegion& pmic_regs, uint32_t reg_addr,
                        uint32_t reg_value) {
  // Match idle
  GetMockReg<PmicWacs2RData>(pmic_regs).ExpectRead(
      PmicWacs2RData().set_wacs2_fsm(PmicWacs2RData::kFsmStateIdle).reg_value());

  // Match cmd write
  GetMockReg<PmicWacs2Cmd>(pmic_regs).ExpectWrite(PmicWacs2Cmd()
                                                      .set_wacs2_write(1)
                                                      .set_wacs2_addr(reg_addr >> 1)
                                                      .set_wacs2_data(reg_value)
                                                      .reg_value());
}

void EnableDomainHelper(ddk_mock::MockMmioRegRegion& pmic_regs, MtkRegulator& domain) {
  ReadPmicRegHelper(pmic_regs, domain.enable_register(), 0);
  WritePmicRegHelper(pmic_regs, domain.enable_register(), 1 << domain.enable_bit());
}

void InitPowerRegulatorsHelper(ddk_mock::MockMmioRegRegion& pmic_regs) {
  ReadPmicRegHelper(pmic_regs, kPmicVprocCon5, 1 << 1);
  ReadPmicRegHelper(pmic_regs, kPmicVcoreCon5, 1 << 1);
  ReadPmicRegHelper(pmic_regs, kPmicVsysCon5, 1 << 1);
}

TEST(PowerTest, Init) {
  ddk_mock::MockMmioReg pmic_reg_array[kPmicMmioRegCount];
  ddk_mock::MockMmioRegRegion pmic_regs(pmic_reg_array, sizeof(uint32_t), kPmicMmioRegCount);
  MtkPowerTest power_test(pmic_regs);

  InitPowerRegulatorsHelper(pmic_regs);
  power_test.InitPowerRegulators();

  // Test if the buck regulators have the right voltage_sel_reg
  MtkBuckRegulator& domain = static_cast<MtkBuckRegulator&>(power_test.GetPowerDomain(0));
  ASSERT_EQ(domain.voltage_sel_reg(), kPmicVprocCon10);
}

TEST(PowerTest, EnablePowerDomain) {
  ddk_mock::MockMmioReg pmic_reg_array[kPmicMmioRegCount];
  ddk_mock::MockMmioRegRegion pmic_regs(pmic_reg_array, sizeof(uint32_t), kPmicMmioRegCount);
  MtkPowerTest power_test(pmic_regs);

  InitPowerRegulatorsHelper(pmic_regs);
  power_test.InitPowerRegulators();

  // Test enabling invalid PowerDomain fails
  ASSERT_EQ(power_test.PowerImplEnablePowerDomain(kMt8167NumPowerDomains + 1), ZX_ERR_OUT_OF_RANGE);

  uint32_t test_index = 0;
  MtkRegulator& domain = power_test.GetPowerDomain(test_index);
  EnableDomainHelper(pmic_regs, domain);
  ASSERT_OK(power_test.PowerImplEnablePowerDomain(test_index));
  ASSERT_EQ(domain.enabled(), true);
  pmic_regs.VerifyAll();
}

TEST(PowerTest, DisablePowerDomain) {
  ddk_mock::MockMmioReg pmic_reg_array[kPmicMmioRegCount];
  ddk_mock::MockMmioRegRegion pmic_regs(pmic_reg_array, sizeof(uint32_t), kPmicMmioRegCount);
  MtkPowerTest power_test(pmic_regs);

  InitPowerRegulatorsHelper(pmic_regs);
  power_test.InitPowerRegulators();

  // Test disabling invalid PowerDomain fails
  ASSERT_EQ(power_test.PowerImplDisablePowerDomain(kMt8167NumPowerDomains + 1),
            ZX_ERR_OUT_OF_RANGE);

  // Test disabling a regulator that is not enabled fails
  uint32_t test_index = 0;
  MtkRegulator& domain = power_test.GetPowerDomain(test_index);
  ASSERT_EQ(domain.enabled(), false);
  ASSERT_EQ(power_test.PowerImplDisablePowerDomain(test_index), ZX_ERR_BAD_STATE);

  // Enable power domain
  EnableDomainHelper(pmic_regs, domain);
  ASSERT_OK(power_test.PowerImplEnablePowerDomain(test_index));
  pmic_regs.VerifyAll();

  // Test disabling the above enabled power domain succeeds
  ReadPmicRegHelper(pmic_regs, domain.enable_register(), 0);
  WritePmicRegHelper(pmic_regs, domain.enable_register(), 0);
  ASSERT_OK(power_test.PowerImplDisablePowerDomain(test_index));
  pmic_regs.VerifyAll();
  ASSERT_EQ(domain.enabled(), false);
}

TEST(PowerTest, GetSupportedVoltageRange) {
  ddk_mock::MockMmioReg pmic_reg_array[kPmicMmioRegCount];
  ddk_mock::MockMmioRegRegion pmic_regs(pmic_reg_array, sizeof(uint32_t), kPmicMmioRegCount);
  MtkPowerTest power_test(pmic_regs);

  InitPowerRegulatorsHelper(pmic_regs);
  power_test.InitPowerRegulators();

  uint32_t min_voltage = 0, max_voltage = 0;

  // Test Buck Regulator
  ASSERT_OK(power_test.PowerImplGetSupportedVoltageRange(0, &min_voltage, &max_voltage));
  ASSERT_EQ(min_voltage, 700000);
  ASSERT_EQ(max_voltage, 1493750);

  // Test Ldo Regulator
  ASSERT_OK(power_test.PowerImplGetSupportedVoltageRange(4, &min_voltage, &max_voltage));
  ASSERT_EQ(min_voltage, 1800000);
  ASSERT_EQ(max_voltage, 2200000);

  // Test Fixed Regulator
  ASSERT_EQ(power_test.PowerImplGetSupportedVoltageRange(3, &min_voltage, &max_voltage),
            ZX_ERR_NOT_SUPPORTED);
}

TEST(PowerTest, RequestVoltage) {
  ddk_mock::MockMmioReg pmic_reg_array[kPmicMmioRegCount];
  ddk_mock::MockMmioRegRegion pmic_regs(pmic_reg_array, sizeof(uint32_t), kPmicMmioRegCount);
  MtkPowerTest power_test(pmic_regs);

  InitPowerRegulatorsHelper(pmic_regs);
  power_test.InitPowerRegulators();
  pmic_regs.VerifyAll();

  uint32_t out_voltage;
  // Fixed Regulator tests
  ASSERT_EQ(power_test.PowerImplRequestVoltage(3, 0, &out_voltage), ZX_ERR_NOT_SUPPORTED);

  // BUCK Regulator tests
  uint32_t test_index = 0;
  // With voltage less than min voltage
  ASSERT_EQ(power_test.PowerImplRequestVoltage(test_index, 0, &out_voltage), ZX_ERR_NOT_SUPPORTED);
  // With voltage greater than max voltage
  ASSERT_EQ(power_test.PowerImplRequestVoltage(test_index, 1500000, &out_voltage),
            ZX_ERR_NOT_SUPPORTED);

  // With a supported voltage
  uint32_t test_voltage = 706251;
  uint16_t expected_selector = 1;
  uint32_t expected_out_voltage = 706250;

  MtkBuckRegulator& buck_domain =
      static_cast<MtkBuckRegulator&>(power_test.GetPowerDomain(test_index));
  uint32_t expected_write_value =
      ((expected_selector << buck_domain.voltage_sel_shift()) & buck_domain.voltage_sel_mask());
  ReadPmicRegHelper(pmic_regs, buck_domain.voltage_sel_reg(), 0);
  WritePmicRegHelper(pmic_regs, buck_domain.voltage_sel_reg(), expected_write_value);
  ASSERT_OK(power_test.PowerImplRequestVoltage(test_index, test_voltage, &out_voltage));
  ASSERT_EQ(buck_domain.cur_voltage(), out_voltage);
  ASSERT_EQ(out_voltage, expected_out_voltage);

  // With a voltage that is already set
  ASSERT_OK(power_test.PowerImplRequestVoltage(test_index, test_voltage, &out_voltage));

  // LDO Regulator tests
  test_index = 4;
  // With voltage less than min voltage
  ASSERT_EQ(power_test.PowerImplRequestVoltage(test_index, 1700000, &out_voltage),
            ZX_ERR_NOT_SUPPORTED);
  // With voltage greater than max voltage
  ASSERT_EQ(power_test.PowerImplRequestVoltage(test_index, 2200500, &out_voltage),
            ZX_ERR_NOT_SUPPORTED);
  test_voltage = 1800500;
  expected_selector = 0;
  expected_out_voltage = 1800000;
  MtkLdoRegulator& ldo_domain =
      static_cast<MtkLdoRegulator&>(power_test.GetPowerDomain(test_index));
  expected_write_value =
      ((expected_selector << ldo_domain.voltage_sel_shift()) & ldo_domain.voltage_sel_mask());
  ReadPmicRegHelper(pmic_regs, ldo_domain.voltage_sel_reg(), 0);
  WritePmicRegHelper(pmic_regs, ldo_domain.voltage_sel_reg(), expected_write_value);
  ASSERT_OK(power_test.PowerImplRequestVoltage(test_index, test_voltage, &out_voltage));
  ASSERT_EQ(ldo_domain.cur_voltage(), out_voltage);
  ASSERT_EQ(out_voltage, expected_out_voltage);

  // With a voltage that is already set
  ASSERT_OK(power_test.PowerImplRequestVoltage(test_index, test_voltage, &out_voltage));
}

TEST(PowerTest, GetCurrentVoltage) {
  ddk_mock::MockMmioReg pmic_reg_array[kPmicMmioRegCount];
  ddk_mock::MockMmioRegRegion pmic_regs(pmic_reg_array, sizeof(uint32_t), kPmicMmioRegCount);
  MtkPowerTest power_test(pmic_regs);

  InitPowerRegulatorsHelper(pmic_regs);
  power_test.InitPowerRegulators();
  pmic_regs.VerifyAll();

  uint32_t out_voltage;
  uint32_t test_index = 0;

  // Fixed Regulator tests
  test_index = 3;
  MtkFixedRegulator& fixed_domain =
      static_cast<MtkFixedRegulator&>(power_test.GetPowerDomain(test_index));
  ASSERT_OK(power_test.PowerImplGetCurrentVoltage(test_index, &out_voltage));
  ASSERT_EQ(fixed_domain.default_voltage(), out_voltage);

  // BUCK Regulator tests
  test_index = 0;
  MtkBuckRegulator& buck_domain =
      static_cast<MtkBuckRegulator&>(power_test.GetPowerDomain(test_index));
  ASSERT_OK(power_test.PowerImplGetCurrentVoltage(test_index, &out_voltage));
  ASSERT_EQ(buck_domain.default_voltage(), out_voltage);

  // change current voltage
  uint32_t test_voltage = 706251;
  uint16_t expected_selector = 1;
  uint32_t expected_out_voltage = 706250;

  uint32_t expected_write_value =
      ((expected_selector << buck_domain.voltage_sel_shift()) & buck_domain.voltage_sel_mask());
  ReadPmicRegHelper(pmic_regs, buck_domain.voltage_sel_reg(), 0);
  WritePmicRegHelper(pmic_regs, buck_domain.voltage_sel_reg(), expected_write_value);

  ASSERT_OK(power_test.PowerImplRequestVoltage(test_index, test_voltage, &out_voltage));
  ASSERT_EQ(out_voltage, expected_out_voltage);
  ASSERT_OK(power_test.PowerImplGetCurrentVoltage(test_index, &out_voltage));
  ASSERT_EQ(buck_domain.cur_voltage(), out_voltage);

  // LDO Regulator tests
  test_index = 4;
  MtkLdoRegulator& ldo_domain =
      static_cast<MtkLdoRegulator&>(power_test.GetPowerDomain(test_index));
  ASSERT_OK(power_test.PowerImplGetCurrentVoltage(test_index, &out_voltage));
  ASSERT_EQ(ldo_domain.default_voltage(), out_voltage);

  // change current voltage
  test_voltage = 1800500;
  expected_selector = 0;
  expected_out_voltage = 1800000;
  expected_write_value =
      ((expected_selector << ldo_domain.voltage_sel_shift()) & ldo_domain.voltage_sel_mask());
  ReadPmicRegHelper(pmic_regs, ldo_domain.voltage_sel_reg(), 0);
  WritePmicRegHelper(pmic_regs, ldo_domain.voltage_sel_reg(), expected_write_value);
  ASSERT_OK(power_test.PowerImplRequestVoltage(test_index, test_voltage, &out_voltage));
  ASSERT_EQ(out_voltage, expected_out_voltage);
  ASSERT_OK(power_test.PowerImplGetCurrentVoltage(test_index, &out_voltage));
  ASSERT_EQ(ldo_domain.cur_voltage(), out_voltage);
}

}  // namespace power
