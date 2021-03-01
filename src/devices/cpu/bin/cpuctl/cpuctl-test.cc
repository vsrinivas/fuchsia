// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/hardware/cpu/ctrl/llcpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fake_ddk/fidl-helper.h>
#include <lib/fidl-async/cpp/bind.h>

#include <ddktl/device.h>
#include <zxtest/zxtest.h>

#include "performance-domain.h"

namespace {

constexpr cpuctrl::wire::CpuPerformanceStateInfo kTestPstates[] = {
    {.frequency_hz = 1000, .voltage_uv = 100}, {.frequency_hz = 800, .voltage_uv = 90},
    {.frequency_hz = 600, .voltage_uv = 80},   {.frequency_hz = 400, .voltage_uv = 70},
    {.frequency_hz = 200, .voltage_uv = 60},
};

constexpr uint32_t kInitialPstate = 0;

constexpr uint32_t kNumLogicalCores = 4;

constexpr uint64_t kLogicalCoreIds[kNumLogicalCores] = {1, 2, 3, 4};

class FakeCpuDevice;
using TestDeviceType = ddk::Device<FakeCpuDevice, ddk::Messageable, ddk::PerformanceTunable>;

class FakeCpuDevice : TestDeviceType,
                      cpuctrl::Device::Interface,
                      fuchsia_device::Controller::Interface {
 public:
  FakeCpuDevice() : TestDeviceType(nullptr) {}
  ~FakeCpuDevice() {}

  unsigned int PstateSetCount() const { return pstate_set_count_; }

  // Manage the fake FIDL Loop
  zx_status_t Init();
  static zx_status_t MessageOp(void* ctx, fidl_incoming_msg_t* msg, fidl_txn_t* txn);
  zx::channel& GetMessengerChannel() { return messenger_.local(); }

  zx_status_t DdkMessage(fidl_incoming_msg_t* msg, fidl_txn_t* txn);
  void DdkRelease() {}

  zx_status_t DdkSetPerformanceState(uint32_t requested_state, uint32_t* out_state) {
    *out_state = requested_state;
    return ZX_OK;
  }

  // llcpp::fuchsia::device::Controller::Interface methods
  // We only implement the following methods for now
  void SetPerformanceState(uint32_t requested_state,
                           SetPerformanceStateCompleter::Sync& _completer) override;
  void GetDevicePerformanceStates(GetDevicePerformanceStatesCompleter::Sync& completer) override;
  void GetCurrentPerformanceState(GetCurrentPerformanceStateCompleter::Sync& completer) override;

  // The following methods are left unimplemented and it's an error to call them.
  void Bind(::fidl::StringView driver, BindCompleter::Sync& _completer) override {}
  void Rebind(::fidl::StringView driver, RebindCompleter::Sync& _completer) override {}
  void UnbindChildren(UnbindChildrenCompleter::Sync& completer) override {}
  void ScheduleUnbind(ScheduleUnbindCompleter::Sync& _completer) override {}
  void GetDriverName(GetDriverNameCompleter::Sync& _completer) override {}
  void GetDeviceName(GetDeviceNameCompleter::Sync& _completer) override {}
  void GetTopologicalPath(GetTopologicalPathCompleter::Sync& _completer) override {}
  void GetEventHandle(GetEventHandleCompleter::Sync& _completer) override {}
  void GetDriverLogFlags(GetDriverLogFlagsCompleter::Sync& _completer) override {}
  void SetDriverLogFlags(uint32_t clear_flags, uint32_t set_flags,
                         SetDriverLogFlagsCompleter::Sync& _completer) override {}
  void RunCompatibilityTests(int64_t hook_wait_time,
                             RunCompatibilityTestsCompleter::Sync& _completer) override {}
  void GetDevicePowerCaps(GetDevicePowerCapsCompleter::Sync& _completer) override {}
  void ConfigureAutoSuspend(bool enable,
                            ::llcpp::fuchsia::device::wire::DevicePowerState requested_state,
                            ConfigureAutoSuspendCompleter::Sync& _completer) override {}
  void UpdatePowerStateMapping(
      ::fidl::Array<::llcpp::fuchsia::device::wire::SystemPowerStateInfo, 7> mapping,
      UpdatePowerStateMappingCompleter::Sync& _completer) override {}
  void GetPowerStateMapping(GetPowerStateMappingCompleter::Sync& _completer) override {}
  void Suspend(::llcpp::fuchsia::device::wire::DevicePowerState requested_state,
               SuspendCompleter::Sync& _completer) override {}
  void Resume(ResumeCompleter::Sync& _complete) override {}

 private:
  virtual void GetPerformanceStateInfo(uint32_t state,
                                       GetPerformanceStateInfoCompleter::Sync& completer) override;
  virtual void GetNumLogicalCores(GetNumLogicalCoresCompleter::Sync& completer) override;
  virtual void GetLogicalCoreId(uint64_t index,
                                GetLogicalCoreIdCompleter::Sync& completer) override;

  fake_ddk::FidlMessenger messenger_;

  uint32_t current_pstate_ = kInitialPstate;
  unsigned int pstate_set_count_ = 0;
};

zx_status_t FakeCpuDevice::Init() {
  return messenger_.SetMessageOp(this, FakeCpuDevice::MessageOp);
}

zx_status_t FakeCpuDevice::MessageOp(void* ctx, fidl_incoming_msg_t* msg, fidl_txn_t* txn) {
  return static_cast<FakeCpuDevice*>(ctx)->DdkMessage(msg, txn);
}

zx_status_t FakeCpuDevice::DdkMessage(fidl_incoming_msg_t* msg, fidl_txn_t* txn) {
  DdkTransaction transaction(txn);
  if (cpuctrl::Device::TryDispatch(this, msg, &transaction) == ::fidl::DispatchResult::kFound) {
    return transaction.Status();
  }
  fuchsia_device::Controller::Dispatch(this, msg, &transaction);
  return transaction.Status();
}

void FakeCpuDevice::GetPerformanceStateInfo(uint32_t state,
                                            GetPerformanceStateInfoCompleter::Sync& completer) {
  if (state >= countof(kTestPstates)) {
    completer.ReplyError(ZX_ERR_OUT_OF_RANGE);
  } else {
    completer.ReplySuccess(kTestPstates[state]);
  }
}

void FakeCpuDevice::GetNumLogicalCores(GetNumLogicalCoresCompleter::Sync& completer) {
  completer.Reply(kNumLogicalCores);
}

void FakeCpuDevice::GetLogicalCoreId(uint64_t index, GetLogicalCoreIdCompleter::Sync& completer) {
  if (index >= countof(kLogicalCoreIds)) {
    completer.Reply(UINT64_MAX);
  }
  completer.Reply(kLogicalCoreIds[index]);
}

void FakeCpuDevice::SetPerformanceState(uint32_t requested_state,
                                        SetPerformanceStateCompleter::Sync& completer) {
  if (requested_state > countof(kTestPstates)) {
    completer.Reply(ZX_ERR_NOT_SUPPORTED, requested_state);
    return;
  }

  pstate_set_count_++;
  current_pstate_ = requested_state;
  completer.Reply(ZX_OK, requested_state);
}

void FakeCpuDevice::GetDevicePerformanceStates(
    GetDevicePerformanceStatesCompleter::Sync& completer) {
  ::fidl::Array<::llcpp::fuchsia::device::wire::DevicePerformanceStateInfo,
                fuchsia_device::MAX_DEVICE_PERFORMANCE_STATES>
      states{};

  for (size_t i = 0; i < fuchsia_device::MAX_DEVICE_PERFORMANCE_STATES; i++) {
    states[i].is_supported = (i < countof(kTestPstates));
    states[i].state_id = static_cast<uint32_t>(i);
  }
  completer.Reply(states, ZX_OK);
}

void FakeCpuDevice::GetCurrentPerformanceState(
    GetCurrentPerformanceStateCompleter::Sync& completer) {
  completer.Reply(current_pstate_);
}

class TestCpuPerformanceDomain : public CpuPerformanceDomain {
 public:
  // Permit Explicit Construction
  TestCpuPerformanceDomain(cpuctrl::Device::SyncClient cpu_client,
                           fuchsia_device::Controller::SyncClient device_client)
      : CpuPerformanceDomain(std::move(cpu_client), std::move(device_client)) {}
};

class PerformanceDomainTest : public zxtest::Test {
 public:
  void SetUp() override;

 protected:
  FakeCpuDevice cpu_;

  std::unique_ptr<TestCpuPerformanceDomain> pd_;
};

void PerformanceDomainTest::SetUp() {
  ASSERT_OK(cpu_.Init());

  zx::channel cpu_client_channel(cpu_.GetMessengerChannel().get());
  zx::channel device_client_channel(cpu_.GetMessengerChannel().get());

  cpuctrl::Device::SyncClient cpu_client(std::move(cpu_client_channel));
  fuchsia_device::Controller::SyncClient device_client(std::move(device_client_channel));

  pd_ = std::make_unique<TestCpuPerformanceDomain>(std::move(cpu_client), std::move(device_client));
}

// Trivial Tests.
TEST_F(PerformanceDomainTest, TestNumLogicalCores) {
  const auto [core_count_status, core_count] = pd_->GetNumLogicalCores();

  EXPECT_OK(core_count_status);
  EXPECT_EQ(core_count, kNumLogicalCores);
}

TEST_F(PerformanceDomainTest, TestGetCurrentPerformanceState) {
  const auto [st, pstate, pstate_info] = pd_->GetCurrentPerformanceState();
  EXPECT_OK(st);
  EXPECT_EQ(pstate, kInitialPstate);
  EXPECT_EQ(pstate_info.frequency_hz, kTestPstates[kInitialPstate].frequency_hz);
  EXPECT_EQ(pstate_info.voltage_uv, kTestPstates[kInitialPstate].voltage_uv);
}

TEST_F(PerformanceDomainTest, TestGetPerformanceStates) {
  const auto pstates = pd_->GetPerformanceStates();

  ASSERT_EQ(pstates.size(), countof(kTestPstates));

  for (size_t i = 0; i < pstates.size(); i++) {
    EXPECT_EQ(pstates[i].voltage_uv, kTestPstates[i].voltage_uv);
    EXPECT_EQ(pstates[i].frequency_hz, kTestPstates[i].frequency_hz);
  }
}

TEST_F(PerformanceDomainTest, TestSetPerformanceState) {
  // Just move to the next sequential pstate with wraparound.
  const uint32_t test_pstate = (kInitialPstate + 1) % countof(kTestPstates);
  const uint32_t invalid_pstate = countof(kTestPstates) + 1;
  zx_status_t st = pd_->SetPerformanceState(test_pstate);

  EXPECT_OK(st);

  {
    const auto [res, new_pstate, info] = pd_->GetCurrentPerformanceState();
    EXPECT_OK(res);
    EXPECT_EQ(new_pstate, test_pstate);
  }

  st = pd_->SetPerformanceState(invalid_pstate);
  EXPECT_NOT_OK(st);

  {
    // Make sure the pstate hasn't changed.
    const auto [res, new_pstate, info] = pd_->GetCurrentPerformanceState();
    EXPECT_OK(res);
    EXPECT_EQ(new_pstate, test_pstate);
  }

  // Make sure there was exactly one successful call to SetPerformanceState.
  EXPECT_EQ(cpu_.PstateSetCount(), 1);
}

}  // namespace
