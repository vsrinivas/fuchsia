// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "aml-cpu.h"

#include <fuchsia/hardware/platform/device/cpp/banjo.h>
#include <fuchsia/hardware/thermal/cpp/banjo.h>
#include <fuchsia/hardware/thermal/llcpp/fidl.h>
#include <lib/device-protocol/pdev.h>
#include <lib/fake_ddk/fake_ddk.h>
#include <lib/fake_ddk/fidl-helper.h>

#include <algorithm>
#include <memory>
#include <vector>

#include <ddktl/device.h>
#include <ddktl/fidl.h>
#include <fake-mmio-reg/fake-mmio-reg.h>
#include <fbl/array.h>
#include <sdk/lib/inspect/testing/cpp/zxtest/inspect.h>
#include <soc/aml-common/aml-cpu-metadata.h>
#include <zxtest/zxtest.h>

#include "src/devices/bus/testing/fake-pdev/fake-pdev.h"

namespace amlogic_cpu {

// This subclass of Bind is only used to test the binding of AmlCpu. DeviceAdd is overridden
// to test expectation on devices that are added.
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
  // The bind function intentionally leaks created devices, so they must be owned here.
  std::vector<std::unique_ptr<AmlCpu>> devices_;
};

// Fake MMIO  that exposes CPU version.
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

  // Note: FakeMmioReg's read callback returns a uint64_t, which is then cast to uint32_t when
  // AmlCpu calls FakeMmioRegRegion::Read32.
  constexpr static uint64_t kCpuVersion = 43;

  std::unique_ptr<ddk_fake::FakeMmioReg[]> regs_;
  std::unique_ptr<ddk_fake::FakeMmioRegRegion> mmio_;
};

using CpuCtrlSyncClient = fidl::WireSyncClient<fuchsia_cpuctrl::Device>;
using ThermalSyncClient = fidl::WireSyncClient<fuchsia_thermal::Device>;
using fuchsia_device::wire::kMaxDevicePerformanceStates;

constexpr size_t kBigClusterIdx =
    static_cast<size_t>(fuchsia_thermal::wire::PowerDomain::kBigClusterPowerDomain);
constexpr size_t kLittleClusterIdx =
    static_cast<size_t>(fuchsia_thermal::wire::PowerDomain::kLittleClusterPowerDomain);

constexpr uint32_t kBigClusterCoreCount = 4;
constexpr uint32_t kLittleClusterCoreCount = 2;

constexpr legacy_cluster_size_t kClusterSizeMetadata[] = {
    {
        .pd_id = kBigClusterIdx,
        .core_count = kBigClusterCoreCount,
    },
    {
        .pd_id = kLittleClusterIdx,
        .core_count = kLittleClusterCoreCount,
    },
};

constexpr size_t PowerDomainToIndex(fuchsia_thermal::wire::PowerDomain pd) {
  switch (pd) {
    case fuchsia_thermal::wire::PowerDomain::kLittleClusterPowerDomain:
      return kLittleClusterIdx;
    case fuchsia_thermal::wire::PowerDomain::kBigClusterPowerDomain:
      return kBigClusterIdx;
  }
  __UNREACHABLE;
}

const fuchsia_thermal::wire::OperatingPoint kFakeOperatingPoints = []() {
  fuchsia_thermal::wire::OperatingPoint result;

  result.count = 3;
  result.latency = 0;
  result.opp[0].volt_uv = 1;
  result.opp[0].freq_hz = 100;
  result.opp[1].volt_uv = 2;
  result.opp[1].freq_hz = 200;
  result.opp[2].volt_uv = 3;
  result.opp[2].freq_hz = 300;

  return result;
}();

const fuchsia_thermal::wire::ThermalDeviceInfo kDefaultDeviceInfo = []() {
  fuchsia_thermal::wire::ThermalDeviceInfo result;

  result.active_cooling = false;
  result.passive_cooling = false;
  result.gpu_throttling = false;
  result.num_trip_points = 0;
  result.big_little = false;
  result.critical_temp_celsius = 0;

  result.opps[kLittleClusterIdx].count = 0;
  result.opps[kBigClusterIdx] = kFakeOperatingPoints;

  return result;
}();

class FakeAmlThermal : fidl::WireInterface<fuchsia_thermal::Device> {
 public:
  FakeAmlThermal() : active_operating_point_(0), device_info_(kDefaultDeviceInfo) {}
  ~FakeAmlThermal() {}

  // Manage the Fake FIDL Message Loop
  zx_status_t Init(std::optional<zx::channel> remote);
  static zx_status_t MessageOp(void* ctx, fidl_incoming_msg_t* msg, fidl_txn_t* txn);
  zx::channel& GetMessengerChannel() { return messenger_.local(); }

  // Accessor
  uint16_t ActiveOperatingPoint() const { return active_operating_point_; }

  zx_status_t DdkMessage(fidl_incoming_msg_t* msg, fidl_txn_t* txn);
  void DdkRelease() {}

  void set_device_info(const fuchsia_thermal::wire::ThermalDeviceInfo& device_info) {
    device_info_ = device_info;
  }

 private:
  // Implement Thermal FIDL Protocol.
  void GetInfo(GetInfoCompleter::Sync& completer);
  void GetDeviceInfo(GetDeviceInfoCompleter::Sync& completer);
  void GetDvfsInfo(fuchsia_thermal::wire::PowerDomain pd, GetDvfsInfoCompleter::Sync& completer);
  void GetTemperatureCelsius(GetTemperatureCelsiusCompleter::Sync& completer);
  void GetStateChangeEvent(GetStateChangeEventCompleter::Sync& completer);
  void GetStateChangePort(GetStateChangePortCompleter::Sync& completer);
  void SetTripCelsius(uint32_t id, float temp, SetTripCelsiusCompleter::Sync& completer);
  void GetDvfsOperatingPoint(fuchsia_thermal::wire::PowerDomain pd,
                             GetDvfsOperatingPointCompleter::Sync& completer);
  void SetDvfsOperatingPoint(uint16_t op_idx, fuchsia_thermal::wire::PowerDomain pd,
                             SetDvfsOperatingPointCompleter::Sync& completer);
  void GetFanLevel(GetFanLevelCompleter::Sync& completer);
  void SetFanLevel(uint32_t fan_level, SetFanLevelCompleter::Sync& completer);

  uint16_t active_operating_point_;
  fake_ddk::FidlMessenger messenger_;
  fuchsia_thermal::wire::ThermalDeviceInfo device_info_;
};

zx_status_t FakeAmlThermal::MessageOp(void* ctx, fidl_incoming_msg_t* msg, fidl_txn_t* txn) {
  return static_cast<FakeAmlThermal*>(ctx)->DdkMessage(msg, txn);
}

zx_status_t FakeAmlThermal::Init(std::optional<zx::channel> remote) {
  return messenger_.SetMessageOp(this, FakeAmlThermal::MessageOp, std::move(remote));
}

zx_status_t FakeAmlThermal::DdkMessage(fidl_incoming_msg_t* msg, fidl_txn_t* txn) {
  DdkTransaction transaction(txn);
  fidl::WireDispatch<fuchsia_thermal::Device>(this, msg, &transaction);
  return transaction.Status();
}

void FakeAmlThermal::GetInfo(GetInfoCompleter::Sync& completer) {
  fuchsia_thermal::wire::ThermalInfo result;

  result.state = 0;
  result.passive_temp_celsius = 0;
  result.critical_temp_celsius = 0;
  result.max_trip_count = 0;

  completer.Reply(ZX_OK,
                  fidl::ObjectView<fuchsia_thermal::wire::ThermalInfo>::FromExternal(&result));
}

void FakeAmlThermal::GetDeviceInfo(GetDeviceInfoCompleter::Sync& completer) {
  fuchsia_thermal::wire::ThermalDeviceInfo result = device_info_;
  completer.Reply(
      ZX_OK, fidl::ObjectView<fuchsia_thermal::wire::ThermalDeviceInfo>::FromExternal(&result));
}

void FakeAmlThermal::GetDvfsInfo(fuchsia_thermal::wire::PowerDomain pd,
                                 GetDvfsInfoCompleter::Sync& completer) {
  fuchsia_thermal::wire::ThermalDeviceInfo device_info = device_info_;
  fuchsia_thermal::wire::OperatingPoint result = device_info.opps[PowerDomainToIndex(pd)];
  completer.Reply(ZX_OK,
                  fidl::ObjectView<fuchsia_thermal::wire::OperatingPoint>::FromExternal(&result));
}

void FakeAmlThermal::GetTemperatureCelsius(GetTemperatureCelsiusCompleter::Sync& completer) {
  completer.Reply(ZX_OK, 0.0);
}

void FakeAmlThermal::GetStateChangeEvent(GetStateChangeEventCompleter::Sync& completer) {
  zx::event invalid;
  completer.Reply(ZX_ERR_NOT_SUPPORTED, std::move(invalid));
}

void FakeAmlThermal::GetStateChangePort(GetStateChangePortCompleter::Sync& completer) {
  zx::port invalid;
  completer.Reply(ZX_ERR_NOT_SUPPORTED, std::move(invalid));
}

void FakeAmlThermal::SetTripCelsius(uint32_t id, float temp,
                                    SetTripCelsiusCompleter::Sync& completer) {
  completer.Reply(ZX_ERR_NOT_SUPPORTED);
}

void FakeAmlThermal::GetDvfsOperatingPoint(fuchsia_thermal::wire::PowerDomain pd,
                                           GetDvfsOperatingPointCompleter::Sync& completer) {
  if (pd == fuchsia_thermal::wire::PowerDomain::kLittleClusterPowerDomain) {
    completer.Reply(ZX_ERR_NOT_SUPPORTED, 0);
    return;
  }

  completer.Reply(ZX_OK, active_operating_point_);
}

void FakeAmlThermal::SetDvfsOperatingPoint(uint16_t idx, fuchsia_thermal::wire::PowerDomain pd,
                                           SetDvfsOperatingPointCompleter::Sync& completer) {
  if (pd == fuchsia_thermal::wire::PowerDomain::kLittleClusterPowerDomain) {
    completer.Reply(ZX_ERR_NOT_SUPPORTED);
    return;
  }

  active_operating_point_ = idx;
  completer.Reply(ZX_OK);
}

void FakeAmlThermal::GetFanLevel(GetFanLevelCompleter::Sync& completer) {
  completer.Reply(ZX_ERR_NOT_SUPPORTED, 0);
}

void FakeAmlThermal::SetFanLevel(uint32_t fan_level, SetFanLevelCompleter::Sync& completer) {
  completer.Reply(ZX_ERR_OUT_OF_RANGE);
}

// Fake device that exposes the thermal banjo protocol. Upon calling Connect, a new instance of
// FakeAmlThermal is created to serve a client, at which point any previous FakeThermalAml
// instance is destroyed.
class FakeThermalDevice : public ddk::ThermalProtocol<FakeThermalDevice, ddk::base_protocol> {
 public:
  FakeThermalDevice()
      : proto_({&thermal_protocol_ops_, this}), device_info_(kDefaultDeviceInfo), fidl_service_() {}

  zx_status_t ThermalConnect(zx::channel chan) {
    fidl_service_ = std::make_unique<FakeAmlThermal>();
    fidl_service_->set_device_info(device_info_);
    return fidl_service_->Init({std::move(chan)});
  }

  const thermal_protocol_t* proto() const { return &proto_; }

  void set_device_info(const fuchsia_thermal::wire::ThermalDeviceInfo& device_info) {
    device_info_ = device_info;
  }

 private:
  thermal_protocol_t proto_;
  fuchsia_thermal::wire::ThermalDeviceInfo device_info_;
  std::unique_ptr<FakeAmlThermal> fidl_service_;
};

// Fixture that supports tests of AmlCpu::Create.
class AmlCpuBindingTest : public zxtest::Test {
 public:
  AmlCpuBindingTest() {
    pdev_.set_mmio(0, mmio_.mmio_info());

    static constexpr size_t kNumBindFragments = 2;

    fbl::Array<fake_ddk::FragmentEntry> fragments(new fake_ddk::FragmentEntry[kNumBindFragments],
                                                  kNumBindFragments);
    fragments[0] = pdev_.fragment();
    fragments[1].name = "thermal";
    fragments[1].protocols.emplace_back(fake_ddk::ProtocolEntry{
        ZX_PROTOCOL_THERMAL,
        *reinterpret_cast<const fake_ddk::Protocol*>(thermal_device_.proto())});
    ddk_.SetFragments(std::move(fragments));

    ddk_.SetMetadata(DEVICE_METADATA_CLUSTER_SIZE_LEGACY, &kClusterSizeMetadata,
                     sizeof(kClusterSizeMetadata));
  }

  zx_device_t* parent() { return fake_ddk::FakeParent(); }

 protected:
  Bind ddk_;
  fake_pdev::FakePDev pdev_;
  FakeMmio mmio_;
  FakeThermalDevice thermal_device_;
};

TEST_F(AmlCpuBindingTest, OneDomain) {
  ASSERT_OK(AmlCpu::Create(nullptr, parent()));
  ASSERT_EQ(ddk_.num_devices_added(), 1);
}

TEST_F(AmlCpuBindingTest, TwoDomains) {
  // Set up device info that defines two power domains.
  thermal_device_.set_device_info([]() {
    fuchsia_thermal::wire::ThermalDeviceInfo result;

    result.active_cooling = false;
    result.passive_cooling = false;
    result.gpu_throttling = false;
    result.num_trip_points = 0;
    result.big_little = true;
    result.critical_temp_celsius = 0;

    result.opps[kLittleClusterIdx] = kFakeOperatingPoints;
    result.opps[kBigClusterIdx] = kFakeOperatingPoints;

    return result;
  }());

  ASSERT_OK(AmlCpu::Create(nullptr, parent()));
  ASSERT_EQ(ddk_.num_devices_added(), 2);

  const auto& devices = ddk_.get_devices();
  for (const auto& device : devices) {
    const size_t idx = device->PowerDomainIndex();

    // Find the cluster metadata that corresponds to this cluster index.
    const auto& cluster_size_meta_itr = std::find_if(
        std::begin(kClusterSizeMetadata), std::end(kClusterSizeMetadata),
        [idx](const legacy_cluster_size_t& elem) -> bool { return idx == elem.pd_id; });

    ASSERT_NE(cluster_size_meta_itr, std::end(kClusterSizeMetadata));
    ASSERT_EQ(cluster_size_meta_itr->core_count, device->ClusterCoreCount());
  }
}

class AmlCpuTest : public AmlCpu {
 public:
  AmlCpuTest(ThermalSyncClient thermal)
      : AmlCpu(nullptr, std::move(thermal), kBigClusterIdx, kBigClusterCoreCount) {}

  zx_status_t Init();
  static zx_status_t MessageOp(void* ctx, fidl_incoming_msg_t* msg, fidl_txn_t* txn);
  zx::channel& GetMessengerChannel() { return messenger_.local(); }

  zx::vmo inspect_vmo() { return inspector_.DuplicateVmo(); }

 private:
  fake_ddk::FidlMessenger messenger_;
};

zx_status_t AmlCpuTest::MessageOp(void* ctx, fidl_incoming_msg_t* msg, fidl_txn_t* txn) {
  return static_cast<AmlCpuTest*>(ctx)->DdkMessage(msg, txn);
}

zx_status_t AmlCpuTest::Init() { return messenger_.SetMessageOp(this, AmlCpuTest::MessageOp); }

using inspect::InspectTestHelper;

class AmlCpuTestFixture : public InspectTestHelper, public zxtest::Test {
 public:
  void SetUp() override;

 protected:
  FakeAmlThermal thermal_;

  std::unique_ptr<AmlCpuTest> dut_;
  std::unique_ptr<CpuCtrlSyncClient> cpu_client_;
};

void AmlCpuTestFixture::SetUp() {
  ASSERT_OK(thermal_.Init({}));
  ThermalSyncClient thermal_client(std::move(thermal_.GetMessengerChannel()));

  dut_ = std::make_unique<AmlCpuTest>(std::move(thermal_client));
  ASSERT_OK(dut_->Init());

  cpu_client_ = std::make_unique<CpuCtrlSyncClient>(std::move(dut_->GetMessengerChannel()));
}

TEST_F(AmlCpuTestFixture, TestGetPerformanceStateInfo) {
  // Make sure that we can get information about all the supported pstates.
  for (uint32_t i = 0; i < kFakeOperatingPoints.count; i++) {
    auto pstateInfo = cpu_client_->GetPerformanceStateInfo(i);

    // First, make sure there were no transport errors.
    ASSERT_OK(pstateInfo.status());

    // Then make sure that the driver accepted the call.
    ASSERT_FALSE(pstateInfo->result.is_err());

    // Then make sure that we're getting the accepted frequency and voltage values.
    EXPECT_EQ(pstateInfo->result.response().info.frequency_hz,
              kFakeOperatingPoints.opp[kFakeOperatingPoints.count - i - 1].freq_hz);
    EXPECT_EQ(pstateInfo->result.response().info.voltage_uv,
              kFakeOperatingPoints.opp[kFakeOperatingPoints.count - i - 1].volt_uv);
  }

  // Make sure that we can't get any information about pstates that don't
  // exist.
  for (uint32_t i = kFakeOperatingPoints.count; i < kMaxDevicePerformanceStates; i++) {
    auto pstateInfo = cpu_client_->GetPerformanceStateInfo(i);

    // Even if it's an unsupported pstate, we still expect the transport to
    // deliver the message successfully.
    ASSERT_OK(pstateInfo.status());

    // Make sure that the driver returns an error, however.
    EXPECT_TRUE(pstateInfo->result.is_err());
  }
}

TEST_F(AmlCpuTestFixture, TestSetPerformanceState) {
  // Make sure that we can drive the CPU to all of the supported performance
  // states.
  for (uint32_t i = 0; i < kFakeOperatingPoints.count; i++) {
    uint32_t out_state = UINT32_MAX;
    zx_status_t st = dut_->DdkSetPerformanceState(i, &out_state);

    // Make sure the call succeeded.
    EXPECT_OK(st);

    // Make sure we could actually drive the device into the state that we
    // expected.
    EXPECT_EQ(out_state, i);

    // Make sure that the call was forwarded to the thermal driver.
    const uint16_t kExpectedOperatingPoint =
        static_cast<uint16_t>(kFakeOperatingPoints.count - i - 1);
    EXPECT_EQ(kExpectedOperatingPoint, thermal_.ActiveOperatingPoint());
  }

  // Next make sure that we can't drive the CPU into any unsupported
  // performance states.
  for (uint32_t i = kFakeOperatingPoints.count; i < kMaxDevicePerformanceStates; i++) {
    const uint16_t kInitialOperatingPoint = thermal_.ActiveOperatingPoint();
    uint32_t out_state = UINT32_MAX;
    zx_status_t st = dut_->DdkSetPerformanceState(i, &out_state);

    // This is not a supported performance state.
    EXPECT_NOT_OK(st);

    // Make sure we haven't meddled with `out_state`
    EXPECT_EQ(out_state, UINT32_MAX);

    // Make sure we haven't meddled with the thermal driver's active
    // operating point.
    EXPECT_EQ(kInitialOperatingPoint, thermal_.ActiveOperatingPoint());
  }
}

TEST_F(AmlCpuTestFixture, TestSetCpuInfo) {
  uint32_t test_cpu_version = 0x28200b02;
  dut_->SetCpuInfo(test_cpu_version);
  ASSERT_NO_FATAL_FAILURES(ReadInspect(dut_->inspect_vmo()));
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

TEST_F(AmlCpuTestFixture, TestGetNumLogicalCores) {
  auto resp = cpu_client_->GetNumLogicalCores();

  ASSERT_OK(resp.status());

  EXPECT_EQ(resp->count, kBigClusterCoreCount);
}

}  // namespace amlogic_cpu

// Redefine PDevMakeMmioBufferWeak per the recommendation in pdev.h.
zx_status_t ddk::PDevMakeMmioBufferWeak(const pdev_mmio_t& pdev_mmio,
                                        std::optional<MmioBuffer>* mmio, uint32_t cache_policy) {
  auto* test_harness = reinterpret_cast<amlogic_cpu::FakeMmio*>(pdev_mmio.offset);
  mmio->emplace(test_harness->mmio());
  return ZX_OK;
}
