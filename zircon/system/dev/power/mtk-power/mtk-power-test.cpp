// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "mtk-power.h"

#include <fbl/auto_call.h>
#include <mock-mmio-reg/mock-mmio-reg.h>
#include <zxtest/zxtest.h>

namespace {

static constexpr uint32_t kPmicMmioRegCount = 768;

} // namespace

namespace power {

class MtkPowerTest : public MtkPower {
public:
    MtkPowerTest(ddk_mock::MockMmioRegRegion& pmic_mmio)
        : MtkPower(nullptr, ddk::MmioBuffer(pmic_mmio.GetMmioBuffer())) {}
    void InitPowerRegulators() { InitializePowerDomains(); }
    MtkRegulator& GetPowerDomain(uint32_t index) { return *(power_domains_[index]); }
};

template <class T> ddk_mock::MockMmioReg& GetMockReg(ddk_mock::MockMmioRegRegion& registers) {
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

TEST(PowerTest, EnablePowerDomain) {
    ddk_mock::MockMmioReg pmic_reg_array[kPmicMmioRegCount];
    ddk_mock::MockMmioRegRegion pmic_regs(pmic_reg_array, sizeof(uint32_t), kPmicMmioRegCount);
    MtkPowerTest power_test(pmic_regs);
    power_test.InitPowerRegulators();

    // Test enabling invalid PowerDomain fails
    ASSERT_EQ(power_test.PowerImplEnablePowerDomain(kMt8167NumPowerDomains + 1),
              ZX_ERR_OUT_OF_RANGE);

    uint32_t test_index = 0;
    MtkRegulator& domain = power_test.GetPowerDomain(test_index);
    EnableDomainHelper(pmic_regs, domain);
    ASSERT_EQ(power_test.PowerImplEnablePowerDomain(test_index), ZX_OK);
    ASSERT_EQ(domain.enabled(), true);
    pmic_regs.VerifyAll();
}

TEST(PowerTest, DisablePowerDomain) {
    ddk_mock::MockMmioReg pmic_reg_array[kPmicMmioRegCount];
    ddk_mock::MockMmioRegRegion pmic_regs(pmic_reg_array, sizeof(uint32_t), kPmicMmioRegCount);
    MtkPowerTest power_test(pmic_regs);
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
    ASSERT_EQ(power_test.PowerImplEnablePowerDomain(test_index), ZX_OK);
    pmic_regs.VerifyAll();

    // Test disabling the above enabled power domain succeeds
    ReadPmicRegHelper(pmic_regs, domain.enable_register(), 0);
    WritePmicRegHelper(pmic_regs, domain.enable_register(), 0);
    ASSERT_EQ(power_test.PowerImplDisablePowerDomain(test_index), ZX_OK);
    pmic_regs.VerifyAll();
    ASSERT_EQ(domain.enabled(), false);
}

TEST(PowerTest, GetSupportedVoltageRange) {
    ddk_mock::MockMmioReg pmic_reg_array[kPmicMmioRegCount];
    ddk_mock::MockMmioRegRegion pmic_regs(pmic_reg_array, sizeof(uint32_t), kPmicMmioRegCount);
    MtkPowerTest power_test(pmic_regs);
    power_test.InitPowerRegulators();
    uint32_t test_index = 0, min_voltage = 0, max_voltage = 0;

    // Test Buck Regulator
    ASSERT_EQ(power_test.PowerImplGetSupportedVoltageRange(test_index, &min_voltage, &max_voltage),
              ZX_OK);
    ASSERT_EQ(min_voltage, 700000);
    ASSERT_EQ(max_voltage, 1493750);

    // Test Ldo Regulator
    ASSERT_EQ(power_test.PowerImplGetSupportedVoltageRange(4, &min_voltage, &max_voltage), ZX_OK);
    ASSERT_EQ(min_voltage, 1800000);
    ASSERT_EQ(max_voltage, 2200000);

    //Test Fixed Regulator
    ASSERT_EQ(power_test.PowerImplGetSupportedVoltageRange(3, &min_voltage, &max_voltage),
              ZX_ERR_NOT_SUPPORTED);
}

} // namespace power
