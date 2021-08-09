// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/memory/monitor/pressure_notifier.h"

#include <fuchsia/feedback/cpp/fidl_test_base.h>
#include <fuchsia/memory/cpp/fidl.h>
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

  void File(fuchsia::feedback::CrashReport report, FileCallback callback) override {
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

class PressureNotifierUnitTest : public fuchsia::memory::Debugger, public gtest::TestLoopFixture {
 public:
  void SetUp() override {
    context_provider_ =
        std::make_unique<sys::testing::ComponentContextProvider>(async_get_default_dispatcher());
    context_provider_->service_directory_provider()->AddService(crash_reporter_.GetHandler());
    SetUpNewPressureNotifier(true /*notify_crash_reproter*/);
    last_level_ = Level::kNormal;
  }

 protected:
  void SetUpNewPressureNotifier(bool send_critical_pressure_crash_reports) {
    notifier_ = std::make_unique<PressureNotifier>(
        false, send_critical_pressure_crash_reports, context_provider_->context(),
        async_get_default_dispatcher(), [this](Level l) { last_level_ = l; });
    // Set up initial pressure level.
    notifier_->observer_.WaitOnLevelChange();
  }

  fmp::ProviderPtr Provider() {
    fmp::ProviderPtr provider;
    context_provider_->ConnectToPublicService(provider.NewRequest());
    return provider;
  }

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

  void SetupMemDebugService() {
    context_provider_->context()->outgoing()->AddPublicService(memdebug_bindings_.GetHandler(this));
  }

  void TestSimulatedPressure(fmp::Level level) {
    fuchsia::memory::DebuggerPtr memdebug;
    context_provider_->ConnectToPublicService(memdebug.NewRequest());
    memdebug->SignalMemoryPressure(level);
  }

  void SetCrashReportInterval(uint32_t mins) {
    notifier_->critical_crash_report_interval_ = zx::min(mins);
  }

  bool CanGenerateNewCriticalCrashReports() const {
    return notifier_->CanGenerateNewCriticalCrashReports();
  }

  size_t num_crash_reports() const { return crash_reporter_.num_crash_reports(); }

  Level last_level() const { return last_level_; }

 private:
  void SignalMemoryPressure(fmp::Level level) override { notifier_->DebugNotify(level); }

  std::unique_ptr<sys::testing::ComponentContextProvider> context_provider_;
  std::unique_ptr<PressureNotifier> notifier_;
  fidl::BindingSet<fuchsia::memory::Debugger> memdebug_bindings_;
  CrashReporterForTest crash_reporter_;
  Level last_level_;
};

class PressureWatcherForTest : public fmp::Watcher {
 public:
  PressureWatcherForTest(bool send_responses)
      : binding_(this, watcher_ptr_.NewRequest()), send_responses_(send_responses) {}

  void OnLevelChanged(fmp::Level level, OnLevelChangedCallback cb) override {
    changes_++;
    last_level_ = level;
    if (send_responses_) {
      cb();
    } else {
      stashed_cb_ = std::move(cb);
    }
  }

  void Respond() { stashed_cb_(); }

  void Register(fmp::ProviderPtr provider) { provider->RegisterWatcher(watcher_ptr_.Unbind()); }

  int NumChanges() const { return changes_; }

  fmp::Level LastLevel() const { return last_level_; }

 private:
  fmp::WatcherPtr watcher_ptr_;
  fidl::Binding<Watcher> binding_;
  fmp::Level last_level_;
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

    // Trigger a pressure level change, causing another call to OnLevelChanged().
    TriggerLevelChange(Level::kNormal);
    RunLoopUntilIdle();
    ASSERT_EQ(watcher.NumChanges(), 2);
  }

  RunLoopUntilIdle();
  ASSERT_EQ(GetWatcherCount(), 0);
}

TEST_F(PressureNotifierUnitTest, NotifyCb) {
  ASSERT_EQ(last_level(), Level::kNormal);
  TriggerLevelChange(Level::kCritical);
  RunLoopUntilIdle();
  ASSERT_EQ(last_level(), Level::kCritical);
}

TEST_F(PressureNotifierUnitTest, NoResponse) {
  PressureWatcherForTest watcher(false);

  watcher.Register(Provider());
  RunLoopUntilIdle();
  ASSERT_EQ(GetWatcherCount(), 1);
  ASSERT_EQ(watcher.NumChanges(), 1);

  // This should not trigger a new notification as the watcher has not responded to the last one.
  TriggerLevelChange(Level::kNormal);
  RunLoopUntilIdle();
  ASSERT_EQ(watcher.NumChanges(), 1);
}

TEST_F(PressureNotifierUnitTest, DelayedResponse) {
  PressureWatcherForTest watcher(false);

  // Signal a specific pressure level here, so that the next one can be different. Delayed callbacks
  // will only come through if the client has missed a level that wasn't the same as the previous
  // one it received a signal for.
  TriggerLevelChange(Level::kNormal);
  watcher.Register(Provider());
  RunLoopUntilIdle();
  ASSERT_EQ(GetWatcherCount(), 1);
  ASSERT_EQ(watcher.NumChanges(), 1);

  // This should not trigger a new notification as the watcher has not responded to the last one.
  TriggerLevelChange(Level::kWarning);
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

    // Trigger pressure level change, causing another call to OnLevelChanged().
    TriggerLevelChange(Level::kNormal);
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
  TriggerLevelChange(Level::kNormal);
  RunLoopUntilIdle();
  ASSERT_EQ(watcher1.NumChanges(), 1);
  ASSERT_EQ(watcher2.NumChanges(), 1);
}

TEST_F(PressureNotifierUnitTest, MultipleWatchersDelayedResponse) {
  PressureWatcherForTest watcher1(false);
  PressureWatcherForTest watcher2(false);

  // Signal a specific pressure level here, so that the next one can be different. Delayed callbacks
  // will only come through if the client has missed a level that wasn't the same as the previous
  // one it received a signal for.
  TriggerLevelChange(Level::kNormal);

  watcher1.Register(Provider());
  watcher2.Register(Provider());
  RunLoopUntilIdle();
  ASSERT_EQ(GetWatcherCount(), 2);
  ASSERT_EQ(watcher1.NumChanges(), 1);
  ASSERT_EQ(watcher2.NumChanges(), 1);

  // This should not trigger new notifications as the watchers have not responded to the last one.
  TriggerLevelChange(Level::kWarning);
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

  // Signal a specific pressure level here, so that the next one can be different. Delayed callbacks
  // will only come through if the client has missed a level that wasn't the same as the previous
  // one it received a signal for.
  TriggerLevelChange(Level::kNormal);

  watcher1.Register(Provider());
  watcher2.Register(Provider());
  RunLoopUntilIdle();
  ASSERT_EQ(GetWatcherCount(), 2);
  ASSERT_EQ(watcher1.NumChanges(), 1);
  ASSERT_EQ(watcher2.NumChanges(), 1);

  // Trigger pressure level change.
  TriggerLevelChange(Level::kWarning);
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

  // Trigger pressure level change, causing another call to OnLevelChanged().
  TriggerLevelChange(Level::kNormal);
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
  TriggerLevelChange(Level::kNormal);
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

TEST_F(PressureNotifierUnitTest, WatcherDoesNotSeeImminentOOM) {
  PressureWatcherForTest watcher(true);

  TriggerLevelChange(Level::kImminentOOM);
  watcher.Register(Provider());
  RunLoopUntilIdle();
  ASSERT_EQ(GetWatcherCount(), 1);
  ASSERT_EQ(watcher.NumChanges(), 1);
  // Watcher sees the initial level as Critical even though it was Imminent-OOM.
  ASSERT_EQ(watcher.LastLevel(), fmp::Level::CRITICAL);

  TriggerLevelChange(Level::kWarning);
  RunLoopUntilIdle();
  ASSERT_EQ(watcher.NumChanges(), 2);
  // Non Imminent-OOM levels come through as expected.
  ASSERT_EQ(watcher.LastLevel(), fmp::Level::WARNING);

  TriggerLevelChange(Level::kImminentOOM);
  RunLoopUntilIdle();
  // Watcher does not see this change as the PressureNotifier won't signal it.
  ASSERT_EQ(watcher.NumChanges(), 2);
  ASSERT_EQ(watcher.LastLevel(), fmp::Level::WARNING);
}

TEST_F(PressureNotifierUnitTest, DelayedWatcherDoesNotSeeImminentOOM) {
  // Don't send responses right away, but wait for the delayed callback to come through.
  PressureWatcherForTest watcher(false);

  TriggerLevelChange(Level::kNormal);
  watcher.Register(Provider());
  RunLoopUntilIdle();
  ASSERT_EQ(GetWatcherCount(), 1);
  ASSERT_EQ(watcher.NumChanges(), 1);
  ASSERT_EQ(watcher.LastLevel(), fmp::Level::NORMAL);

  // This should not trigger a new notification as the watcher has not responded to the last one.
  TriggerLevelChange(Level::kImminentOOM);
  RunLoopUntilIdle();
  ASSERT_EQ(watcher.NumChanges(), 1);
  ASSERT_EQ(watcher.LastLevel(), fmp::Level::NORMAL);

  // Respond to the last message. This should send a new notification to the watcher.
  watcher.Respond();
  RunLoopUntilIdle();
  ASSERT_EQ(watcher.NumChanges(), 2);
  // Watcher will see the delayed Imminent-OOM level as Critical.
  ASSERT_EQ(watcher.LastLevel(), fmp::Level::CRITICAL);
}

TEST_F(PressureNotifierUnitTest, CrashReportOnCritical) {
  ASSERT_EQ(num_crash_reports(), 0ul);
  ASSERT_TRUE(CanGenerateNewCriticalCrashReports());

  TriggerLevelChange(Level::kCritical);

  ASSERT_EQ(num_crash_reports(), 1ul);
  ASSERT_FALSE(CanGenerateNewCriticalCrashReports());
}

TEST_F(PressureNotifierUnitTest, NoCrashReportOnWarning) {
  ASSERT_EQ(num_crash_reports(), 0ul);
  ASSERT_TRUE(CanGenerateNewCriticalCrashReports());

  TriggerLevelChange(Level::kWarning);

  ASSERT_EQ(num_crash_reports(), 0ul);
  ASSERT_TRUE(CanGenerateNewCriticalCrashReports());
}

TEST_F(PressureNotifierUnitTest, NoCrashReportOnNormal) {
  ASSERT_EQ(num_crash_reports(), 0ul);
  ASSERT_TRUE(CanGenerateNewCriticalCrashReports());

  TriggerLevelChange(Level::kNormal);

  ASSERT_EQ(num_crash_reports(), 0ul);
  ASSERT_TRUE(CanGenerateNewCriticalCrashReports());
}

TEST_F(PressureNotifierUnitTest, NoCrashReportOnCriticalToWarning) {
  ASSERT_EQ(num_crash_reports(), 0ul);
  ASSERT_TRUE(CanGenerateNewCriticalCrashReports());

  TriggerLevelChange(Level::kCritical);

  ASSERT_EQ(num_crash_reports(), 1ul);
  ASSERT_FALSE(CanGenerateNewCriticalCrashReports());

  TriggerLevelChange(Level::kWarning);

  // No new crash reports for Critical -> Warning
  ASSERT_EQ(num_crash_reports(), 1ul);
  ASSERT_FALSE(CanGenerateNewCriticalCrashReports());

  TriggerLevelChange(Level::kCritical);

  // No new crash reports for Warning -> Critical
  ASSERT_EQ(num_crash_reports(), 1ul);
  ASSERT_FALSE(CanGenerateNewCriticalCrashReports());
}

TEST_F(PressureNotifierUnitTest, CrashReportOnCriticalToNormal) {
  ASSERT_EQ(num_crash_reports(), 0ul);
  ASSERT_TRUE(CanGenerateNewCriticalCrashReports());

  TriggerLevelChange(Level::kCritical);

  ASSERT_EQ(num_crash_reports(), 1ul);
  ASSERT_FALSE(CanGenerateNewCriticalCrashReports());

  TriggerLevelChange(Level::kNormal);

  // No new crash reports for Critical -> Normal, but can generate future reports.
  ASSERT_EQ(num_crash_reports(), 1ul);
  ASSERT_TRUE(CanGenerateNewCriticalCrashReports());

  TriggerLevelChange(Level::kCritical);

  // New crash report generated on Critical, but cannot generate any more reports.
  ASSERT_EQ(num_crash_reports(), 2ul);
  ASSERT_FALSE(CanGenerateNewCriticalCrashReports());
}

TEST_F(PressureNotifierUnitTest, CrashReportOnCriticalAfterLong) {
  ASSERT_EQ(num_crash_reports(), 0ul);
  ASSERT_TRUE(CanGenerateNewCriticalCrashReports());

  TriggerLevelChange(Level::kCritical);

  ASSERT_EQ(num_crash_reports(), 1ul);
  ASSERT_FALSE(CanGenerateNewCriticalCrashReports());

  TriggerLevelChange(Level::kWarning);

  // No new crash reports for Critical -> Warning
  ASSERT_EQ(num_crash_reports(), 1ul);
  ASSERT_FALSE(CanGenerateNewCriticalCrashReports());

  // Crash report interval set to zero. Can generate new reports.
  SetCrashReportInterval(0);
  ASSERT_TRUE(CanGenerateNewCriticalCrashReports());

  TriggerLevelChange(Level::kCritical);

  // New crash report generated on Critical, and can generate future reports.
  ASSERT_EQ(num_crash_reports(), 2ul);
  ASSERT_TRUE(CanGenerateNewCriticalCrashReports());

  // Crash report interval set to 30 mins. Cannot generate new reports.
  SetCrashReportInterval(30);
  ASSERT_FALSE(CanGenerateNewCriticalCrashReports());

  TriggerLevelChange(Level::kWarning);

  // No new crash reports for Critical -> Warning
  ASSERT_EQ(num_crash_reports(), 2ul);
  ASSERT_FALSE(CanGenerateNewCriticalCrashReports());

  TriggerLevelChange(Level::kCritical);

  // No new crash reports for Warning -> Critical
  ASSERT_EQ(num_crash_reports(), 2ul);
  ASSERT_FALSE(CanGenerateNewCriticalCrashReports());
}

TEST_F(PressureNotifierUnitTest, DoNotSendCriticalPressureCrashReport) {
  SetUpNewPressureNotifier(false /*send_critical_pressure_crash_reports*/);
  ASSERT_EQ(num_crash_reports(), 0ul);
  ASSERT_TRUE(CanGenerateNewCriticalCrashReports());

  // Cannot write critical crash reports.
  TriggerLevelChange(Level::kCritical);
  ASSERT_EQ(num_crash_reports(), 0ul);

  // Cannot write imminent-OOM crash reports.
  TriggerLevelChange(Level::kImminentOOM);
  ASSERT_EQ(num_crash_reports(), 0ul);
}

TEST_F(PressureNotifierUnitTest, CrashReportOnOOM) {
  ASSERT_EQ(num_crash_reports(), 0ul);

  TriggerLevelChange(Level::kImminentOOM);
  ASSERT_EQ(num_crash_reports(), 1ul);
}

TEST_F(PressureNotifierUnitTest, RepeatedCrashReportOnOOM) {
  ASSERT_EQ(num_crash_reports(), 0ul);

  TriggerLevelChange(Level::kImminentOOM);
  ASSERT_EQ(num_crash_reports(), 1ul);

  // Can generate repeated imminent-OOM crash reports (unlike critical ones).
  TriggerLevelChange(Level::kImminentOOM);
  ASSERT_EQ(num_crash_reports(), 2ul);

  TriggerLevelChange(Level::kImminentOOM);
  ASSERT_EQ(num_crash_reports(), 3ul);
}

TEST_F(PressureNotifierUnitTest, CrashReportOnCriticalAndOOM) {
  ASSERT_EQ(num_crash_reports(), 0ul);

  // Critical crash reports don't affect imminent-OOM reports.
  TriggerLevelChange(Level::kCritical);
  ASSERT_EQ(num_crash_reports(), 1ul);

  TriggerLevelChange(Level::kImminentOOM);
  ASSERT_EQ(num_crash_reports(), 2ul);
}

TEST_F(PressureNotifierUnitTest, CrashReportOnOOMAndCritical) {
  ASSERT_EQ(num_crash_reports(), 0ul);

  // Imminent-OOM crash reports don't affect critical reports.
  TriggerLevelChange(Level::kImminentOOM);
  ASSERT_EQ(num_crash_reports(), 1ul);

  TriggerLevelChange(Level::kCritical);
  ASSERT_EQ(num_crash_reports(), 2ul);
}

TEST_F(PressureNotifierUnitTest, SimulatePressure) {
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

    // Start the fuchsia.memory.Debugger service.
    SetupMemDebugService();

    // Simulate pressure via the fuchsia.memory.Debugger service.
    TestSimulatedPressure(fmp::Level::CRITICAL);
    RunLoopUntilIdle();
    // Verify that watchers saw the change.
    ASSERT_EQ(watcher1.NumChanges(), 2);
    ASSERT_EQ(watcher2.NumChanges(), 2);

    TestSimulatedPressure(fmp::Level::WARNING);
    RunLoopUntilIdle();
    ASSERT_EQ(watcher1.NumChanges(), 3);
    ASSERT_EQ(watcher2.NumChanges(), 3);

    // Repeating the same level should count too.
    TestSimulatedPressure(fmp::Level::WARNING);
    RunLoopUntilIdle();
    ASSERT_EQ(watcher1.NumChanges(), 4);
    ASSERT_EQ(watcher2.NumChanges(), 4);

    TestSimulatedPressure(fmp::Level::NORMAL);
    RunLoopUntilIdle();
    ASSERT_EQ(watcher1.NumChanges(), 5);
    ASSERT_EQ(watcher2.NumChanges(), 5);

    // Verify that simulated signals don't affect the real signaling mechanism.
    TriggerLevelChange(Level::kNormal);
    RunLoopUntilIdle();
    ASSERT_EQ(watcher1.NumChanges(), 6);
    ASSERT_EQ(watcher2.NumChanges(), 6);
  }

  RunLoopUntilIdle();
  ASSERT_EQ(GetWatcherCount(), 0);
}

}  // namespace test
}  // namespace monitor
