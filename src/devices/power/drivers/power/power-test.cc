// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "power.h"

#include <fuchsia/hardware/power/cpp/banjo.h>
#include <fuchsia/hardware/powerimpl/c/banjo.h>
#include <fuchsia/hardware/powerimpl/cpp/banjo.h>
#include <lib/fake_ddk/fake_ddk.h>

#include <memory>

#include <zxtest/zxtest.h>

namespace power {

class FakePower : public ddk::PowerProtocol<FakePower> {
 public:
  FakePower() : proto_{.ops = &power_protocol_ops_, .ctx = this} {}
  ddk::PowerProtocolClient GetClient() const { return ddk::PowerProtocolClient(&proto_); }
  zx_status_t PowerRegisterPowerDomain(uint32_t min_needed_voltage_uV,
                                       uint32_t max_supported_voltage_uV) {
    power_domain_registered_count_++;
    return ZX_OK;
  }

  zx_status_t PowerUnregisterPowerDomain() {
    power_domain_unregistered_count_++;
    return ZX_OK;
  }

  zx_status_t PowerGetPowerDomainStatus(power_domain_status_t* out_status) { return ZX_OK; }

  zx_status_t PowerGetSupportedVoltageRange(uint32_t* min_voltage, uint32_t* max_voltage) {
    return ZX_OK;
  }

  zx_status_t PowerRequestVoltage(uint32_t voltage, uint32_t* actual_voltage) { return ZX_OK; }

  zx_status_t PowerGetCurrentVoltage(uint32_t index, uint32_t* current_voltage) { return ZX_OK; }

  zx_status_t PowerWritePmicCtrlReg(uint32_t reg_addr, uint32_t value) { return ZX_OK; }
  zx_status_t PowerReadPmicCtrlReg(uint32_t reg_addr, uint32_t* out_value) { return ZX_OK; }
  uint32_t power_domain_registered_count() { return power_domain_registered_count_; }
  uint32_t power_domain_unregistered_count() { return power_domain_unregistered_count_; }

 private:
  power_protocol_t proto_;
  uint32_t power_domain_registered_count_ = 0;
  uint32_t power_domain_unregistered_count_ = 0;
};

class FakePowerImpl : public ddk::PowerImplProtocol<FakePowerImpl> {
 public:
  FakePowerImpl() : proto_{.ops = &power_impl_protocol_ops_, .ctx = this} {}
  ddk::PowerImplProtocolClient GetClient() const { return ddk::PowerImplProtocolClient(&proto_); }
  zx_status_t PowerImplGetCurrentVoltage(uint32_t index, uint32_t* current_voltage) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  zx_status_t PowerImplDisablePowerDomain(uint32_t index) {
    power_domain_disabled_count_++;
    return ZX_OK;
  }

  zx_status_t PowerImplEnablePowerDomain(uint32_t index) {
    power_domain_enabled_count_++;
    return ZX_OK;
  }

  zx_status_t PowerImplGetPowerDomainStatus(uint32_t index, power_domain_status_t* out_status) {
    return ZX_OK;
  }

  zx_status_t PowerImplGetSupportedVoltageRange(uint32_t index, uint32_t* min_voltage,
                                                uint32_t* max_voltage) {
    return ZX_OK;
  }

  zx_status_t PowerImplRequestVoltage(uint32_t index, uint32_t voltage, uint32_t* actual_voltage) {
    *actual_voltage = voltage;
    return ZX_OK;
  }
  zx_status_t PowerImplWritePmicCtrlReg(uint32_t index, uint32_t reg_addr, uint32_t value) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  zx_status_t PowerImplReadPmicCtrlReg(uint32_t index, uint32_t addr, uint32_t* value) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  uint32_t power_domain_enabled_count() { return power_domain_enabled_count_; }

  uint32_t power_domain_disabled_count() { return power_domain_disabled_count_; }

 private:
  power_impl_protocol_t proto_;
  uint32_t power_domain_enabled_count_ = 0;
  uint32_t power_domain_disabled_count_ = 0;
};

class GenericPowerTest : public zxtest::Test {
 public:
  explicit GenericPowerTest() {}
  void SetUp() override {
    power_impl_ = std::make_unique<FakePowerImpl>();
    parent_power_ = std::make_unique<FakePower>();
    dut_ = std::make_unique<PowerDevice>(fake_ddk::kFakeParent, 0, power_impl_->GetClient(),
                                         parent_power_->GetClient(), 10, 1000, false);
    dut_->DdkOpenProtocolSessionMultibindable(ZX_PROTOCOL_POWER, &proto_ctx_);
  }

 protected:
  std::unique_ptr<PowerDevice> dut_;
  power_protocol_t proto_ctx_;
  std::unique_ptr<FakePower> parent_power_;
  std::unique_ptr<FakePowerImpl> power_impl_;
};

TEST_F(GenericPowerTest, RegisterDomain) {
  ddk::PowerProtocolClient proto_client = ddk::PowerProtocolClient(&proto_ctx_);
  proto_client.RegisterPowerDomain(20, 800);
  EXPECT_EQ(dut_->GetDependentCount(), 1);
  EXPECT_EQ(parent_power_->power_domain_registered_count(), 1);
  EXPECT_EQ(power_impl_->power_domain_enabled_count(), 1);
}

TEST_F(GenericPowerTest, RegisterTwice) {
  ddk::PowerProtocolClient proto_client = ddk::PowerProtocolClient(&proto_ctx_);
  EXPECT_OK(proto_client.RegisterPowerDomain(20, 800));
  EXPECT_EQ(dut_->GetDependentCount(), 1);
  EXPECT_EQ(parent_power_->power_domain_registered_count(), 1);
  EXPECT_EQ(power_impl_->power_domain_enabled_count(), 1);
  EXPECT_OK(proto_client.RegisterPowerDomain(20, 800));
  EXPECT_EQ(dut_->GetDependentCount(), 1);
  EXPECT_EQ(parent_power_->power_domain_registered_count(), 1);
  EXPECT_EQ(power_impl_->power_domain_enabled_count(), 1);
}

TEST_F(GenericPowerTest, UnregisterDomain) {
  ddk::PowerProtocolClient proto_client = ddk::PowerProtocolClient(&proto_ctx_);
  EXPECT_OK(proto_client.RegisterPowerDomain(20, 800));
  EXPECT_EQ(dut_->GetDependentCount(), 1);
  EXPECT_EQ(parent_power_->power_domain_registered_count(), 1);
  EXPECT_EQ(power_impl_->power_domain_enabled_count(), 1);
  EXPECT_OK(proto_client.UnregisterPowerDomain());
  EXPECT_EQ(dut_->GetDependentCount(), 0);
  EXPECT_EQ(parent_power_->power_domain_unregistered_count(), 1);
  EXPECT_EQ(power_impl_->power_domain_disabled_count(), 1);
}

TEST_F(GenericPowerTest, UnregisterTwice) {
  ddk::PowerProtocolClient proto_client = ddk::PowerProtocolClient(&proto_ctx_);
  EXPECT_OK(proto_client.RegisterPowerDomain(20, 800));
  EXPECT_EQ(dut_->GetDependentCount(), 1);
  EXPECT_EQ(parent_power_->power_domain_registered_count(), 1);
  EXPECT_EQ(power_impl_->power_domain_enabled_count(), 1);
  EXPECT_OK(proto_client.UnregisterPowerDomain());
  EXPECT_EQ(dut_->GetDependentCount(), 0);
  EXPECT_EQ(parent_power_->power_domain_registered_count(), 1);
  EXPECT_EQ(power_impl_->power_domain_enabled_count(), 1);
  EXPECT_EQ(proto_client.UnregisterPowerDomain(), ZX_ERR_UNAVAILABLE);
  EXPECT_EQ(dut_->GetDependentCount(), 0);
}

TEST_F(GenericPowerTest, DependentCount_TwoChildren) {
  ddk::PowerProtocolClient proto_client = ddk::PowerProtocolClient(&proto_ctx_);
  proto_client.RegisterPowerDomain(20, 800);
  EXPECT_EQ(dut_->GetDependentCount(), 1);
  EXPECT_EQ(parent_power_->power_domain_registered_count(), 1);
  EXPECT_EQ(power_impl_->power_domain_enabled_count(), 1);

  power_protocol_t proto_ctx_2;
  dut_->DdkOpenProtocolSessionMultibindable(ZX_PROTOCOL_POWER, &proto_ctx_2);
  ddk::PowerProtocolClient proto_client_2 = ddk::PowerProtocolClient(&proto_ctx_2);
  EXPECT_EQ(dut_->GetDependentCount(), 1);
  proto_client_2.RegisterPowerDomain(50, 400);
  EXPECT_EQ(dut_->GetDependentCount(), 2);
  EXPECT_EQ(parent_power_->power_domain_registered_count(), 1);
  EXPECT_EQ(power_impl_->power_domain_enabled_count(), 1);
}

TEST_F(GenericPowerTest, GetSupportedVoltageRange) {
  ddk::PowerProtocolClient proto_client = ddk::PowerProtocolClient(&proto_ctx_);
  uint32_t min_voltage = 0;
  uint32_t max_voltage = 0;
  proto_client.GetSupportedVoltageRange(&min_voltage, &max_voltage);
  EXPECT_EQ(min_voltage, 10);
  EXPECT_EQ(max_voltage, 1000);
}

TEST_F(GenericPowerTest, RequestVoltage_UnsuppportedVoltage) {
  ddk::PowerProtocolClient proto_client = ddk::PowerProtocolClient(&proto_ctx_);
  proto_client.RegisterPowerDomain(20, 800);
  uint32_t min_voltage = 0;
  uint32_t max_voltage = 0;
  proto_client.GetSupportedVoltageRange(&min_voltage, &max_voltage);
  EXPECT_EQ(min_voltage, 10);
  EXPECT_EQ(max_voltage, 1000);

  uint32_t out_voltage = 0;
  EXPECT_EQ(proto_client.RequestVoltage(1010, &out_voltage), ZX_ERR_INVALID_ARGS);
}

TEST_F(GenericPowerTest, RequestVoltage) {
  ddk::PowerProtocolClient proto_client = ddk::PowerProtocolClient(&proto_ctx_);
  EXPECT_OK(proto_client.RegisterPowerDomain(20, 800));
  EXPECT_EQ(dut_->GetDependentCount(), 1);
  EXPECT_EQ(parent_power_->power_domain_registered_count(), 1);
  EXPECT_EQ(power_impl_->power_domain_enabled_count(), 1);

  power_protocol_t proto_ctx_2;
  dut_->DdkOpenProtocolSessionMultibindable(ZX_PROTOCOL_POWER, &proto_ctx_2);
  ddk::PowerProtocolClient proto_client_2 = ddk::PowerProtocolClient(&proto_ctx_2);
  EXPECT_EQ(dut_->GetDependentCount(), 1);
  EXPECT_OK(proto_client_2.RegisterPowerDomain(10, 400));
  EXPECT_EQ(dut_->GetDependentCount(), 2);
  uint32_t out_actual_voltage = 0;
  EXPECT_OK(proto_client_2.RequestVoltage(900, &out_actual_voltage));
  EXPECT_EQ(out_actual_voltage, 400);
  EXPECT_OK(proto_client_2.RequestVoltage(15, &out_actual_voltage));
  EXPECT_EQ(out_actual_voltage, 20);

  EXPECT_OK(proto_client_2.UnregisterPowerDomain());
  EXPECT_EQ(dut_->GetDependentCount(), 1);
  EXPECT_OK(proto_client.RequestVoltage(900, &out_actual_voltage));
  EXPECT_EQ(out_actual_voltage, 800);
  EXPECT_OK(proto_client.RequestVoltage(15, &out_actual_voltage));
  EXPECT_EQ(out_actual_voltage, 20);
}

TEST_F(GenericPowerTest, RequestVoltage_Unregistered) {
  ddk::PowerProtocolClient proto_client = ddk::PowerProtocolClient(&proto_ctx_);
  uint32_t out_actual_voltage;
  EXPECT_EQ(proto_client.RequestVoltage(900, &out_actual_voltage), ZX_ERR_UNAVAILABLE);
}

TEST_F(GenericPowerTest, FixedVoltageDomain) {
  auto dut_fixed = std::make_unique<PowerDevice>(fake_ddk::kFakeParent, 1, power_impl_->GetClient(),
                                                 parent_power_->GetClient(), 1000, 1000, true);
  power_protocol_t proto_ctx_2;
  dut_fixed->DdkOpenProtocolSessionMultibindable(ZX_PROTOCOL_POWER, &proto_ctx_2);
  ddk::PowerProtocolClient proto_client_2 = ddk::PowerProtocolClient(&proto_ctx_2);
  EXPECT_OK(proto_client_2.RegisterPowerDomain(0, 0));
  EXPECT_EQ(dut_fixed->GetDependentCount(), 1);
  EXPECT_EQ(parent_power_->power_domain_registered_count(), 1);
  EXPECT_EQ(power_impl_->power_domain_enabled_count(), 1);

  uint32_t min_voltage = 0, max_voltage = 0;
  EXPECT_EQ(proto_client_2.GetSupportedVoltageRange(&min_voltage, &max_voltage),
            ZX_ERR_NOT_SUPPORTED);

  uint32_t out_voltage = 0;
  EXPECT_EQ(proto_client_2.RequestVoltage(900, &out_voltage), ZX_ERR_NOT_SUPPORTED);
  EXPECT_OK(proto_client_2.UnregisterPowerDomain());
  EXPECT_EQ(dut_fixed->GetDependentCount(), 0);
  EXPECT_EQ(parent_power_->power_domain_unregistered_count(), 1);
  EXPECT_EQ(power_impl_->power_domain_disabled_count(), 1);
}

}  // namespace power
