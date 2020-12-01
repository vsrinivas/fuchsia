// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "aml-cpu.h"

#include <fuchsia/hardware/thermal/llcpp/fidl.h>
#include <lib/device-protocol/pdev.h>
#include <lib/fake_ddk/fake_ddk.h>
#include <lib/fake_ddk/fidl-helper.h>
#include <lib/inspect/cpp/reader.h>

#include <memory>
#include <vector>

#include <ddktl/device.h>
#include <ddktl/fidl.h>
#include <ddktl/protocol/composite.h>
#include <ddktl/protocol/platform/device.h>
#include <ddktl/protocol/thermal.h>
#include <fake-mmio-reg/fake-mmio-reg.h>
#include <fbl/array.h>
#include <zxtest/zxtest.h>

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

  size_t num_devices_added() const { return devices_.size(); }

 private:
  // The bind function intentionally leaks created devices, so they must be owned here.
  std::vector<std::unique_ptr<AmlCpu>> devices_;
};

class FakeComposite : public ddk::CompositeProtocol<FakeComposite> {
 public:
  explicit FakeComposite(zx_device_t* parent)
      : proto_({&composite_protocol_ops_, this}), parent_(parent) {}

  const composite_protocol_t* proto() const { return &proto_; }

  uint32_t CompositeGetFragmentCount() { return static_cast<uint32_t>(kNumFragments); }

  // In typical usage of fake_ddk, kFakeParent is the only device that exists, and all faked
  // protocols are bound to it. Hence, the list of fragments is a repeated list of the parent
  // device.
  void CompositeGetFragments(composite_device_fragment_t* comp_list, size_t comp_count,
                             size_t* comp_actual) {
    size_t comp_cur;

    for (comp_cur = 0; comp_cur < comp_count; comp_cur++) {
      strncpy(comp_list[comp_cur].name, "unamed-fragment", 32);
      comp_list[comp_cur].device = parent_;
    }

    if (comp_actual != nullptr) {
      *comp_actual = comp_cur;
    }
  }

  bool CompositeGetFragment(const char* name, zx_device_t** out) {
    *out = parent_;
    return true;
  }

 private:
  // AmlCpu expects two fragments -- pdev and the thermal device.
  static constexpr size_t kNumFragments = 2;

  composite_protocol_t proto_;
  zx_device_t* parent_;
};

// Fake platform device that exposes CPU version via MMIO.
class FakePDev : public ddk::PDevProtocol<FakePDev, ddk::base_protocol> {
 public:
  FakePDev() : proto_({&pdev_protocol_ops_, this}) {
    regs_ = std::make_unique<ddk_fake::FakeMmioReg[]>(kRegCount);
    mmio_ = std::make_unique<ddk_fake::FakeMmioRegRegion>(regs_.get(), sizeof(uint32_t), kRegCount);
    (*mmio_)[kCpuVersionOffset].SetReadCallback([]() { return kCpuVersion; });
  }

  const pdev_protocol_t* proto() const { return &proto_; }

  zx_status_t PDevGetMmio(uint32_t index, pdev_mmio_t* out_mmio) {
    EXPECT_EQ(index, 0);
    out_mmio->offset = reinterpret_cast<size_t>(this);
    return ZX_OK;
  }

  // Not needed by this test
  zx_status_t PDevGetInterrupt(uint32_t index, uint32_t flags, zx::interrupt* out_irq) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  zx_status_t PDevGetBti(uint32_t index, zx::bti* out_bti) { return ZX_ERR_NOT_SUPPORTED; }
  zx_status_t PDevGetSmc(uint32_t index, zx::resource* out_resource) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  zx_status_t PDevGetDeviceInfo(pdev_device_info_t* out_info) { return ZX_ERR_NOT_SUPPORTED; }
  zx_status_t PDevGetBoardInfo(pdev_board_info_t* out_info) { return ZX_ERR_NOT_SUPPORTED; }

  ddk::MmioBuffer mmio() { return ddk::MmioBuffer(mmio_->GetMmioBuffer()); }

 private:
  static constexpr size_t kCpuVersionOffset = 0x220;
  static constexpr size_t kRegCount = kCpuVersionOffset / sizeof(uint32_t) + 1;

  // Note: FakeMmioReg's read callback returns a uint64_t, which is then cast to uint32_t when
  // AmlCpu calls FakeMmioRegRegion::Read32.
  constexpr static uint64_t kCpuVersion = 43;

  pdev_protocol_t proto_;
  std::unique_ptr<ddk_fake::FakeMmioReg[]> regs_;
  std::unique_ptr<ddk_fake::FakeMmioRegRegion> mmio_;
};

using CpuCtrlSyncClient = fuchsia_cpuctrl::Device::SyncClient;
using ThermalSyncClient = fuchsia_thermal::Device::SyncClient;
using llcpp::fuchsia::device::MAX_DEVICE_PERFORMANCE_STATES;

constexpr size_t kBigClusterIdx =
    static_cast<size_t>(fuchsia_thermal::PowerDomain::BIG_CLUSTER_POWER_DOMAIN);
constexpr size_t kLittleClusterIdx =
    static_cast<size_t>(fuchsia_thermal::PowerDomain::LITTLE_CLUSTER_POWER_DOMAIN);

constexpr size_t PowerDomainToIndex(fuchsia_thermal::PowerDomain pd) {
  switch (pd) {
    case fuchsia_thermal::PowerDomain::LITTLE_CLUSTER_POWER_DOMAIN:
      return kLittleClusterIdx;
    case fuchsia_thermal::PowerDomain::BIG_CLUSTER_POWER_DOMAIN:
      return kBigClusterIdx;
  }
  __UNREACHABLE;
}

const fuchsia_thermal::OperatingPoint kFakeOperatingPoints = []() {
  fuchsia_thermal::OperatingPoint result;

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

const fuchsia_thermal::ThermalDeviceInfo kDefaultDeviceInfo = []() {
  fuchsia_thermal::ThermalDeviceInfo result;

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

class FakeAmlThermal;
using TestDeviceType = ddk::Device<FakeAmlThermal, ddk::Messageable>;

class FakeAmlThermal : TestDeviceType, fuchsia_thermal::Device::Interface {
 public:
  FakeAmlThermal()
      : TestDeviceType(nullptr), active_operating_point_(0), device_info_(kDefaultDeviceInfo) {}
  ~FakeAmlThermal() {}

  // Manage the Fake FIDL Message Loop
  zx_status_t Init(std::optional<zx::channel> remote);
  static zx_status_t MessageOp(void* ctx, fidl_incoming_msg_t* msg, fidl_txn_t* txn);
  zx::channel& GetMessengerChannel() { return messenger_.local(); }

  // Accessor
  uint16_t ActiveOperatingPoint() const { return active_operating_point_; }

  zx_status_t DdkMessage(fidl_incoming_msg_t* msg, fidl_txn_t* txn);
  void DdkRelease() {}

  void set_device_info(const fuchsia_thermal::ThermalDeviceInfo& device_info) {
    device_info_ = device_info;
  }

 private:
  // Implement Thermal FIDL Protocol.
  void GetInfo(GetInfoCompleter::Sync& completer);
  void GetDeviceInfo(GetDeviceInfoCompleter::Sync& completer);
  void GetDvfsInfo(fuchsia_thermal::PowerDomain pd, GetDvfsInfoCompleter::Sync& completer);
  void GetTemperatureCelsius(GetTemperatureCelsiusCompleter::Sync& completer);
  void GetStateChangeEvent(GetStateChangeEventCompleter::Sync& completer);
  void GetStateChangePort(GetStateChangePortCompleter::Sync& completer);
  void SetTripCelsius(uint32_t id, float temp, SetTripCelsiusCompleter::Sync& completer);
  void GetDvfsOperatingPoint(fuchsia_thermal::PowerDomain pd,
                             GetDvfsOperatingPointCompleter::Sync& completer);
  void SetDvfsOperatingPoint(uint16_t op_idx, fuchsia_thermal::PowerDomain pd,
                             SetDvfsOperatingPointCompleter::Sync& completer);
  void GetFanLevel(GetFanLevelCompleter::Sync& completer);
  void SetFanLevel(uint32_t fan_level, SetFanLevelCompleter::Sync& completer);

  uint16_t active_operating_point_;
  fake_ddk::FidlMessenger messenger_;
  fuchsia_thermal::ThermalDeviceInfo device_info_;
};

zx_status_t FakeAmlThermal::MessageOp(void* ctx, fidl_incoming_msg_t* msg, fidl_txn_t* txn) {
  return static_cast<FakeAmlThermal*>(ctx)->DdkMessage(msg, txn);
}

zx_status_t FakeAmlThermal::Init(std::optional<zx::channel> remote) {
  return messenger_.SetMessageOp(this, FakeAmlThermal::MessageOp, std::move(remote));
}

zx_status_t FakeAmlThermal::DdkMessage(fidl_incoming_msg_t* msg, fidl_txn_t* txn) {
  DdkTransaction transaction(txn);
  fuchsia_thermal::Device::Dispatch(this, msg, &transaction);
  return transaction.Status();
}

void FakeAmlThermal::GetInfo(GetInfoCompleter::Sync& completer) {
  fuchsia_thermal::ThermalInfo result;

  result.state = 0;
  result.passive_temp_celsius = 0;
  result.critical_temp_celsius = 0;
  result.max_trip_count = 0;

  completer.Reply(ZX_OK, fidl::unowned_ptr(&result));
}

void FakeAmlThermal::GetDeviceInfo(GetDeviceInfoCompleter::Sync& completer) {
  fuchsia_thermal::ThermalDeviceInfo result = device_info_;
  completer.Reply(ZX_OK, fidl::unowned_ptr(&result));
}

void FakeAmlThermal::GetDvfsInfo(fuchsia_thermal::PowerDomain pd,
                                 GetDvfsInfoCompleter::Sync& completer) {
  fuchsia_thermal::ThermalDeviceInfo device_info = device_info_;
  fuchsia_thermal::OperatingPoint result = device_info.opps[PowerDomainToIndex(pd)];
  completer.Reply(ZX_OK, fidl::unowned_ptr(&result));
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

void FakeAmlThermal::GetDvfsOperatingPoint(fuchsia_thermal::PowerDomain pd,
                                           GetDvfsOperatingPointCompleter::Sync& completer) {
  if (pd == fuchsia_thermal::PowerDomain::LITTLE_CLUSTER_POWER_DOMAIN) {
    completer.Reply(ZX_ERR_NOT_SUPPORTED, 0);
    return;
  }

  completer.Reply(ZX_OK, active_operating_point_);
}

void FakeAmlThermal::SetDvfsOperatingPoint(uint16_t idx, fuchsia_thermal::PowerDomain pd,
                                           SetDvfsOperatingPointCompleter::Sync& completer) {
  if (pd == fuchsia_thermal::PowerDomain::LITTLE_CLUSTER_POWER_DOMAIN) {
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

  void set_device_info(const fuchsia_thermal::ThermalDeviceInfo& device_info) {
    device_info_ = device_info;
  }

 private:
  thermal_protocol_t proto_;
  fuchsia_thermal::ThermalDeviceInfo device_info_;
  std::unique_ptr<FakeAmlThermal> fidl_service_;
};

// Fixture that supports tests of AmlCpu::Create.
class AmlCpuBindingTest : public zxtest::Test {
 public:
  AmlCpuBindingTest() : composite_(fake_ddk::kFakeParent) {
    static constexpr size_t kNumBindProtocols = 3;

    fbl::Array<fake_ddk::ProtocolEntry> protocols(new fake_ddk::ProtocolEntry[kNumBindProtocols],
                                                  kNumBindProtocols);
    protocols[0] = {ZX_PROTOCOL_COMPOSITE,
                    *reinterpret_cast<const fake_ddk::Protocol*>(composite_.proto())};
    protocols[1] = {ZX_PROTOCOL_PDEV, *reinterpret_cast<const fake_ddk::Protocol*>(pdev_.proto())};
    protocols[2] = {ZX_PROTOCOL_THERMAL,
                    *reinterpret_cast<const fake_ddk::Protocol*>(thermal_device_.proto())};
    ddk_.SetProtocols(std::move(protocols));
  }

  zx_device_t* parent() { return fake_ddk::FakeParent(); }

 protected:
  Bind ddk_;
  FakeComposite composite_;
  FakePDev pdev_;
  FakeThermalDevice thermal_device_;
};

TEST_F(AmlCpuBindingTest, OneDomain) {
  ASSERT_OK(AmlCpu::Create(nullptr, parent()));
  ASSERT_EQ(ddk_.num_devices_added(), 1);
}

TEST_F(AmlCpuBindingTest, TwoDomains) {
  // Set up device info that defines two power domains.
  thermal_device_.set_device_info([]() {
    fuchsia_thermal::ThermalDeviceInfo result;

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
}

class AmlCpuTest : public AmlCpu {
 public:
  AmlCpuTest(ThermalSyncClient thermal) : AmlCpu(nullptr, std::move(thermal), kBigClusterIdx) {}

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

class InspectTestHelper {
 public:
  InspectTestHelper() {}

  void ReadInspect(const zx::vmo& vmo) {
    hierarchy_ = inspect::ReadFromVmo(vmo);
    ASSERT_TRUE(hierarchy_.is_ok());
  }

  inspect::Hierarchy& hierarchy() { return hierarchy_.value(); }

  template <typename T>
  void CheckProperty(const inspect::NodeValue& node, std::string property, T expected_value) {
    const T* actual_value = node.get_property<T>(property);
    ASSERT_TRUE(actual_value);
    EXPECT_EQ(expected_value.value(), actual_value->value());
  }

 private:
  fit::result<inspect::Hierarchy> hierarchy_;
};

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
  for (uint32_t i = kFakeOperatingPoints.count; i < MAX_DEVICE_PERFORMANCE_STATES; i++) {
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
  for (uint32_t i = kFakeOperatingPoints.count; i < MAX_DEVICE_PERFORMANCE_STATES; i++) {
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

}  // namespace amlogic_cpu

// Redefine PDevMakeMmioBufferWeak per the recommendation in pdev.h.
zx_status_t ddk::PDevMakeMmioBufferWeak(const pdev_mmio_t& pdev_mmio,
                                        std::optional<MmioBuffer>* mmio, uint32_t cache_policy) {
  auto* test_harness = reinterpret_cast<amlogic_cpu::FakePDev*>(pdev_mmio.offset);
  mmio->emplace(test_harness->mmio());
  return ZX_OK;
}
