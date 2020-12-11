// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "aml-cpu.h"

#include <lib/fake_ddk/fidl-helper.h>

#include <memory>
#include <vector>

#include <ddktl/device.h>
#include <ddktl/fidl.h>
#include <mock/ddktl/protocol/clock.h>
#include <mock/ddktl/protocol/power.h>
#include <sdk/lib/inspect/testing/cpp/zxtest/inspect.h>
#include <zxtest/zxtest.h>

namespace amlogic_cpu {

using llcpp::fuchsia::device::MAX_DEVICE_PERFORMANCE_STATES;
using CpuCtrlClient = fuchsia_cpuctrl::Device::SyncClient;
using inspect::InspectTestHelper;

#define MHZ(x) ((x)*1000000)

const std::vector<operating_point_t> kTestOperatingPoints = {
    {.freq_hz = MHZ(10), .volt_uv = 1500, .pd_id = 0},
    {.freq_hz = MHZ(9), .volt_uv = 1350, .pd_id = 0},
    {.freq_hz = MHZ(8), .volt_uv = 1200, .pd_id = 0},
    {.freq_hz = MHZ(7), .volt_uv = 1050, .pd_id = 0},
    {.freq_hz = MHZ(6), .volt_uv = 900, .pd_id = 0},
    {.freq_hz = MHZ(5), .volt_uv = 750, .pd_id = 0},
    {.freq_hz = MHZ(4), .volt_uv = 600, .pd_id = 0},
    {.freq_hz = MHZ(3), .volt_uv = 450, .pd_id = 0},
    {.freq_hz = MHZ(2), .volt_uv = 300, .pd_id = 0},
    {.freq_hz = MHZ(1), .volt_uv = 150, .pd_id = 0},
};

class AmlCpuTest : public AmlCpu {
 public:
  AmlCpuTest(const ddk::ClockProtocolClient&& plldiv16, const ddk::ClockProtocolClient&& cpudiv16,
             const ddk::ClockProtocolClient&& cpuscaler, const ddk::PowerProtocolClient&& pwr,
             const std::vector<operating_point_t> operating_points)
      : AmlCpu(nullptr, std::move(plldiv16), std::move(cpudiv16), std::move(cpuscaler),
               std::move(pwr), operating_points) {}

  static zx_status_t MessageOp(void* ctx, fidl_incoming_msg_t* msg, fidl_txn_t* txn) {
    return static_cast<AmlCpuTest*>(ctx)->DdkMessage(msg, txn);
  }
  zx_status_t InitTest() { return messenger_.SetMessageOp(this, AmlCpuTest::MessageOp); }

  zx::channel& GetMessengerChannel() { return messenger_.local(); }

  zx::vmo inspect_vmo() { return inspector_.DuplicateVmo(); }

 private:
  fake_ddk::FidlMessenger messenger_;
};

class AmlCpuTestFixture : public InspectTestHelper, public zxtest::Test {
 public:
  AmlCpuTestFixture()
      : dut_(ddk::ClockProtocolClient(pll_clock_.GetProto()),
             ddk::ClockProtocolClient(cpu_clock_.GetProto()),
             ddk::ClockProtocolClient(scaler_clock_.GetProto()),
             ddk::PowerProtocolClient(power_.GetProto()), kTestOperatingPoints),
        operating_points_(kTestOperatingPoints) {}

  void SetUp() override {
    ASSERT_OK(dut_.InitTest());
    // Notes on AmlCpu Initialization:
    //  + Should enable the CPU and PLL clocks.
    //  + Should initially assume that the device is in it's lowest performance state.
    //  + Should configure the device to it's highest performance state.
    pll_clock_.ExpectEnable(ZX_OK);
    cpu_clock_.ExpectEnable(ZX_OK);

    const operating_point_t& slowest = operating_points_.back();
    const operating_point_t& fastest = operating_points_.front();

    // The DUT should initialize.
    power_.ExpectGetSupportedVoltageRange(ZX_OK, slowest.volt_uv, fastest.volt_uv);
    power_.ExpectRegisterPowerDomain(ZX_OK, slowest.volt_uv, fastest.volt_uv);

    // The DUT scales up to the fastest available pstate.
    power_.ExpectRequestVoltage(ZX_OK, fastest.volt_uv, fastest.volt_uv);
    scaler_clock_.ExpectSetRate(ZX_OK, fastest.freq_hz);

    ASSERT_OK(dut_.Init());

    cpu_client_ = std::make_unique<CpuCtrlClient>(std::move(dut_.GetMessengerChannel()));
  }

 protected:
  void VerifyAll() {
    ASSERT_NO_FATAL_FAILURES(pll_clock_.VerifyAndClear());
    ASSERT_NO_FATAL_FAILURES(cpu_clock_.VerifyAndClear());
    ASSERT_NO_FATAL_FAILURES(scaler_clock_.VerifyAndClear());
    ASSERT_NO_FATAL_FAILURES(power_.VerifyAndClear());
  }

  ddk::MockClock pll_clock_;
  ddk::MockClock cpu_clock_;
  ddk::MockClock scaler_clock_;
  ddk::MockPower power_;

  AmlCpuTest dut_;
  std::unique_ptr<CpuCtrlClient> cpu_client_;

  const std::vector<operating_point_t> operating_points_;
};

TEST_F(AmlCpuTestFixture, TestGetPerformanceStateInfo) {
  // Make sure that we can get information about all the supported pstates.
  for (size_t i = 0; i < kTestOperatingPoints.size(); i++) {
    const uint32_t pstate = static_cast<uint32_t>(i);
    auto pstateInfo = cpu_client_->GetPerformanceStateInfo(pstate);

    // First, make sure there were no transport errors.
    ASSERT_OK(pstateInfo.status());

    // Then make sure that the driver accepted the call.
    ASSERT_FALSE(pstateInfo->result.is_err());

    // Then make sure that we're getting the expected frequency and voltage values.
    EXPECT_EQ(pstateInfo->result.response().info.frequency_hz, kTestOperatingPoints[i].freq_hz);
    EXPECT_EQ(pstateInfo->result.response().info.voltage_uv, kTestOperatingPoints[i].volt_uv);
  }

  // Make sure that we can't get any information about pstates that don't
  // exist.
  for (size_t i = kTestOperatingPoints.size(); i < MAX_DEVICE_PERFORMANCE_STATES; i++) {
    const uint32_t pstate = static_cast<uint32_t>(i);
    auto pstateInfo = cpu_client_->GetPerformanceStateInfo(pstate);

    // Even if it's an unsupported pstate, we still expect the transport to
    // deliver the message successfully.
    ASSERT_OK(pstateInfo.status());

    // Make sure that the driver returns an error, however.
    EXPECT_TRUE(pstateInfo->result.is_err());
  }

  ASSERT_NO_FATAL_FAILURES(VerifyAll());
}

TEST_F(AmlCpuTestFixture, TestSetPerformanceState) {
  // Scale to the lowest performance state.
  const uint32_t min_pstate_index = static_cast<uint32_t>(kTestOperatingPoints.size() - 1);
  const operating_point_t& min_pstate = kTestOperatingPoints[min_pstate_index];

  scaler_clock_.ExpectSetRate(ZX_OK, min_pstate.freq_hz);
  power_.ExpectRequestVoltage(ZX_OK, min_pstate.volt_uv, min_pstate.volt_uv);

  uint32_t out_state = UINT32_MAX;
  zx_status_t result = dut_.DdkSetPerformanceState(min_pstate_index, &out_state);
  EXPECT_OK(result);
  EXPECT_EQ(out_state, min_pstate_index);

  // Scale to the highest performance state.
  const uint32_t max_pstate_index = 0;
  const operating_point_t& max_pstate = kTestOperatingPoints[max_pstate_index];

  scaler_clock_.ExpectSetRate(ZX_OK, max_pstate.freq_hz);
  power_.ExpectRequestVoltage(ZX_OK, max_pstate.volt_uv, max_pstate.volt_uv);

  out_state = UINT32_MAX;
  result = dut_.DdkSetPerformanceState(max_pstate_index, &out_state);
  EXPECT_OK(result);
  EXPECT_EQ(out_state, max_pstate_index);

  // Set to the pstate that we're already at and make sure that it's a no-op.
  result = dut_.DdkSetPerformanceState(max_pstate_index, &out_state);
  EXPECT_OK(result);
  EXPECT_EQ(out_state, max_pstate_index);

  ASSERT_NO_FATAL_FAILURES(VerifyAll());
}

TEST_F(AmlCpuTestFixture, TestSetCpuInfo) {
  uint32_t test_cpu_version = 0x28200b02;
  dut_.SetCpuInfo(test_cpu_version);
  ASSERT_NO_FATAL_FAILURES(ReadInspect(dut_.inspect_vmo()));
  auto* cpu_info = hierarchy().GetByPath({"cpu_info_service"});
  ASSERT_TRUE(cpu_info);

  // cpu_major_revision : 40
  ASSERT_NO_FATAL_FAILURES(CheckProperty<inspect::UintPropertyValue>(
      cpu_info->node(), "cpu_major_revision", inspect::UintPropertyValue(40)));
  // cpu_minor_revision : 11
  ASSERT_NO_FATAL_FAILURES(CheckProperty<inspect::UintPropertyValue>(
      cpu_info->node(), "cpu_minor_revision", inspect::UintPropertyValue(11)));
  // cpu_package_id : 2
  ASSERT_NO_FATAL_FAILURES(CheckProperty<inspect::UintPropertyValue>(
      cpu_info->node(), "cpu_package_id", inspect::UintPropertyValue(2)));
}

}  // namespace amlogic_cpu
