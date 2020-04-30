// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/feedback/reboot_info/reboot_log_handler.h"

#include <lib/async/cpp/executor.h>
#include <lib/fit/result.h>
#include <lib/zx/time.h>
#include <zircon/errors.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/developer/feedback/reboot_info/reboot_log.h"
#include "src/developer/feedback/testing/cobalt_test_fixture.h"
#include "src/developer/feedback/testing/gpretty_printers.h"
#include "src/developer/feedback/testing/stubs/cobalt_logger.h"
#include "src/developer/feedback/testing/stubs/cobalt_logger_factory.h"
#include "src/developer/feedback/testing/stubs/crash_reporter.h"
#include "src/developer/feedback/testing/unit_test_fixture.h"
#include "src/developer/feedback/utils/cobalt/event.h"
#include "src/developer/feedback/utils/cobalt/logger.h"
#include "src/lib/files/scoped_temp_dir.h"

namespace feedback {
namespace {

using testing::ElementsAre;
using testing::IsEmpty;

constexpr ::fit::result_state kError = ::fit::result_state::error;
constexpr ::fit::result_state kOk = ::fit::result_state::ok;

struct TestParam {
  std::string test_name;
  std::string input_reboot_log;
  std::string output_crash_signature;
  std::optional<zx::duration> output_uptime;
  cobalt::RebootReason output_event_code;
};

class RebootLogHandlerTest : public UnitTestFixture,
                             public CobaltTestFixture,
                             public testing::WithParamInterface<TestParam> {
 public:
  RebootLogHandlerTest() : CobaltTestFixture(/*unit_test_fixture=*/this), executor_(dispatcher()) {}

 protected:
  void SetUpCrashReporterServer(std::unique_ptr<stubs::CrashReporterBase> server) {
    crash_reporter_server_ = std::move(server);
    if (crash_reporter_server_) {
      InjectServiceProvider(crash_reporter_server_.get());
    }
  }

  void WriteRebootLogContents(const std::string& contents) {
    FX_CHECK(tmp_dir_.NewTempFileWithData(contents, &reboot_log_path_));
  }

  ::fit::result<void> HandleRebootLog() {
    return HandleRebootLog(RebootLog::ParseRebootLog(reboot_log_path_));
  }

  ::fit::result<void> HandleRebootLog(const RebootLog& reboot_log) {
    ::fit::result<void> result;
    executor_.schedule_task(
        feedback::HandleRebootLog(reboot_log, dispatcher(), services())
            .then([&result](::fit::result<void>& res) { result = std::move(res); }));
    // TODO(fxb/46216, fxb/48485): remove delay.
    RunLoopFor(zx::sec(90));
    return result;
  }

 private:
  async::Executor executor_;

 protected:
  std::string reboot_log_path_;
  std::unique_ptr<stubs::CrashReporterBase> crash_reporter_server_;

 private:
  files::ScopedTempDir tmp_dir_;
};

TEST_F(RebootLogHandlerTest, Succeed_WellFormedRebootLog) {
  const RebootLog reboot_log(RebootReason::kKernelPanic,
                             "ZIRCON REBOOT REASON (KERNEL PANIC)\n\nUPTIME (ms)\n74715002",
                             zx::msec(74715002));

  SetUpCrashReporterServer(
      std::make_unique<stubs::CrashReporter>(stubs::CrashReporter::Expectations{
          .crash_signature = ToCrashSignature(reboot_log.RebootReason()),
          .reboot_log = reboot_log.RebootLogStr(),
          .uptime = reboot_log.Uptime(),
      }));
  SetUpCobaltServer(std::make_unique<stubs::CobaltLoggerFactory>());

  const auto result = HandleRebootLog(reboot_log);
  EXPECT_EQ(result.state(), kOk);

  EXPECT_THAT(ReceivedCobaltEvents(),
              ElementsAre(cobalt::Event(cobalt::RebootReason::kKernelPanic)));
}

TEST_F(RebootLogHandlerTest, Succeed_NoUptime) {
  const RebootLog reboot_log(RebootReason::kKernelPanic, "ZIRCON REBOOT REASON (KERNEL PANIC)\n",
                             std::nullopt);

  SetUpCrashReporterServer(
      std::make_unique<stubs::CrashReporter>(stubs::CrashReporter::Expectations{
          .crash_signature = ToCrashSignature(reboot_log.RebootReason()),
          .reboot_log = reboot_log.RebootLogStr(),
          .uptime = std::nullopt,
      }));
  SetUpCobaltServer(std::make_unique<stubs::CobaltLoggerFactory>());

  const auto result = HandleRebootLog(reboot_log);
  EXPECT_EQ(result.state(), kOk);

  EXPECT_THAT(ReceivedCobaltEvents(),
              ElementsAre(cobalt::Event(cobalt::RebootReason::kKernelPanic)));
}

TEST_F(RebootLogHandlerTest, Succeed_NoCrashReportFiledCleanReboot) {
  const RebootLog reboot_log(RebootReason::kClean,
                             "ZIRCON REBOOT REASON (NO CRASH)\n\nUPTIME (ms)\n74715002",
                             zx::msec(74715002));

  SetUpCrashReporterServer(std::make_unique<stubs::CrashReporterNoFileExpected>());
  SetUpCobaltServer(std::make_unique<stubs::CobaltLoggerFactory>());

  const auto result = HandleRebootLog(reboot_log);
  EXPECT_EQ(result.state(), kOk);

  EXPECT_THAT(ReceivedCobaltEvents(), ElementsAre(cobalt::Event(cobalt::RebootReason::kClean)));
}

TEST_F(RebootLogHandlerTest, Succeed_NoCrashReportFiledColdReboot) {
  const RebootLog reboot_log(RebootReason::kCold, std::nullopt, std::nullopt);

  SetUpCrashReporterServer(std::make_unique<stubs::CrashReporterNoFileExpected>());
  SetUpCobaltServer(std::make_unique<stubs::CobaltLoggerFactory>());

  const auto result = HandleRebootLog(reboot_log);
  EXPECT_EQ(result.state(), kOk);

  EXPECT_THAT(ReceivedCobaltEvents(), ElementsAre(cobalt::Event(cobalt::RebootReason::kCold)));
}

TEST_F(RebootLogHandlerTest, Fail_CrashReporterNotAvailable) {
  const RebootLog reboot_log(RebootReason::kKernelPanic,
                             "ZIRCON REBOOT REASON (KERNEL PANIC)\n\nUPTIME (ms)\n74715002",
                             zx::msec(74715002));
  SetUpCrashReporterServer(nullptr);
  SetUpCobaltServer(std::make_unique<stubs::CobaltLoggerFactory>());

  const auto result = HandleRebootLog(reboot_log);
  EXPECT_EQ(result.state(), kError);

  EXPECT_THAT(ReceivedCobaltEvents(),
              ElementsAre(cobalt::Event(cobalt::RebootReason::kKernelPanic)));
}

TEST_F(RebootLogHandlerTest, Fail_CrashReporterClosesConnection) {
  const RebootLog reboot_log(RebootReason::kKernelPanic,
                             "ZIRCON REBOOT REASON (KERNEL PANIC)\n\nUPTIME (ms)\n74715002",
                             zx::msec(74715002));
  SetUpCrashReporterServer(std::make_unique<stubs::CrashReporterClosesConnection>());
  SetUpCobaltServer(std::make_unique<stubs::CobaltLoggerFactory>());

  const auto result = HandleRebootLog(reboot_log);
  EXPECT_EQ(result.state(), kError);

  EXPECT_THAT(ReceivedCobaltEvents(),
              ElementsAre(cobalt::Event(cobalt::RebootReason::kKernelPanic)));
}

TEST_F(RebootLogHandlerTest, Fail_CrashReporterFailsToFile) {
  const RebootLog reboot_log(RebootReason::kKernelPanic,
                             "ZIRCON REBOOT REASON (KERNEL PANIC)\n\nUPTIME (ms)\n74715002",
                             zx::msec(74715002));
  SetUpCrashReporterServer(std::make_unique<stubs::CrashReporterAlwaysReturnsError>());
  SetUpCobaltServer(std::make_unique<stubs::CobaltLoggerFactory>());

  const auto result = HandleRebootLog(reboot_log);
  EXPECT_EQ(result.state(), kError);

  EXPECT_THAT(ReceivedCobaltEvents(),
              ElementsAre(cobalt::Event(cobalt::RebootReason::kKernelPanic)));
}

TEST_F(RebootLogHandlerTest, Fail_CallHandleTwice) {
  const RebootLog reboot_log(RebootReason::kNotParseable, std::nullopt, std::nullopt);
  internal::RebootLogHandler handler(dispatcher(), services());
  handler.Handle(reboot_log);
  ASSERT_DEATH(handler.Handle(reboot_log),
               testing::HasSubstr("Handle() is not intended to be called twice"));
}

INSTANTIATE_TEST_SUITE_P(WithVariousRebootLogs, RebootLogHandlerTest,
                         ::testing::ValuesIn(std::vector<TestParam>({
                             {
                                 "KernelPanic",
                                 "ZIRCON REBOOT REASON (KERNEL PANIC)\n\nUPTIME (ms)\n65487494",
                                 "fuchsia-kernel-panic",
                                 zx::msec(65487494),
                                 cobalt::RebootReason::kKernelPanic,
                             },
                             {
                                 "OOM",
                                 "ZIRCON REBOOT REASON (OOM)\n\nUPTIME (ms)\n65487494",
                                 "fuchsia-oom",
                                 zx::msec(65487494),
                                 cobalt::RebootReason::kOOM,
                             },
                             {
                                 "Spontaneous",
                                 "ZIRCON REBOOT REASON (UNKNOWN)\n\nUPTIME (ms)\n65487494",
                                 "fuchsia-reboot-unknown",
                                 zx::msec(65487494),
                                 cobalt::RebootReason::kUnknown,
                             },
                             {
                                 "SoftwareWatchdogTimeout",
                                 "ZIRCON REBOOT REASON (SW WATCHDOG)\n\nUPTIME (ms)\n65487494",
                                 "fuchsia-sw-watchdog-timeout",
                                 zx::msec(65487494),
                                 cobalt::RebootReason::kSoftwareWatchdog,
                             },
                             {
                                 "HardwareWatchdogTimeout",
                                 "ZIRCON REBOOT REASON (HW WATCHDOG)\n\nUPTIME (ms)\n65487494",
                                 "fuchsia-hw-watchdog-timeout",
                                 zx::msec(65487494),
                                 cobalt::RebootReason::kHardwareWatchdog,
                             },
                             {
                                 "BrownoutPower",
                                 "ZIRCON REBOOT REASON (BROWNOUT)\n\nUPTIME (ms)\n65487494",
                                 "fuchsia-brownout",
                                 zx::msec(65487494),
                                 cobalt::RebootReason::kBrownout,
                             },
                         })),
                         [](const testing::TestParamInfo<TestParam>& info) {
                           return info.param.test_name;
                         });

TEST_P(RebootLogHandlerTest, Succeed) {
  const auto param = GetParam();

  WriteRebootLogContents(param.input_reboot_log);
  SetUpCrashReporterServer(
      std::make_unique<stubs::CrashReporter>(stubs::CrashReporter::Expectations{
          .crash_signature = param.output_crash_signature,
          .reboot_log = param.input_reboot_log,
          .uptime = param.output_uptime,
      }));
  SetUpCobaltServer(std::make_unique<stubs::CobaltLoggerFactory>());

  fit::result<void> result = HandleRebootLog();
  EXPECT_EQ(result.state(), kOk);

  EXPECT_THAT(ReceivedCobaltEvents(), ElementsAre(cobalt::Event(param.output_event_code)));
}

}  // namespace
}  // namespace feedback
