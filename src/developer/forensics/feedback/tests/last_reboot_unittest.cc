// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/feedback/last_reboot.h"

#include <fuchsia/feedback/cpp/fidl.h>
#include <fuchsia/hardware/power/statecontrol/cpp/fidl.h>

#include <memory>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/developer/forensics/feedback/reboot_log/reboot_log.h"
#include "src/developer/forensics/last_reboot/reporter.h"
#include "src/developer/forensics/testing/stubs/cobalt_logger.h"
#include "src/developer/forensics/testing/stubs/cobalt_logger_factory.h"
#include "src/developer/forensics/testing/stubs/crash_reporter.h"
#include "src/developer/forensics/testing/stubs/reboot_methods_watcher_register.h"
#include "src/developer/forensics/testing/unit_test_fixture.h"
#include "src/developer/forensics/utils/cobalt/logger.h"
#include "src/lib/files/path.h"
#include "src/lib/timekeeper/async_test_clock.h"

namespace forensics::feedback {
namespace {

using ::testing::IsEmpty;
using ::testing::UnorderedElementsAreArray;

class LastRebootTest : public UnitTestFixture {
 public:
  LastRebootTest()
      : clock_(dispatcher()),
        cobalt_(dispatcher(), services(), &clock_),
        reboot_watcher_register_server_(
            std::make_unique<stubs::RebootMethodsWatcherRegisterHangs>()) {
    SetUpCobaltServer(std::make_unique<stubs::CobaltLoggerFactory>());
    InjectServiceProvider(reboot_watcher_register_server_.get());
  }

  void TearDown() override {
    files::DeletePath("/tmp/has_reported_on_reboot_log.txt", /*recursive=*/false);
  }

 protected:
  void SetUpCrashReporterServer(std::unique_ptr<stubs::CrashReporterBase> crash_reporter_server) {
    crash_reporter_server_ = std::move(crash_reporter_server);
  }

  cobalt::Logger* Cobalt() { return &cobalt_; }

  fuchsia::feedback::CrashReporter* CrashReporter() { return crash_reporter_server_.get(); }

  stubs::RebootMethodsWatcherRegisterBase* RebootWatcherRegisterServer() {
    return reboot_watcher_register_server_.get();
  }

 private:
  timekeeper::AsyncTestClock clock_;
  cobalt::Logger cobalt_;

  std::unique_ptr<stubs::RebootMethodsWatcherRegisterBase> reboot_watcher_register_server_;
  std::unique_ptr<stubs::CrashReporterBase> crash_reporter_server_;
};

TEST_F(LastRebootTest, FirstInstance) {
  const zx::duration oom_crash_reporting_delay = zx::sec(90);
  const RebootLog reboot_log(RebootReason::kOOM, "reboot log", zx::sec(1), std::nullopt);

  SetUpCrashReporterServer(
      std::make_unique<stubs::CrashReporter>(stubs::CrashReporter::Expectations{
          .crash_signature = ToCrashSignature(reboot_log.RebootReason()),
          .reboot_log = reboot_log.RebootLogStr(),
          .uptime = reboot_log.Uptime(),
          .is_fatal = IsFatal(reboot_log.RebootReason()),
      }));

  LastReboot last_reboot(dispatcher(), services(), Cobalt(), CrashReporter(),
                         LastReboot::Options{
                             .is_first_instance = true,
                             .reboot_log = reboot_log,
                             .graceful_reboot_reason_write_path = "n/a",
                             .oom_crash_reporting_delay = oom_crash_reporting_delay,
                         });

  RunLoopFor(oom_crash_reporting_delay);

  EXPECT_TRUE(RebootWatcherRegisterServer()->IsBound());
  EXPECT_THAT(
      ReceivedCobaltEvents(),
      UnorderedElementsAreArray({
          cobalt::Event(cobalt::LastRebootReason::kSystemOutOfMemory, zx::sec(1).to_usecs()),
      }));
}

TEST_F(LastRebootTest, IsNotFirstInstance) {
  const zx::duration oom_crash_reporting_delay = zx::sec(90);
  const RebootLog reboot_log(RebootReason::kOOM, "reboot log", zx::sec(1), std::nullopt);

  SetUpCrashReporterServer(std::make_unique<stubs::CrashReporterNoFileExpected>());

  LastReboot last_reboot(dispatcher(), services(), Cobalt(), CrashReporter(),
                         LastReboot::Options{
                             .is_first_instance = false,
                             .reboot_log = reboot_log,
                             .graceful_reboot_reason_write_path = "n/a",
                             .oom_crash_reporting_delay = oom_crash_reporting_delay,
                         });

  RunLoopFor(oom_crash_reporting_delay);

  EXPECT_TRUE(RebootWatcherRegisterServer()->IsBound());
  EXPECT_THAT(ReceivedCobaltEvents(), IsEmpty());
}

TEST_F(LastRebootTest, ReportsOnReboot) {
  const zx::duration oom_crash_reporting_delay = zx::sec(90);
  const RebootLog reboot_log(RebootReason::kOOM, "reboot log", zx::sec(1), std::nullopt);

  LastReboot last_reboot(dispatcher(), services(), Cobalt(), CrashReporter(),
                         LastReboot::Options{
                             .is_first_instance = false,
                             .reboot_log = reboot_log,
                             .graceful_reboot_reason_write_path = "n/a",
                             .oom_crash_reporting_delay = oom_crash_reporting_delay,
                         });

  bool error_handler_called = false;
  ::fidl::InterfaceRequestHandler<fuchsia::feedback::LastRebootInfoProvider> handler(
      [&](::fidl::InterfaceRequest<fuchsia::feedback::LastRebootInfoProvider> request) {
        last_reboot.Handle(std::move(request), [&](zx_status_t) { error_handler_called = true; });
      });
  AddHandler(std::move(handler));

  fuchsia::feedback::LastRebootInfoProviderPtr last_reboot_info_ptr;
  services()->Connect(last_reboot_info_ptr.NewRequest(dispatcher()));

  bool called = false;
  last_reboot_info_ptr->Get([&](::fuchsia::feedback::LastReboot) { called = true; });

  RunLoopUntilIdle();
  EXPECT_TRUE(called);

  last_reboot_info_ptr.Unbind();
  RunLoopUntilIdle();
  EXPECT_TRUE(error_handler_called);
}

}  // namespace
}  // namespace forensics::feedback
