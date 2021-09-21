// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.hardware.cpu.ctrl/cpp/wire.h>
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
using TestDeviceType = ddk::Device<FakeCpuDevice, ddk::MessageableManual, ddk::PerformanceTunable>;

class FakeCpuDevice : TestDeviceType,
                      fidl::WireServer<cpuctrl::Device>,
                      fidl::WireServer<fuchsia_device::Controller> {
 public:
  FakeCpuDevice() : TestDeviceType(nullptr) {}
  ~FakeCpuDevice() {}

  unsigned int PstateSetCount() const { return pstate_set_count_; }

  // Manage the fake FIDL Loop
  zx_status_t Init();
  static zx_status_t MessageOp(void* ctx, fidl_incoming_msg_t* msg, fidl_txn_t* txn);
  zx::channel& GetMessengerChannel() { return messenger_.local(); }

  void DdkMessage(fidl::IncomingMessage&& msg, DdkTransaction& txn);
  void DdkRelease() {}

  zx_status_t DdkSetPerformanceState(uint32_t requested_state, uint32_t* out_state) {
    *out_state = requested_state;
    return ZX_OK;
  }

  // fidl::WireServer<fuchsia_device::Controller> methods
  // We only implement the following methods for now
  void SetPerformanceState(SetPerformanceStateRequestView request,
                           SetPerformanceStateCompleter::Sync& _completer) override;
  void GetDevicePerformanceStates(GetDevicePerformanceStatesRequestView request,
                                  GetDevicePerformanceStatesCompleter::Sync& completer) override;
  void GetCurrentPerformanceState(GetCurrentPerformanceStateRequestView request,
                                  GetCurrentPerformanceStateCompleter::Sync& completer) override;

  // The following methods are left unimplemented and it's an error to call them.
  void Bind(BindRequestView request, BindCompleter::Sync& _completer) override {}
  void Rebind(RebindRequestView request, RebindCompleter::Sync& _completer) override {}
  void UnbindChildren(UnbindChildrenRequestView request,
                      UnbindChildrenCompleter::Sync& completer) override {}
  void ScheduleUnbind(ScheduleUnbindRequestView request,
                      ScheduleUnbindCompleter::Sync& _completer) override {}
  void GetDriverName(GetDriverNameRequestView request,
                     GetDriverNameCompleter::Sync& _completer) override {}
  void GetDeviceName(GetDeviceNameRequestView request,
                     GetDeviceNameCompleter::Sync& _completer) override {}
  void GetTopologicalPath(GetTopologicalPathRequestView request,
                          GetTopologicalPathCompleter::Sync& _completer) override {}
  void GetEventHandle(GetEventHandleRequestView request,
                      GetEventHandleCompleter::Sync& _completer) override {}
  void GetMinDriverLogSeverity(GetMinDriverLogSeverityRequestView request,
                               GetMinDriverLogSeverityCompleter::Sync& _completer) override {}
  void SetMinDriverLogSeverity(SetMinDriverLogSeverityRequestView request,
                               SetMinDriverLogSeverityCompleter::Sync& _completer) override {}
  void RunCompatibilityTests(RunCompatibilityTestsRequestView request,
                             RunCompatibilityTestsCompleter::Sync& _completer) override {}
  void GetDevicePowerCaps(GetDevicePowerCapsRequestView request,
                          GetDevicePowerCapsCompleter::Sync& _completer) override {}
  void ConfigureAutoSuspend(ConfigureAutoSuspendRequestView request,
                            ConfigureAutoSuspendCompleter::Sync& _completer) override {}
  void UpdatePowerStateMapping(UpdatePowerStateMappingRequestView request,
                               UpdatePowerStateMappingCompleter::Sync& _completer) override {}
  void GetPowerStateMapping(GetPowerStateMappingRequestView request,
                            GetPowerStateMappingCompleter::Sync& _completer) override {}
  void Suspend(SuspendRequestView request, SuspendCompleter::Sync& _completer) override {}
  void Resume(ResumeRequestView request, ResumeCompleter::Sync& _complete) override {}

 private:
  virtual void GetPerformanceStateInfo(GetPerformanceStateInfoRequestView request,
                                       GetPerformanceStateInfoCompleter::Sync& completer) override;
  virtual void GetNumLogicalCores(GetNumLogicalCoresRequestView request,
                                  GetNumLogicalCoresCompleter::Sync& completer) override;
  virtual void GetLogicalCoreId(GetLogicalCoreIdRequestView request,
                                GetLogicalCoreIdCompleter::Sync& completer) override;

  fake_ddk::FidlMessenger messenger_;

  uint32_t current_pstate_ = kInitialPstate;
  unsigned int pstate_set_count_ = 0;
};

zx_status_t FakeCpuDevice::Init() {
  return messenger_.SetMessageOp(this, FakeCpuDevice::MessageOp);
}

zx_status_t FakeCpuDevice::MessageOp(void* ctx, fidl_incoming_msg_t* msg, fidl_txn_t* txn) {
  DdkTransaction transaction(txn);
  static_cast<FakeCpuDevice*>(ctx)->DdkMessage(fidl::IncomingMessage::FromEncodedCMessage(msg),
                                               transaction);
  return transaction.Status();
}

void FakeCpuDevice::DdkMessage(fidl::IncomingMessage&& msg, DdkTransaction& txn) {
  if (fidl::WireTryDispatch<cpuctrl::Device>(this, msg, &txn) == ::fidl::DispatchResult::kFound) {
    return;
  }
  fidl::WireDispatch<fuchsia_device::Controller>(this, std::move(msg), &txn);
}

void FakeCpuDevice::GetPerformanceStateInfo(GetPerformanceStateInfoRequestView request,
                                            GetPerformanceStateInfoCompleter::Sync& completer) {
  if (request->state >= countof(kTestPstates)) {
    completer.ReplyError(ZX_ERR_OUT_OF_RANGE);
  } else {
    completer.ReplySuccess(kTestPstates[request->state]);
  }
}

void FakeCpuDevice::GetNumLogicalCores(GetNumLogicalCoresRequestView request,
                                       GetNumLogicalCoresCompleter::Sync& completer) {
  completer.Reply(kNumLogicalCores);
}

void FakeCpuDevice::GetLogicalCoreId(GetLogicalCoreIdRequestView request,
                                     GetLogicalCoreIdCompleter::Sync& completer) {
  if (request->index >= countof(kLogicalCoreIds)) {
    completer.Reply(UINT64_MAX);
  }
  completer.Reply(kLogicalCoreIds[request->index]);
}

void FakeCpuDevice::SetPerformanceState(SetPerformanceStateRequestView request,
                                        SetPerformanceStateCompleter::Sync& completer) {
  if (request->requested_state > countof(kTestPstates)) {
    completer.Reply(ZX_ERR_NOT_SUPPORTED, request->requested_state);
    return;
  }

  pstate_set_count_++;
  current_pstate_ = request->requested_state;
  completer.Reply(ZX_OK, request->requested_state);
}

void FakeCpuDevice::GetDevicePerformanceStates(
    GetDevicePerformanceStatesRequestView request,
    GetDevicePerformanceStatesCompleter::Sync& completer) {
  ::fidl::Array<fuchsia_device::wire::DevicePerformanceStateInfo,
                fuchsia_device::wire::kMaxDevicePerformanceStates>
      states{};

  for (size_t i = 0; i < fuchsia_device::wire::kMaxDevicePerformanceStates; i++) {
    states[i].is_supported = (i < countof(kTestPstates));
    states[i].state_id = static_cast<uint32_t>(i);
  }
  completer.Reply(states, ZX_OK);
}

void FakeCpuDevice::GetCurrentPerformanceState(
    GetCurrentPerformanceStateRequestView request,
    GetCurrentPerformanceStateCompleter::Sync& completer) {
  completer.Reply(current_pstate_);
}

class TestCpuPerformanceDomain : public CpuPerformanceDomain {
 public:
  // Permit Explicit Construction
  TestCpuPerformanceDomain(fidl::WireSyncClient<cpuctrl::Device> cpu_client,
                           fidl::WireSyncClient<fuchsia_device::Controller> device_client)
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

  fidl::WireSyncClient<cpuctrl::Device> cpu_client(std::move(cpu_client_channel));
  fidl::WireSyncClient<fuchsia_device::Controller> device_client(std::move(device_client_channel));

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
