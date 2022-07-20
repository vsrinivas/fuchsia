// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/modular/bin/basemgr/session_provider.h"

#include <fuchsia/hardware/power/statecontrol/cpp/fidl.h>
#include <fuchsia/hardware/power/statecontrol/cpp/fidl_test_base.h>
#include <fuchsia/sys/cpp/fidl.h>
#include <lib/async/dispatcher.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fidl/cpp/interface_handle.h>

#include <chrono>
#include <memory>

#include <gtest/gtest.h>
#include <src/lib/files/file.h>
#include <src/lib/files/scoped_temp_dir.h>
#include <src/lib/testing/loop_fixture/real_loop_fixture.h>
#include <src/modular/bin/basemgr/reboot_rate_limiter.h>

namespace modular {

constexpr size_t kMaxCrashRecoveryLimit = 4;

class MockAdmin : public fuchsia::hardware::power::statecontrol::testing::Admin_TestBase {
 public:
  MockAdmin() : binding_(this) {}

  fidl::InterfaceHandle<fuchsia::hardware::power::statecontrol::Admin> NewBinding(
      async_dispatcher_t* dispatcher) {
    return binding_.NewBinding(dispatcher);
  }

  bool RebootCalled() const { return reboot_called_; }

  void Reboot(fuchsia::hardware::power::statecontrol::RebootReason reason,
              RebootCallback callback) override {
    ASSERT_FALSE(reboot_called_);
    reboot_called_ = true;
    ASSERT_EQ(fuchsia::hardware::power::statecontrol::RebootReason::SESSION_FAILURE, reason);
    callback(fuchsia::hardware::power::statecontrol::Admin_Reboot_Result::WithResponse(
        fuchsia::hardware::power::statecontrol::Admin_Reboot_Response(ZX_OK)));
  }

  // |TestBase|
  void NotImplemented_(const std::string& name) override {
    FX_NOTIMPLEMENTED() << name << " is not implemented";
  }

 private:
  fidl::Binding<fuchsia::hardware::power::statecontrol::Admin> binding_;

  bool reboot_called_ = false;
};

class SessionProviderTest : public gtest::RealLoopFixture {
 public:
  void SetUp() override { admin_proxy_.Bind(mock_admin_.NewBinding(dispatcher())); }

  SessionProvider CreateSessionProvider(const std::string& reboot_tracker_file_path) {
    return SessionProvider(
        /*launcher=*/nullptr,
        /*administrator=*/admin_proxy_.get(),
        /*config_accessor=*/nullptr,
        /*v2_services_for_sessionmgr=*/fuchsia::sys::ServiceList(),
        /*outgoing_dir_root=*/nullptr,
        /*on_zero_sessions=*/[this]() { ++on_zero_sessions_ctr_; },
        /*reboot_tracker_file_path=*/reboot_tracker_file_path);
  }

  MockAdmin* GetMockAdmin() { return &mock_admin_; }

  size_t NumOfSessionRestarts() const { return on_zero_sessions_ctr_; }

  std::string GetTmpFilePath() { return tmp_dir.path() + "/reboot_tracker.txt"; }

  std::string GetTmpFileContent() {
    std::string content;
    ZX_ASSERT_MSG(files::ReadFileToString(GetTmpFilePath(), &content),
                  "Failed to read tmp file at %s: %s", GetTmpFilePath().data(), strerror(errno));
    return content;
  }

 private:
  MockAdmin mock_admin_;
  fuchsia::hardware::power::statecontrol::AdminPtr admin_proxy_;
  size_t on_zero_sessions_ctr_ = 0;
  files::ScopedTempDir tmp_dir = files::ScopedTempDir();
};

TEST_F(SessionProviderTest, TriggersRebootAfterMultiplesShellCrashes) {
  auto session_provider = CreateSessionProvider(GetTmpFilePath());
  session_provider.MarkClockAsStarted();

  for (size_t i = 0; i < kMaxCrashRecoveryLimit; ++i) {
    session_provider.OnSessionShutdown(SessionContextImpl::ShutDownReason::CRITICAL_FAILURE);
  }

  RunLoopUntil([this] { return GetMockAdmin()->RebootCalled(); });
  EXPECT_EQ(NumOfSessionRestarts(), kMaxCrashRecoveryLimit);
}

TEST_F(SessionProviderTest, TriggersRebootAfterMultiplesShellCrashesIfFilePathInvalid) {
  auto session_provider = CreateSessionProvider("/data/some-invalid-path");
  session_provider.MarkClockAsStarted();

  for (size_t i = 0; i < kMaxCrashRecoveryLimit; ++i) {
    session_provider.OnSessionShutdown(SessionContextImpl::ShutDownReason::CRITICAL_FAILURE);
  }

  RunLoopUntil([this] { return GetMockAdmin()->RebootCalled(); });
  EXPECT_EQ(NumOfSessionRestarts(), kMaxCrashRecoveryLimit);
}

TEST_F(SessionProviderTest, SkipsRebootIfWithinBackoffWindow) {
  auto session_provider = CreateSessionProvider(GetTmpFilePath());
  session_provider.MarkClockAsStarted();
  RebootRateLimiter rate_limiter(GetTmpFilePath());
  ASSERT_TRUE(rate_limiter.UpdateTrackingFile().is_ok());

  for (size_t i = 0; i < kMaxCrashRecoveryLimit; ++i) {
    session_provider.OnSessionShutdown(SessionContextImpl::ShutDownReason::CRITICAL_FAILURE);
  }

  EXPECT_EQ(NumOfSessionRestarts(), kMaxCrashRecoveryLimit);
  EXPECT_FALSE(GetMockAdmin()->RebootCalled());
}

TEST_F(SessionProviderTest, SkipsUpdatingTrackingFileIfClockNotStarted) {
  // If we dont explicitly mark the UTC clock as started, by invoking,
  // |MarkClockAsStarted|, then the |SessionProvider| instance will assume
  // it is not ready.
  auto session_provider = CreateSessionProvider(GetTmpFilePath());
  RebootRateLimiter rate_limiter(GetTmpFilePath());
  auto timepoint = RebootRateLimiter::SystemClock::now();
  ASSERT_TRUE(rate_limiter.UpdateTrackingFile(timepoint - std::chrono::hours(1)).is_ok());
  std::string content_before_reboot = GetTmpFileContent();

  for (size_t i = 0; i < kMaxCrashRecoveryLimit; ++i) {
    session_provider.OnSessionShutdown(SessionContextImpl::ShutDownReason::CRITICAL_FAILURE);
  }

  RunLoopUntil([this] { return GetMockAdmin()->RebootCalled(); });
  EXPECT_EQ(NumOfSessionRestarts(), kMaxCrashRecoveryLimit);

  std::string content_after_reboot = GetTmpFileContent();
  EXPECT_EQ(content_before_reboot, content_after_reboot);
}

}  // namespace modular
