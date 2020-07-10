#include "aml-cpu.h"

#include <fuchsia/hardware/thermal/llcpp/fidl.h>
#include <lib/fake_ddk/fidl-helper.h>
#include <lib/inspect/cpp/reader.h>

#include <memory>

#include <ddktl/device.h>
#include <ddktl/fidl.h>
#include <zxtest/zxtest.h>

namespace amlogic_cpu {

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

const fuchsia_thermal::ThermalDeviceInfo kFakeThermalDeviceInfo = []() {
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
  FakeAmlThermal() : TestDeviceType(nullptr), active_operating_point_(0) {}
  ~FakeAmlThermal() {}

  // Manage the Fake FIDL Message Loop
  zx_status_t Init();
  static zx_status_t MessageOp(void* ctx, fidl_msg_t* msg, fidl_txn_t* txn);
  zx::channel& GetMessengerChannel() { return messenger_.local(); }

  // Accessor
  uint16_t ActiveOperatingPoint() const { return active_operating_point_; }

  zx_status_t DdkMessage(fidl_msg_t* msg, fidl_txn_t* txn);
  void DdkRelease() {}

 private:
  // Implement Thermal FIDL Protocol.
  void GetInfo(GetInfoCompleter::Sync completer);
  void GetDeviceInfo(GetDeviceInfoCompleter::Sync completer);
  void GetDvfsInfo(fuchsia_thermal::PowerDomain pd, GetDvfsInfoCompleter::Sync completer);
  void GetTemperatureCelsius(GetTemperatureCelsiusCompleter::Sync completer);
  void GetStateChangeEvent(GetStateChangeEventCompleter::Sync completer);
  void GetStateChangePort(GetStateChangePortCompleter::Sync completer);
  void SetTripCelsius(uint32_t id, float temp, SetTripCelsiusCompleter::Sync completer);
  void GetDvfsOperatingPoint(fuchsia_thermal::PowerDomain pd,
                             GetDvfsOperatingPointCompleter::Sync completer);
  void SetDvfsOperatingPoint(uint16_t op_idx, fuchsia_thermal::PowerDomain pd,
                             SetDvfsOperatingPointCompleter::Sync completer);
  void GetFanLevel(GetFanLevelCompleter::Sync completer);
  void SetFanLevel(uint32_t fan_level, SetFanLevelCompleter::Sync completer);

  uint16_t active_operating_point_;
  fake_ddk::FidlMessenger messenger_;
};

zx_status_t FakeAmlThermal::MessageOp(void* ctx, fidl_msg_t* msg, fidl_txn_t* txn) {
  return static_cast<FakeAmlThermal*>(ctx)->DdkMessage(msg, txn);
}

zx_status_t FakeAmlThermal::Init() {
  return messenger_.SetMessageOp(this, FakeAmlThermal::MessageOp);
}

zx_status_t FakeAmlThermal::DdkMessage(fidl_msg_t* msg, fidl_txn_t* txn) {
  DdkTransaction transaction(txn);
  fuchsia_thermal::Device::Dispatch(this, msg, &transaction);
  return transaction.Status();
}

void FakeAmlThermal::GetInfo(GetInfoCompleter::Sync completer) {
  fuchsia_thermal::ThermalInfo result;

  result.state = 0;
  result.passive_temp_celsius = 0;
  result.critical_temp_celsius = 0;
  result.max_trip_count = 0;

  completer.Reply(ZX_OK, fidl::unowned_ptr(&result));
}

void FakeAmlThermal::GetDeviceInfo(GetDeviceInfoCompleter::Sync completer) {
  fuchsia_thermal::ThermalDeviceInfo result = kFakeThermalDeviceInfo;
  completer.Reply(ZX_OK, fidl::unowned_ptr(&result));
}

void FakeAmlThermal::GetDvfsInfo(fuchsia_thermal::PowerDomain pd,
                                 GetDvfsInfoCompleter::Sync completer) {
  fuchsia_thermal::ThermalDeviceInfo device_info = kFakeThermalDeviceInfo;
  fuchsia_thermal::OperatingPoint result = device_info.opps[PowerDomainToIndex(pd)];
  completer.Reply(ZX_OK, fidl::unowned_ptr(&result));
}

void FakeAmlThermal::GetTemperatureCelsius(GetTemperatureCelsiusCompleter::Sync completer) {
  completer.Reply(ZX_OK, 0.0);
}

void FakeAmlThermal::GetStateChangeEvent(GetStateChangeEventCompleter::Sync completer) {
  zx::event invalid;
  completer.Reply(ZX_ERR_NOT_SUPPORTED, std::move(invalid));
}

void FakeAmlThermal::GetStateChangePort(GetStateChangePortCompleter::Sync completer) {
  zx::port invalid;
  completer.Reply(ZX_ERR_NOT_SUPPORTED, std::move(invalid));
}

void FakeAmlThermal::SetTripCelsius(uint32_t id, float temp,
                                    SetTripCelsiusCompleter::Sync completer) {
  completer.Reply(ZX_ERR_NOT_SUPPORTED);
}

void FakeAmlThermal::GetDvfsOperatingPoint(fuchsia_thermal::PowerDomain pd,
                                           GetDvfsOperatingPointCompleter::Sync completer) {
  if (pd == fuchsia_thermal::PowerDomain::LITTLE_CLUSTER_POWER_DOMAIN) {
    completer.Reply(ZX_ERR_NOT_SUPPORTED, 0);
    return;
  }

  completer.Reply(ZX_OK, active_operating_point_);
}

void FakeAmlThermal::SetDvfsOperatingPoint(uint16_t idx, fuchsia_thermal::PowerDomain pd,
                                           SetDvfsOperatingPointCompleter::Sync completer) {
  if (pd == fuchsia_thermal::PowerDomain::LITTLE_CLUSTER_POWER_DOMAIN) {
    completer.Reply(ZX_ERR_NOT_SUPPORTED);
    return;
  }

  active_operating_point_ = idx;
  completer.Reply(ZX_OK);
}

void FakeAmlThermal::GetFanLevel(GetFanLevelCompleter::Sync completer) {
  completer.Reply(ZX_ERR_NOT_SUPPORTED, 0);
}

void FakeAmlThermal::SetFanLevel(uint32_t fan_level, SetFanLevelCompleter::Sync completer) {
  completer.Reply(ZX_ERR_OUT_OF_RANGE);
}

class AmlCpuTest : public AmlCpu {
 public:
  AmlCpuTest(ThermalSyncClient thermal) : AmlCpu(nullptr, std::move(thermal)) {}

  zx_status_t Init();
  static zx_status_t MessageOp(void* ctx, fidl_msg_t* msg, fidl_txn_t* txn);
  zx::channel& GetMessengerChannel() { return messenger_.local(); }

  zx::vmo inspect_vmo() { return inspector_.DuplicateVmo(); }

 private:
  fake_ddk::FidlMessenger messenger_;
};

zx_status_t AmlCpuTest::MessageOp(void* ctx, fidl_msg_t* msg, fidl_txn_t* txn) {
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
  ASSERT_OK(thermal_.Init());
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
