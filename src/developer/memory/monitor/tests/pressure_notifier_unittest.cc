// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/memory/monitor/pressure_notifier.h"

#include <fuchsia/feedback/cpp/fidl_test_base.h>
#include <lib/async/default.h>
#include <lib/gtest/test_loop_fixture.h>
#include <lib/sys/cpp/testing/component_context_provider.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/zx/clock.h>

#include <gtest/gtest.h>

namespace monitor {
namespace test {

namespace fmp = fuchsia::memorypressure;

class CrashReporterForTest : public fuchsia::feedback::testing::CrashReporter_TestBase {
 public:
  CrashReporterForTest() : binding_(this) {}

  void File(fuchsia::feedback::CrashReport report, FileCallback callback) {
    num_crash_reports_++;
    fuchsia::feedback::CrashReporter_File_Result result;
    result.set_response({});
    callback(std::move(result));
  }

  fidl::InterfaceRequestHandler<fuchsia::feedback::CrashReporter> GetHandler() {
    return [this](fidl::InterfaceRequest<fuchsia::feedback::CrashReporter> request) {
      binding_.Bind(std::move(request));
    };
  }

  void NotImplemented_(const std::string& name) override {
    FX_NOTIMPLEMENTED() << name << " is not implemented";
  }

  size_t num_crash_reports() const { return num_crash_reports_; }

 private:
  fidl::Binding<fuchsia::feedback::CrashReporter> binding_;
  size_t num_crash_reports_ = 0;
};

class PressureNotifierUnitTest : public gtest::TestLoopFixture {
 public:
  void SetUp() override {
    context_provider_ =
        std::make_unique<sys::testing::ComponentContextProvider>(async_get_default_dispatcher());
    context_provider_->service_directory_provider()->AddService(crash_reporter_.GetHandler());
    notifier_ = std::make_unique<PressureNotifier>(false, context_provider_->context(),
                                                   async_get_default_dispatcher());
  }

 protected:
  fmp::ProviderPtr Provider() {
    fmp::ProviderPtr provider;
    context_provider_->ConnectToPublicService(provider.NewRequest());
    return provider;
  }

  void InitialLevel() { notifier_->observer_.WaitOnLevelChange(); }

  int GetWatcherCount() { return notifier_->watchers_.size(); }

  void ReleaseWatchers() {
    for (auto& w : notifier_->watchers_) {
      notifier_->ReleaseWatcher(w->proxy.get());
    }
  }

  void TriggerLevelChange(Level level) {
    if (level >= Level::kNumLevels) {
      return;
    }
    notifier_->observer_.OnLevelChanged(notifier_->observer_.wait_items_[level].handle);
    RunLoopUntilIdle();
  }

  void SetCrashReportInterval(uint32_t mins) { notifier_->crash_report_interval_ = zx::min(mins); }

  bool CanGenerateNewCrashReports() const { return notifier_->CanGenerateNewCrashReports(); }

  size_t num_crash_reports() const { return crash_reporter_.num_crash_reports(); }

 private:
  std::unique_ptr<sys::testing::ComponentContextProvider> context_provider_;
  std::unique_ptr<PressureNotifier> notifier_;
  CrashReporterForTest crash_reporter_;
};

class PressureWatcherForTest : public fmp::Watcher {
 public:
  PressureWatcherForTest(bool send_responses)
      : binding_(this, watcher_ptr_.NewRequest()), send_responses_(send_responses) {}

  void OnLevelChanged(fmp::Level level, OnLevelChangedCallback cb) override {
    changes_++;
    if (send_responses_) {
      cb();
    } else {
      stashed_cb_ = std::move(cb);
    }
  }

  void Respond() { stashed_cb_(); }

  void Register(fmp::ProviderPtr provider) { provider->RegisterWatcher(watcher_ptr_.Unbind()); }

  int NumChanges() const { return changes_; }

 private:
  fmp::WatcherPtr watcher_ptr_;
  fidl::Binding<Watcher> binding_;
  int changes_ = 0;
  bool send_responses_;
  OnLevelChangedCallback stashed_cb_;
};

TEST_F(PressureNotifierUnitTest, Watcher) {
  // Scoped so that the Watcher gets deleted. We can then verify that the Provider has no watchers
  // remaining.
  {
    PressureWatcherForTest watcher(true);

    // Registering the watcher should call OnLevelChanged().
    watcher.Register(Provider());
    RunLoopUntilIdle();
    ASSERT_EQ(GetWatcherCount(), 1);
    ASSERT_EQ(watcher.NumChanges(), 1);

    // Trigger first pressure level change, causing another call to OnLevelChanged().
    InitialLevel();
    RunLoopUntilIdle();
    ASSERT_EQ(watcher.NumChanges(), 2);
  }

  RunLoopUntilIdle();
  ASSERT_EQ(GetWatcherCount(), 0);
}

TEST_F(PressureNotifierUnitTest, NoResponse) {
  PressureWatcherForTest watcher(false);

  watcher.Register(Provider());
  RunLoopUntilIdle();
  ASSERT_EQ(GetWatcherCount(), 1);
  ASSERT_EQ(watcher.NumChanges(), 1);

  // This should not trigger a new notification as the watcher has not responded to the last one.
  InitialLevel();
  RunLoopUntilIdle();
  ASSERT_EQ(watcher.NumChanges(), 1);
}

TEST_F(PressureNotifierUnitTest, DelayedResponse) {
  PressureWatcherForTest watcher(false);

  watcher.Register(Provider());
  RunLoopUntilIdle();
  ASSERT_EQ(GetWatcherCount(), 1);
  ASSERT_EQ(watcher.NumChanges(), 1);

  // This should not trigger a new notification as the watcher has not responded to the last one.
  InitialLevel();
  RunLoopUntilIdle();
  ASSERT_EQ(watcher.NumChanges(), 1);

  // Respond to the last message. This should send a new notification to the watcher.
  watcher.Respond();
  RunLoopUntilIdle();
  ASSERT_EQ(watcher.NumChanges(), 2);
}

TEST_F(PressureNotifierUnitTest, MultipleWatchers) {
  // Scoped so that the Watcher gets deleted. We can then verify that the Provider has no watchers
  // remaining.
  {
    PressureWatcherForTest watcher1(true);
    PressureWatcherForTest watcher2(true);

    // Registering the watchers should call OnLevelChanged().
    watcher1.Register(Provider());
    watcher2.Register(Provider());
    RunLoopUntilIdle();
    ASSERT_EQ(GetWatcherCount(), 2);
    ASSERT_EQ(watcher1.NumChanges(), 1);
    ASSERT_EQ(watcher2.NumChanges(), 1);

    // Trigger first pressure level change, causing another call to OnLevelChanged().
    InitialLevel();
    RunLoopUntilIdle();
    ASSERT_EQ(watcher1.NumChanges(), 2);
    ASSERT_EQ(watcher2.NumChanges(), 2);
  }

  RunLoopUntilIdle();
  ASSERT_EQ(GetWatcherCount(), 0);
}

TEST_F(PressureNotifierUnitTest, MultipleWatchersNoResponse) {
  PressureWatcherForTest watcher1(false);
  PressureWatcherForTest watcher2(false);

  watcher1.Register(Provider());
  watcher2.Register(Provider());
  RunLoopUntilIdle();
  ASSERT_EQ(GetWatcherCount(), 2);
  ASSERT_EQ(watcher1.NumChanges(), 1);
  ASSERT_EQ(watcher2.NumChanges(), 1);

  // This should not trigger new notifications as the watchers have not responded to the last one.
  InitialLevel();
  RunLoopUntilIdle();
  ASSERT_EQ(watcher1.NumChanges(), 1);
  ASSERT_EQ(watcher2.NumChanges(), 1);
}

TEST_F(PressureNotifierUnitTest, MultipleWatchersDelayedResponse) {
  PressureWatcherForTest watcher1(false);
  PressureWatcherForTest watcher2(false);

  watcher1.Register(Provider());
  watcher2.Register(Provider());
  RunLoopUntilIdle();
  ASSERT_EQ(GetWatcherCount(), 2);
  ASSERT_EQ(watcher1.NumChanges(), 1);
  ASSERT_EQ(watcher2.NumChanges(), 1);

  // This should not trigger new notifications as the watchers have not responded to the last one.
  InitialLevel();
  RunLoopUntilIdle();
  ASSERT_EQ(watcher1.NumChanges(), 1);
  ASSERT_EQ(watcher2.NumChanges(), 1);

  // Respond to the last message. This should send new notifications to the watchers.
  watcher1.Respond();
  watcher2.Respond();
  RunLoopUntilIdle();
  ASSERT_EQ(watcher1.NumChanges(), 2);
  ASSERT_EQ(watcher2.NumChanges(), 2);
}

TEST_F(PressureNotifierUnitTest, MultipleWatchersMixedResponse) {
  // Set up watcher1 to not respond immediately, and watcher2 to respond immediately.
  PressureWatcherForTest watcher1(false);
  PressureWatcherForTest watcher2(true);

  watcher1.Register(Provider());
  watcher2.Register(Provider());
  RunLoopUntilIdle();
  ASSERT_EQ(GetWatcherCount(), 2);
  ASSERT_EQ(watcher1.NumChanges(), 1);
  ASSERT_EQ(watcher2.NumChanges(), 1);

  // Trigger first pressure level change.
  InitialLevel();
  RunLoopUntilIdle();
  // Since watcher1 did not respond to the previous change, it will not see this change.
  ASSERT_EQ(watcher1.NumChanges(), 1);
  // Since watcher2 responded to the previous change, it will see it.
  ASSERT_EQ(watcher2.NumChanges(), 2);

  // watcher1 responds now.
  watcher1.Respond();
  RunLoopUntilIdle();
  // watcher1 sees the previous change now.
  ASSERT_EQ(watcher1.NumChanges(), 2);
  ASSERT_EQ(watcher2.NumChanges(), 2);
}

TEST_F(PressureNotifierUnitTest, ReleaseWatcherNoPendingCallback) {
  PressureWatcherForTest watcher(true);

  watcher.Register(Provider());
  RunLoopUntilIdle();
  ASSERT_EQ(GetWatcherCount(), 1);
  ASSERT_EQ(watcher.NumChanges(), 1);

  // Trigger first pressure level change, causing another call to OnLevelChanged().
  InitialLevel();
  RunLoopUntilIdle();
  ASSERT_EQ(watcher.NumChanges(), 2);

  // Release all registered watchers, so that the watcher is now invalid.
  ReleaseWatchers();
  RunLoopUntilIdle();
  // There were no outstanding callbacks, so ReleaseWatchers() sould have freed all watchers.
  ASSERT_EQ(GetWatcherCount(), 0);
}

TEST_F(PressureNotifierUnitTest, ReleaseWatcherPendingCallback) {
  PressureWatcherForTest watcher(false);

  watcher.Register(Provider());
  RunLoopUntilIdle();
  ASSERT_EQ(GetWatcherCount(), 1);
  ASSERT_EQ(watcher.NumChanges(), 1);

  // This should not trigger a new notification as the watcher has not responded to the last one.
  InitialLevel();
  RunLoopUntilIdle();
  ASSERT_EQ(watcher.NumChanges(), 1);

  // Release all registered watchers, so that the watcher is now invalid.
  ReleaseWatchers();
  RunLoopUntilIdle();
  // Verify that the watcher has not been freed yet, since a callback is outstanding.
  ASSERT_EQ(GetWatcherCount(), 1);

  // Respond now. This should free the watcher as well.
  watcher.Respond();
  RunLoopUntilIdle();
  // Verify that the watcher has been freed.
  ASSERT_EQ(GetWatcherCount(), 0);
}

TEST_F(PressureNotifierUnitTest, CrashReportOnCritical) {
  ASSERT_EQ(num_crash_reports(), 0ul);
  ASSERT_TRUE(CanGenerateNewCrashReports());

  TriggerLevelChange(Level::kCritical);

  ASSERT_EQ(num_crash_reports(), 1ul);
  ASSERT_FALSE(CanGenerateNewCrashReports());
}

TEST_F(PressureNotifierUnitTest, NoCrashReportOnWarning) {
  ASSERT_EQ(num_crash_reports(), 0ul);
  ASSERT_TRUE(CanGenerateNewCrashReports());

  TriggerLevelChange(Level::kWarning);

  ASSERT_EQ(num_crash_reports(), 0ul);
  ASSERT_TRUE(CanGenerateNewCrashReports());
}

TEST_F(PressureNotifierUnitTest, NoCrashReportOnNormal) {
  ASSERT_EQ(num_crash_reports(), 0ul);
  ASSERT_TRUE(CanGenerateNewCrashReports());

  TriggerLevelChange(Level::kNormal);

  ASSERT_EQ(num_crash_reports(), 0ul);
  ASSERT_TRUE(CanGenerateNewCrashReports());
}

TEST_F(PressureNotifierUnitTest, NoCrashReportOnCriticalToWarning) {
  ASSERT_EQ(num_crash_reports(), 0ul);
  ASSERT_TRUE(CanGenerateNewCrashReports());

  TriggerLevelChange(Level::kCritical);

  ASSERT_EQ(num_crash_reports(), 1ul);
  ASSERT_FALSE(CanGenerateNewCrashReports());

  TriggerLevelChange(Level::kWarning);

  // No new crash reports for Critical -> Warning
  ASSERT_EQ(num_crash_reports(), 1ul);
  ASSERT_FALSE(CanGenerateNewCrashReports());

  TriggerLevelChange(Level::kCritical);

  // No new crash reports for Warning -> Critical
  ASSERT_EQ(num_crash_reports(), 1ul);
  ASSERT_FALSE(CanGenerateNewCrashReports());
}

TEST_F(PressureNotifierUnitTest, CrashReportOnCriticalToNormal) {
  ASSERT_EQ(num_crash_reports(), 0ul);
  ASSERT_TRUE(CanGenerateNewCrashReports());

  TriggerLevelChange(Level::kCritical);

  ASSERT_EQ(num_crash_reports(), 1ul);
  ASSERT_FALSE(CanGenerateNewCrashReports());

  TriggerLevelChange(Level::kNormal);

  // No new crash reports for Critical -> Normal, but can generate future reports.
  ASSERT_EQ(num_crash_reports(), 1ul);
  ASSERT_TRUE(CanGenerateNewCrashReports());

  TriggerLevelChange(Level::kCritical);

  // New crash report generated on Critical, but cannot generate any more reports.
  ASSERT_EQ(num_crash_reports(), 2ul);
  ASSERT_FALSE(CanGenerateNewCrashReports());
}

TEST_F(PressureNotifierUnitTest, CrashReportOnCriticalAfterLong) {
  ASSERT_EQ(num_crash_reports(), 0ul);
  ASSERT_TRUE(CanGenerateNewCrashReports());

  TriggerLevelChange(Level::kCritical);

  ASSERT_EQ(num_crash_reports(), 1ul);
  ASSERT_FALSE(CanGenerateNewCrashReports());

  TriggerLevelChange(Level::kWarning);

  // No new crash reports for Critical -> Warning
  ASSERT_EQ(num_crash_reports(), 1ul);
  ASSERT_FALSE(CanGenerateNewCrashReports());

  // Crash report interval set to zero. Can generate new reports.
  SetCrashReportInterval(0);
  ASSERT_TRUE(CanGenerateNewCrashReports());

  TriggerLevelChange(Level::kCritical);

  // New crash report generated on Critical, and can generate future reports.
  ASSERT_EQ(num_crash_reports(), 2ul);
  ASSERT_TRUE(CanGenerateNewCrashReports());

  // Crash report interval set to 30 mins. Cannot generate new reports.
  SetCrashReportInterval(30);
  ASSERT_FALSE(CanGenerateNewCrashReports());

  TriggerLevelChange(Level::kWarning);

  // No new crash reports for Critical -> Warning
  ASSERT_EQ(num_crash_reports(), 2ul);
  ASSERT_FALSE(CanGenerateNewCrashReports());

  TriggerLevelChange(Level::kCritical);

  // No new crash reports for Warning -> Critical
  ASSERT_EQ(num_crash_reports(), 2ul);
  ASSERT_FALSE(CanGenerateNewCrashReports());
}

}  // namespace test
}  // namespace monitor
