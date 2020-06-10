// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/feedback/last_reboot/reporter.h"

#include <lib/fit/result.h>
#include <lib/zx/time.h>
#include <zircon/errors.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/developer/feedback/last_reboot/reboot_log.h"
#include "src/developer/feedback/testing/cobalt_test_fixture.h"
#include "src/developer/feedback/testing/gpretty_printers.h"
#include "src/developer/feedback/testing/stubs/cobalt_logger.h"
#include "src/developer/feedback/testing/stubs/cobalt_logger_factory.h"
#include "src/developer/feedback/testing/stubs/crash_reporter.h"
#include "src/developer/feedback/testing/unit_test_fixture.h"
#include "src/developer/feedback/utils/cobalt/event.h"
#include "src/developer/feedback/utils/cobalt/logger.h"
#include "src/lib/files/file.h"
#include "src/lib/files/path.h"
#include "src/lib/files/scoped_temp_dir.h"

namespace feedback {
namespace {

using testing::IsEmpty;
using testing::UnorderedElementsAreArray;

constexpr char kHasReportedOnPath[] = "/tmp/has_reported_on_reboot_log.txt";

struct TestParam {
  std::string test_name;
  std::string input_reboot_log;
  std::string output_crash_signature;
  std::optional<zx::duration> output_uptime;
  cobalt::LegacyRebootReason output_reboot_reason;
  cobalt::LastRebootReason output_last_reboot_reason;
};

class ReporterTest : public UnitTestFixture,
                     public CobaltTestFixture,
                     public testing::WithParamInterface<TestParam> {
 public:
  ReporterTest()
      : CobaltTestFixture(/*unit_test_fixture=*/this), cobalt_(dispatcher(), services()) {}

  void TearDown() override { files::DeletePath(kHasReportedOnPath, /*recursive=*/false); }

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

  void ReportLog() {
    const auto reboot_log = RebootLog::ParseRebootLog(reboot_log_path_);
    ReportOn(reboot_log);
  }

  void ReportOn(const RebootLog& reboot_log) {
    Reporter reporter(dispatcher(), services(), &cobalt_);
    reporter.ReportOn(reboot_log, /*delay=*/zx::sec(0));
    RunLoopUntilIdle();
  }

  std::string reboot_log_path_;
  std::unique_ptr<stubs::CrashReporterBase> crash_reporter_server_;

 private:
  cobalt::Logger cobalt_;
  files::ScopedTempDir tmp_dir_;
};

TEST_F(ReporterTest, Succeed_WellFormedRebootLog) {
  const zx::duration uptime = zx::msec(74715002);
  const RebootLog reboot_log(RebootReason::kKernelPanic,
                             "ZIRCON REBOOT REASON (KERNEL PANIC)\n\nUPTIME (ms)\n74715002",
                             uptime);

  SetUpCrashReporterServer(
      std::make_unique<stubs::CrashReporter>(stubs::CrashReporter::Expectations{
          .crash_signature = ToCrashSignature(reboot_log.RebootReason()),
          .reboot_log = reboot_log.RebootLogStr(),
          .uptime = reboot_log.Uptime(),
      }));
  SetUpCobaltServer(std::make_unique<stubs::CobaltLoggerFactory>());

  ReportOn(reboot_log);

  EXPECT_THAT(ReceivedCobaltEvents(),
              UnorderedElementsAreArray({
                  cobalt::Event(cobalt::LegacyRebootReason::kKernelPanic),
                  cobalt::Event(cobalt::LastRebootReason::kKernelPanic, uptime.to_usecs()),
              }));
  EXPECT_TRUE(files::IsFile(kHasReportedOnPath));
}

TEST_F(ReporterTest, Succeed_NoUptime) {
  const RebootLog reboot_log(RebootReason::kKernelPanic, "ZIRCON REBOOT REASON (KERNEL PANIC)\n",
                             std::nullopt);

  SetUpCrashReporterServer(
      std::make_unique<stubs::CrashReporter>(stubs::CrashReporter::Expectations{
          .crash_signature = ToCrashSignature(reboot_log.RebootReason()),
          .reboot_log = reboot_log.RebootLogStr(),
          .uptime = std::nullopt,
      }));
  SetUpCobaltServer(std::make_unique<stubs::CobaltLoggerFactory>());

  ReportOn(reboot_log);

  EXPECT_THAT(ReceivedCobaltEvents(),
              UnorderedElementsAreArray({
                  cobalt::Event(cobalt::LegacyRebootReason::kKernelPanic),
                  cobalt::Event(cobalt::LastRebootReason::kKernelPanic, /*duration=*/0u),
              }));
}

TEST_F(ReporterTest, Succeed_NoCrashReportFiledCleanReboot) {
  const zx::duration uptime = zx::msec(74715002);
  const RebootLog reboot_log(RebootReason::kGenericGraceful,
                             "ZIRCON REBOOT REASON (NO CRASH)\n\nUPTIME (ms)\n74715002", uptime);

  SetUpCrashReporterServer(std::make_unique<stubs::CrashReporterNoFileExpected>());
  SetUpCobaltServer(std::make_unique<stubs::CobaltLoggerFactory>());

  ReportOn(reboot_log);

  EXPECT_THAT(ReceivedCobaltEvents(),
              UnorderedElementsAreArray({
                  cobalt::Event(cobalt::LegacyRebootReason::kClean),
                  cobalt::Event(cobalt::LastRebootReason::kGenericGraceful, uptime.to_usecs()),
              }));
}

TEST_F(ReporterTest, Succeed_NoCrashReportFiledColdReboot) {
  const RebootLog reboot_log(RebootReason::kCold, std::nullopt, std::nullopt);

  SetUpCrashReporterServer(std::make_unique<stubs::CrashReporterNoFileExpected>());
  SetUpCobaltServer(std::make_unique<stubs::CobaltLoggerFactory>());

  ReportOn(reboot_log);

  EXPECT_THAT(ReceivedCobaltEvents(),
              UnorderedElementsAreArray({
                  cobalt::Event(cobalt::LegacyRebootReason::kCold),
                  cobalt::Event(cobalt::LastRebootReason::kCold, /*duration=*/0u),
              }));
}

TEST_F(ReporterTest, Fail_CrashReporterFailsToFile) {
  const zx::duration uptime = zx::msec(74715002);
  const RebootLog reboot_log(RebootReason::kKernelPanic,
                             "ZIRCON REBOOT REASON (KERNEL PANIC)\n\nUPTIME (ms)\n74715002",
                             uptime);
  SetUpCrashReporterServer(std::make_unique<stubs::CrashReporterAlwaysReturnsError>());
  SetUpCobaltServer(std::make_unique<stubs::CobaltLoggerFactory>());

  ReportOn(reboot_log);

  EXPECT_THAT(ReceivedCobaltEvents(),
              UnorderedElementsAreArray({
                  cobalt::Event(cobalt::LegacyRebootReason::kKernelPanic),
                  cobalt::Event(cobalt::LastRebootReason::kKernelPanic, uptime.to_usecs()),
              }));
}

TEST_F(ReporterTest, Succeed_DoesNothingIfAlreadyReportedOn) {
  ASSERT_TRUE(files::WriteFile(kHasReportedOnPath, /*data=*/"", /*size=*/0));

  const RebootLog reboot_log(RebootReason::kKernelPanic,
                             "ZIRCON REBOOT REASON (KERNEL PANIC)\n\nUPTIME (ms)\n74715002",
                             zx::msec(74715002));

  SetUpCrashReporterServer(std::make_unique<stubs::CrashReporterNoFileExpected>());
  SetUpCobaltServer(std::make_unique<stubs::CobaltLoggerFactory>());

  ReportOn(reboot_log);

  EXPECT_THAT(ReceivedCobaltEvents(), IsEmpty());
}

INSTANTIATE_TEST_SUITE_P(WithVariousRebootLogs, ReporterTest,
                         ::testing::ValuesIn(std::vector<TestParam>({
                             {
                                 "KernelPanic",
                                 "ZIRCON REBOOT REASON (KERNEL PANIC)\n\nUPTIME (ms)\n65487494",
                                 "fuchsia-kernel-panic",
                                 zx::msec(65487494),
                                 cobalt::LegacyRebootReason::kKernelPanic,
                                 cobalt::LastRebootReason::kKernelPanic,
                             },
                             {
                                 "OOM",
                                 "ZIRCON REBOOT REASON (OOM)\n\nUPTIME (ms)\n65487494",
                                 "fuchsia-oom",
                                 zx::msec(65487494),
                                 cobalt::LegacyRebootReason::kOOM,
                                 cobalt::LastRebootReason::kSystemOutOfMemory,
                             },
                             {
                                 "Spontaneous",
                                 "ZIRCON REBOOT REASON (UNKNOWN)\n\nUPTIME (ms)\n65487494",
                                 "fuchsia-brief-power-loss",
                                 zx::msec(65487494),
                                 cobalt::LegacyRebootReason::kUnknown,
                                 cobalt::LastRebootReason::kBriefPowerLoss,
                             },
                             {
                                 "SoftwareWatchdogTimeout",
                                 "ZIRCON REBOOT REASON (SW WATCHDOG)\n\nUPTIME (ms)\n65487494",
                                 "fuchsia-sw-watchdog-timeout",
                                 zx::msec(65487494),
                                 cobalt::LegacyRebootReason::kSoftwareWatchdog,
                                 cobalt::LastRebootReason::kSoftwareWatchdogTimeout,
                             },
                             {
                                 "HardwareWatchdogTimeout",
                                 "ZIRCON REBOOT REASON (HW WATCHDOG)\n\nUPTIME (ms)\n65487494",
                                 "fuchsia-hw-watchdog-timeout",
                                 zx::msec(65487494),
                                 cobalt::LegacyRebootReason::kHardwareWatchdog,
                                 cobalt::LastRebootReason::kHardwareWatchdogTimeout,
                             },
                             {
                                 "BrownoutPower",
                                 "ZIRCON REBOOT REASON (BROWNOUT)\n\nUPTIME (ms)\n65487494",
                                 "fuchsia-brownout",
                                 zx::msec(65487494),
                                 cobalt::LegacyRebootReason::kBrownout,
                                 cobalt::LastRebootReason::kBrownout,
                             },
                         })),
                         [](const testing::TestParamInfo<TestParam>& info) {
                           return info.param.test_name;
                         });

TEST_P(ReporterTest, Succeed) {
  const auto param = GetParam();

  WriteRebootLogContents(param.input_reboot_log);
  SetUpCrashReporterServer(
      std::make_unique<stubs::CrashReporter>(stubs::CrashReporter::Expectations{
          .crash_signature = param.output_crash_signature,
          .reboot_log = param.input_reboot_log,
          .uptime = param.output_uptime,
      }));
  SetUpCobaltServer(std::make_unique<stubs::CobaltLoggerFactory>());

  ReportLog();

  const zx::duration expected_uptime =
      (param.output_uptime.has_value()) ? param.output_uptime.value() : zx::usec(0);
  EXPECT_THAT(ReceivedCobaltEvents(),
              UnorderedElementsAreArray({
                  cobalt::Event(param.output_reboot_reason),
                  cobalt::Event(param.output_last_reboot_reason, expected_uptime.to_usecs()),
              }));
}

}  // namespace
}  // namespace feedback
