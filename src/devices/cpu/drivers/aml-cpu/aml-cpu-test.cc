// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "aml-cpu.h"

#include <fuchsia/hardware/clock/cpp/banjo-mock.h>
#include <fuchsia/hardware/power/cpp/banjo-mock.h>
#include <lib/device-protocol/pdev.h>
#include <lib/fake_ddk/fake_ddk.h>
#include <lib/fake_ddk/fidl-helper.h>

#include <memory>
#include <vector>

#include <ddktl/device.h>
#include <ddktl/fidl.h>
#include <fake-mmio-reg/fake-mmio-reg.h>
#include <sdk/lib/inspect/testing/cpp/zxtest/inspect.h>
#include <zxtest/zxtest.h>

#include "src/devices/bus/testing/fake-pdev/fake-pdev.h"

namespace amlogic_cpu {

using fuchsia_device::wire::kMaxDevicePerformanceStates;
using CpuCtrlClient = fidl::WireSyncClient<fuchsia_cpuctrl::Device>;
using inspect::InspectTestHelper;

#define MHZ(x) ((x)*1000000)

constexpr uint32_t kPdArmA53 = 1;

constexpr amlogic_cpu::perf_domain_t kPerfDomains[] = {
    {.id = kPdArmA53, .core_count = 4, .relative_performance = 255, .name = "S905D2 ARM A53"},
};

constexpr amlogic_cpu::operating_point_t kOperatingPointsMetadata[] = {
    {.freq_hz = 100'000'000, .volt_uv = 731'000, .pd_id = kPdArmA53},
    {.freq_hz = 250'000'000, .volt_uv = 731'000, .pd_id = kPdArmA53},
    {.freq_hz = 500'000'000, .volt_uv = 731'000, .pd_id = kPdArmA53},
    {.freq_hz = 667'000'000, .volt_uv = 731'000, .pd_id = kPdArmA53},
    {.freq_hz = 1'000'000'000, .volt_uv = 731'000, .pd_id = kPdArmA53},
    {.freq_hz = 1'200'000'000, .volt_uv = 731'000, .pd_id = kPdArmA53},
    {.freq_hz = 1'398'000'000, .volt_uv = 761'000, .pd_id = kPdArmA53},
    {.freq_hz = 1'512'000'000, .volt_uv = 791'000, .pd_id = kPdArmA53},
    {.freq_hz = 1'608'000'000, .volt_uv = 831'000, .pd_id = kPdArmA53},
    {.freq_hz = 1'704'000'000, .volt_uv = 861'000, .pd_id = kPdArmA53},
    {.freq_hz = 1'896'000'000, .volt_uv = 1'022'000, .pd_id = kPdArmA53},
};

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

const uint32_t kTestCoreCount = 1;

class FakeMmio {
 public:
  FakeMmio() {
    regs_ = std::make_unique<ddk_fake::FakeMmioReg[]>(kRegCount);
    mmio_ = std::make_unique<ddk_fake::FakeMmioRegRegion>(regs_.get(), sizeof(uint32_t), kRegCount);
    (*mmio_)[kCpuVersionOffset].SetReadCallback([]() { return kCpuVersion; });
  }

  fake_pdev::FakePDev::MmioInfo mmio_info() { return {.offset = reinterpret_cast<size_t>(this)}; }

  ddk::MmioBuffer mmio() { return ddk::MmioBuffer(mmio_->GetMmioBuffer()); }

 private:
  static constexpr size_t kCpuVersionOffset = 0x220;
  static constexpr size_t kRegCount = kCpuVersionOffset / sizeof(uint32_t) + 1;

  constexpr static uint64_t kCpuVersion = 43;

  std::unique_ptr<ddk_fake::FakeMmioReg[]> regs_;
  std::unique_ptr<ddk_fake::FakeMmioRegRegion> mmio_;
};

class Bind : public fake_ddk::Bind {
 public:
  Bind() : fake_ddk::Bind() {}

  zx_status_t DeviceAdd(zx_driver_t* drv, zx_device_t* parent, device_add_args_t* args,
                        zx_device_t** out) override {
    if (parent != fake_ddk::kFakeParent || !args || args->proto_id != ZX_PROTOCOL_CPU_CTRL ||
        args->ctx == nullptr) {
      return ZX_ERR_INVALID_ARGS;
    }

    devices_.push_back(std::unique_ptr<AmlCpu>(static_cast<AmlCpu*>(args->ctx)));
    return ZX_OK;
  }

  const std::vector<std::unique_ptr<AmlCpu>>& get_devices() { return devices_; }

  size_t num_devices_added() const { return devices_.size(); }

 private:
  std::vector<std::unique_ptr<AmlCpu>> devices_;
};

class FakePowerDevice : public ddk::PowerProtocol<FakePowerDevice, ddk::base_protocol> {
 public:
  FakePowerDevice() : proto_({&power_protocol_ops_, this}), voltage_(0) {}

  zx_status_t PowerRegisterPowerDomain(uint32_t min_needed_voltage,
                                       uint32_t max_supported_voltage) {
    return ZX_OK;
  }
  zx_status_t PowerUnregisterPowerDomain() { return ZX_OK; }
  zx_status_t PowerGetPowerDomainStatus(power_domain_status_t* out_status) { return ZX_OK; }
  zx_status_t PowerGetSupportedVoltageRange(uint32_t* min_voltage, uint32_t* max_voltage) {
    return ZX_OK;
  }

  zx_status_t PowerRequestVoltage(uint32_t voltage, uint32_t* actual_voltage) {
    voltage_ = voltage;
    *actual_voltage = voltage;
    return ZX_OK;
  }

  zx_status_t PowerGetCurrentVoltage(uint32_t index, uint32_t* current_voltage) {
    *current_voltage = voltage_;
    return ZX_OK;
  }

  zx_status_t PowerWritePmicCtrlReg(uint32_t reg_addr, uint32_t value) { return ZX_OK; }
  zx_status_t PowerReadPmicCtrlReg(uint32_t reg_addr, uint32_t* out_value) { return ZX_OK; }

  const power_protocol_t* proto() const { return &proto_; }

 private:
  power_protocol_t proto_;
  uint32_t voltage_;
};

class FakeClockDevice : public ddk::ClockProtocol<FakeClockDevice, ddk::base_protocol> {
 public:
  FakeClockDevice() : proto_({&clock_protocol_ops_, this}) {}

  zx_status_t ClockEnable() { return ZX_OK; }
  zx_status_t ClockDisable() { return ZX_OK; }
  zx_status_t ClockIsEnabled(bool* out_enabled) { return ZX_OK; }

  zx_status_t ClockSetRate(uint64_t hz) { return ZX_OK; }
  zx_status_t ClockQuerySupportedRate(uint64_t max_rate, uint64_t* out_max_supported_rate) {
    return ZX_OK;
  }
  zx_status_t ClockGetRate(uint64_t* out_current_rate) { return ZX_OK; }

  zx_status_t ClockSetInput(uint32_t idx) { return ZX_OK; }
  zx_status_t ClockGetNumInputs(uint32_t* out) { return ZX_OK; }
  zx_status_t ClockGetInput(uint32_t* out) { return ZX_OK; }

  const clock_protocol_t* proto() const { return &proto_; }

 private:
  clock_protocol_t proto_;
};

class AmlCpuBindingTest : public zxtest::Test {
 public:
  AmlCpuBindingTest() {
    pdev_.set_mmio(0, mmio_.mmio_info());

    static constexpr size_t kNumBindFragments = 5;

    fbl::Array<fake_ddk::FragmentEntry> fragments(new fake_ddk::FragmentEntry[kNumBindFragments],
                                                  kNumBindFragments);
    fragments[0] = pdev_.fragment();

    fragments[1].name = "power-01";
    fragments[1].protocols.emplace_back(fake_ddk::ProtocolEntry{
        ZX_PROTOCOL_POWER, *reinterpret_cast<const fake_ddk::Protocol*>(pwr_.proto())});

    fragments[2].name = "clock-pll-div16-01";
    fragments[2].protocols.emplace_back(fake_ddk::ProtocolEntry{
        ZX_PROTOCOL_CLOCK, *reinterpret_cast<const fake_ddk::Protocol*>(clk0_.proto())});

    fragments[3].name = "clock-cpu-div16-01";
    fragments[3].protocols.emplace_back(fake_ddk::ProtocolEntry{
        ZX_PROTOCOL_CLOCK, *reinterpret_cast<const fake_ddk::Protocol*>(clk1_.proto())});

    fragments[4].name = "clock-cpu-scaler-01";
    fragments[4].protocols.emplace_back(fake_ddk::ProtocolEntry{
        ZX_PROTOCOL_CLOCK, *reinterpret_cast<const fake_ddk::Protocol*>(clk2_.proto())});

    ddk_.SetFragments(std::move(fragments));

    ddk_.SetMetadata(DEVICE_METADATA_AML_PERF_DOMAINS, &kPerfDomains, sizeof(kPerfDomains));
  }

  zx_device_t* parent() { return fake_ddk::FakeParent(); }

 protected:
  Bind ddk_;
  fake_pdev::FakePDev pdev_;
  FakeMmio mmio_;

  FakePowerDevice pwr_;
  FakeClockDevice clk0_;
  FakeClockDevice clk1_;
  FakeClockDevice clk2_;
};

TEST_F(AmlCpuBindingTest, TrivialBinding) {
  ddk_.SetMetadata(DEVICE_METADATA_AML_OP_POINTS, &kOperatingPointsMetadata,
                   sizeof(kOperatingPointsMetadata));

  ASSERT_OK(AmlCpu::Create(nullptr, parent()));
  ASSERT_EQ(ddk_.num_devices_added(), 1);
}

TEST_F(AmlCpuBindingTest, UnorderedOperatingPoints) {
  // AML CPU's bind hook expects that all operating points are strictly
  // ordered and it should handle the situation where there are duplicate
  // frequencies.
  constexpr amlogic_cpu::operating_point_t kOperatingPointsMetadata[] = {
      {.freq_hz = MHZ(1), .volt_uv = 200'000, .pd_id = kPdArmA53},
      {.freq_hz = MHZ(1), .volt_uv = 100'000, .pd_id = kPdArmA53},
      {.freq_hz = MHZ(1), .volt_uv = 300'000, .pd_id = kPdArmA53},
  };

  ddk_.SetMetadata(DEVICE_METADATA_AML_OP_POINTS, &kOperatingPointsMetadata,
                   sizeof(kOperatingPointsMetadata));

  ASSERT_OK(AmlCpu::Create(nullptr, parent()));
  ASSERT_EQ(ddk_.num_devices_added(), 1);

  const auto& devices = ddk_.get_devices();
  ASSERT_EQ(devices.size(), 1);

  const auto& dev = devices[0];

  uint32_t out_state;
  EXPECT_OK(dev->DdkSetPerformanceState(0, &out_state));
  EXPECT_EQ(out_state, 0);

  uint32_t voltage = 0;
  EXPECT_OK(pwr_.PowerGetCurrentVoltage(0, &voltage));
  EXPECT_EQ(voltage, 300'000);
}

class AmlCpuTest : public AmlCpu {
 public:
  AmlCpuTest(const ddk::ClockProtocolClient&& plldiv16, const ddk::ClockProtocolClient&& cpudiv16,
             const ddk::ClockProtocolClient&& cpuscaler, const ddk::PowerProtocolClient&& pwr,
             const std::vector<operating_point_t> operating_points, const uint32_t core_count)
      : AmlCpu(nullptr, std::move(plldiv16), std::move(cpudiv16), std::move(cpuscaler),
               std::move(pwr), operating_points, core_count) {}

  static zx_status_t MessageOp(void* ctx, fidl_incoming_msg_t* msg, fidl_txn_t* txn) {
    return static_cast<AmlCpuTest*>(ctx)->ddk_device_proto_.message(ctx, msg, txn);
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
             ddk::PowerProtocolClient(power_.GetProto()), kTestOperatingPoints, kTestCoreCount),
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

    cpu_client_ = CpuCtrlClient(std::move(dut_.GetMessengerChannel()));
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
  CpuCtrlClient cpu_client_;

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
  for (size_t i = kTestOperatingPoints.size(); i < kMaxDevicePerformanceStates; i++) {
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
  ASSERT_NO_FATAL_FAILURES(
      CheckProperty(cpu_info->node(), "cpu_major_revision", inspect::UintPropertyValue(40)));
  // cpu_minor_revision : 11
  ASSERT_NO_FATAL_FAILURES(
      CheckProperty(cpu_info->node(), "cpu_minor_revision", inspect::UintPropertyValue(11)));
  // cpu_package_id : 2
  ASSERT_NO_FATAL_FAILURES(
      CheckProperty(cpu_info->node(), "cpu_package_id", inspect::UintPropertyValue(2)));
}

TEST_F(AmlCpuTestFixture, TestGetLogicalCoreCount) {
  auto coreCountResp = cpu_client_->GetNumLogicalCores();

  ASSERT_OK(coreCountResp.status());

  EXPECT_EQ(coreCountResp->count, kTestCoreCount);
}

}  // namespace amlogic_cpu

// Redefine PDevMakeMmioBufferWeak per the recommendation in pdev.h.
zx_status_t ddk::PDevMakeMmioBufferWeak(const pdev_mmio_t& pdev_mmio,
                                        std::optional<MmioBuffer>* mmio, uint32_t cache_policy) {
  auto* test_harness = reinterpret_cast<amlogic_cpu::FakeMmio*>(pdev_mmio.offset);
  mmio->emplace(test_harness->mmio());
  return ZX_OK;
}
