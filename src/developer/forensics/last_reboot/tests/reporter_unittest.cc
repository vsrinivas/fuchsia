// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/last_reboot/reporter.h"

#include <lib/fit/result.h>
#include <lib/zx/time.h>
#include <zircon/errors.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/developer/forensics/last_reboot/reboot_log.h"
#include "src/developer/forensics/testing/gpretty_printers.h"
#include "src/developer/forensics/testing/stubs/cobalt_logger.h"
#include "src/developer/forensics/testing/stubs/cobalt_logger_factory.h"
#include "src/developer/forensics/testing/stubs/crash_reporter.h"
#include "src/developer/forensics/testing/unit_test_fixture.h"
#include "src/developer/forensics/utils/cobalt/event.h"
#include "src/developer/forensics/utils/cobalt/logger.h"
#include "src/developer/forensics/utils/cobalt/metrics.h"
#include "src/lib/files/file.h"
#include "src/lib/files/path.h"
#include "src/lib/files/scoped_temp_dir.h"
#include "src/lib/fxl/strings/string_printf.h"

namespace forensics {
namespace last_reboot {
namespace {

using testing::IsEmpty;
using testing::UnorderedElementsAreArray;

constexpr char kHasReportedOnPath[] = "/tmp/has_reported_on_reboot_log.txt";

struct UngracefulRebootTestParam {
  std::string test_name;
  std::string zircon_reboot_log;

  std::string output_crash_signature;
  std::optional<zx::duration> output_uptime;
  cobalt::LastRebootReason output_last_reboot_reason;
};

struct GracefulRebootTestParam {
  std::string test_name;
  std::optional<std::string> graceful_reboot_log;

  cobalt::LastRebootReason output_last_reboot_reason;
};

struct GracefulRebootWithCrashTestParam {
  std::string test_name;
  std::string graceful_reboot_log;

  std::string output_crash_signature;
  zx::duration output_uptime;
  cobalt::LastRebootReason output_last_reboot_reason;
};

template <typename TestParam>
class ReporterTest : public UnitTestFixture, public testing::WithParamInterface<TestParam> {
 public:
  ReporterTest() : cobalt_(dispatcher(), services()) {}

  void SetUp() override { FX_CHECK(tmp_dir_.NewTempFileWithData("", &not_a_fdr_path_)); }
  void TearDown() override { files::DeletePath(kHasReportedOnPath, /*recursive=*/false); }

 protected:
  void SetUpCrashReporterServer(std::unique_ptr<stubs::CrashReporterBase> server) {
    crash_reporter_server_ = std::move(server);
    if (crash_reporter_server_) {
      InjectServiceProvider(crash_reporter_server_.get());
    }
  }

  void WriteZirconRebootLogContents(const std::string& contents) {
    FX_CHECK(tmp_dir_.NewTempFileWithData(contents, &zircon_reboot_log_path_));
  }

  void WriteGracefulRebootLogContents(const std::string& contents) {
    FX_CHECK(tmp_dir_.NewTempFileWithData(contents, &graceful_reboot_log_path_));
  }

  void SetAsFdr() { FX_CHECK(files::DeletePath(not_a_fdr_path_, /*recursive=*/true)); }

  void ReportOnRebootLog() {
    const auto reboot_log = RebootLog::ParseRebootLog(zircon_reboot_log_path_,
                                                      graceful_reboot_log_path_, not_a_fdr_path_);
    ReportOn(reboot_log);
  }

  void ReportOn(const RebootLog& reboot_log) {
    Reporter reporter(dispatcher(), services(), &cobalt_);
    reporter.ReportOn(reboot_log, /*delay=*/zx::sec(0));
    RunLoopUntilIdle();
  }

  std::string zircon_reboot_log_path_;
  std::string graceful_reboot_log_path_;
  std::string not_a_fdr_path_;
  std::unique_ptr<stubs::CrashReporterBase> crash_reporter_server_;

 private:
  cobalt::Logger cobalt_;
  files::ScopedTempDir tmp_dir_;
};

using GenericReporterTest = ReporterTest<UngracefulRebootTestParam /*does not matter*/>;

TEST_F(GenericReporterTest, Succeed_WellFormedRebootLog) {
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
                  cobalt::Event(cobalt::LastRebootReason::kKernelPanic, uptime.to_usecs()),
              }));
  EXPECT_TRUE(files::IsFile(kHasReportedOnPath));
}

TEST_F(GenericReporterTest, Succeed_NoUptime) {
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
                  cobalt::Event(cobalt::LastRebootReason::kKernelPanic, /*duration=*/0u),
              }));
}

TEST_F(GenericReporterTest, Succeed_NoCrashReportFiledCleanReboot) {
  const zx::duration uptime = zx::msec(74715002);
  const RebootLog reboot_log(RebootReason::kGenericGraceful,
                             "ZIRCON REBOOT REASON (NO CRASH)\n\nUPTIME (ms)\n74715002", uptime);

  SetUpCrashReporterServer(std::make_unique<stubs::CrashReporterNoFileExpected>());
  SetUpCobaltServer(std::make_unique<stubs::CobaltLoggerFactory>());

  ReportOn(reboot_log);

  EXPECT_THAT(ReceivedCobaltEvents(),
              UnorderedElementsAreArray({
                  cobalt::Event(cobalt::LastRebootReason::kGenericGraceful, uptime.to_usecs()),
              }));
}

TEST_F(GenericReporterTest, Succeed_NoCrashReportFiledColdReboot) {
  const RebootLog reboot_log(RebootReason::kCold, std::nullopt, std::nullopt);

  SetUpCrashReporterServer(std::make_unique<stubs::CrashReporterNoFileExpected>());
  SetUpCobaltServer(std::make_unique<stubs::CobaltLoggerFactory>());

  ReportOn(reboot_log);

  EXPECT_THAT(ReceivedCobaltEvents(),
              UnorderedElementsAreArray({
                  cobalt::Event(cobalt::LastRebootReason::kCold, /*duration=*/0u),
              }));
}

TEST_F(GenericReporterTest, Fail_CrashReporterFailsToFile) {
  const zx::duration uptime = zx::msec(74715002);
  const RebootLog reboot_log(RebootReason::kKernelPanic,
                             "ZIRCON REBOOT REASON (KERNEL PANIC)\n\nUPTIME (ms)\n74715002",
                             uptime);
  SetUpCrashReporterServer(std::make_unique<stubs::CrashReporterAlwaysReturnsError>());
  SetUpCobaltServer(std::make_unique<stubs::CobaltLoggerFactory>());

  ReportOn(reboot_log);

  EXPECT_THAT(ReceivedCobaltEvents(),
              UnorderedElementsAreArray({
                  cobalt::Event(cobalt::LastRebootReason::kKernelPanic, uptime.to_usecs()),
              }));
}

TEST_F(GenericReporterTest, Succeed_DoesNothingIfAlreadyReportedOn) {
  ASSERT_TRUE(files::WriteFile(kHasReportedOnPath, /*data=*/"", /*size=*/0));

  const RebootLog reboot_log(RebootReason::kKernelPanic,
                             "ZIRCON REBOOT REASON (KERNEL PANIC)\n\nUPTIME (ms)\n74715002",
                             zx::msec(74715002));

  SetUpCrashReporterServer(std::make_unique<stubs::CrashReporterNoFileExpected>());
  SetUpCobaltServer(std::make_unique<stubs::CobaltLoggerFactory>());

  ReportOn(reboot_log);

  EXPECT_THAT(ReceivedCobaltEvents(), IsEmpty());
}

using UngracefulReporterTest = ReporterTest<UngracefulRebootTestParam>;

INSTANTIATE_TEST_SUITE_P(WithVariousRebootLogs, UngracefulReporterTest,
                         ::testing::ValuesIn(std::vector<UngracefulRebootTestParam>({
                             {
                                 "KernelPanic",
                                 "ZIRCON REBOOT REASON (KERNEL PANIC)\n\nUPTIME (ms)\n65487494",
                                 "fuchsia-kernel-panic",
                                 zx::msec(65487494),
                                 cobalt::LastRebootReason::kKernelPanic,
                             },
                             {
                                 "OOM",
                                 "ZIRCON REBOOT REASON (OOM)\n\nUPTIME (ms)\n65487494",
                                 "fuchsia-oom",
                                 zx::msec(65487494),
                                 cobalt::LastRebootReason::kSystemOutOfMemory,
                             },
                             {
                                 "Spontaneous",
                                 "ZIRCON REBOOT REASON (UNKNOWN)\n\nUPTIME (ms)\n65487494",
                                 "fuchsia-brief-power-loss",
                                 zx::msec(65487494),
                                 cobalt::LastRebootReason::kBriefPowerLoss,
                             },
                             {
                                 "SoftwareWatchdogTimeout",
                                 "ZIRCON REBOOT REASON (SW WATCHDOG)\n\nUPTIME (ms)\n65487494",
                                 "fuchsia-sw-watchdog-timeout",
                                 zx::msec(65487494),
                                 cobalt::LastRebootReason::kSoftwareWatchdogTimeout,
                             },
                             {
                                 "HardwareWatchdogTimeout",
                                 "ZIRCON REBOOT REASON (HW WATCHDOG)\n\nUPTIME (ms)\n65487494",
                                 "fuchsia-hw-watchdog-timeout",
                                 zx::msec(65487494),
                                 cobalt::LastRebootReason::kHardwareWatchdogTimeout,
                             },
                             {
                                 "BrownoutPower",
                                 "ZIRCON REBOOT REASON (BROWNOUT)\n\nUPTIME (ms)\n65487494",
                                 "fuchsia-brownout",
                                 zx::msec(65487494),
                                 cobalt::LastRebootReason::kBrownout,
                             },
                             {
                                 "NotParseable",
                                 "NOT PARSEABLE",
                                 "fuchsia-reboot-log-not-parseable",
                                 std::nullopt,
                                 cobalt::LastRebootReason::kUnknown,
                             },
                         })),
                         [](const testing::TestParamInfo<UngracefulRebootTestParam>& info) {
                           return info.param.test_name;
                         });

TEST_P(UngracefulReporterTest, Succeed) {
  const auto param = GetParam();

  WriteZirconRebootLogContents(param.zircon_reboot_log);
  SetUpCrashReporterServer(
      std::make_unique<stubs::CrashReporter>(stubs::CrashReporter::Expectations{
          .crash_signature = param.output_crash_signature,
          .reboot_log = param.zircon_reboot_log,
          .uptime = param.output_uptime,
      }));
  SetUpCobaltServer(std::make_unique<stubs::CobaltLoggerFactory>());

  ReportOnRebootLog();

  const zx::duration expected_uptime =
      (param.output_uptime.has_value()) ? param.output_uptime.value() : zx::usec(0);
  EXPECT_THAT(ReceivedCobaltEvents(),
              UnorderedElementsAreArray({
                  cobalt::Event(param.output_last_reboot_reason, expected_uptime.to_usecs()),
              }));
}

using GracefulReporterTest = ReporterTest<GracefulRebootTestParam>;

INSTANTIATE_TEST_SUITE_P(WithVariousRebootLogs, GracefulReporterTest,
                         ::testing::ValuesIn(std::vector<GracefulRebootTestParam>({
                             {
                                 "UserRequest",
                                 "USER REQUEST",
                                 cobalt::LastRebootReason::kUserRequest,
                             },
                             {
                                 "SystemUpdate",
                                 "SYSTEM UPDATE",
                                 cobalt::LastRebootReason::kSystemUpdate,
                             },
                             {
                                 "HighTemperature",
                                 "HIGH TEMPERATURE",
                                 cobalt::LastRebootReason::kHighTemperature,
                             },
                             {
                                 "NotSupported",
                                 "NOT SUPPORTED",
                                 cobalt::LastRebootReason::kGenericGraceful,
                             },
                             {
                                 "kNotParseable",
                                 "NOT PARSEABLE",
                                 cobalt::LastRebootReason::kGenericGraceful,
                             },
                             {
                                 "None",
                                 std::nullopt,
                                 cobalt::LastRebootReason::kGenericGraceful,
                             },
                         })),
                         [](const testing::TestParamInfo<GracefulRebootTestParam>& info) {
                           return info.param.test_name;
                         });

TEST_P(GracefulReporterTest, Succeed) {
  const auto param = GetParam();

  WriteZirconRebootLogContents("ZIRCON REBOOT REASON (NO CRASH)\n\nUPTIME (ms)\n65487494");
  if (param.graceful_reboot_log.has_value()) {
    WriteGracefulRebootLogContents(param.graceful_reboot_log.value());
  }

  SetUpCrashReporterServer(std::make_unique<stubs::CrashReporterNoFileExpected>());
  SetUpCobaltServer(std::make_unique<stubs::CobaltLoggerFactory>());

  ReportOnRebootLog();

  const zx::duration expected_uptime = zx::msec(65487494);
  EXPECT_THAT(ReceivedCobaltEvents(),
              UnorderedElementsAreArray({
                  cobalt::Event(param.output_last_reboot_reason, expected_uptime.to_usecs()),
              }));
}

TEST_P(GracefulReporterTest, Succeed_FDR) {
  WriteZirconRebootLogContents("ZIRCON REBOOT REASON (NO CRASH)\n\nUPTIME (ms)\n65487494");
  SetAsFdr();

  SetUpCrashReporterServer(std::make_unique<stubs::CrashReporterNoFileExpected>());
  SetUpCobaltServer(std::make_unique<stubs::CobaltLoggerFactory>());

  ReportOnRebootLog();

  const zx::duration expected_uptime = zx::msec(65487494);
  EXPECT_THAT(
      ReceivedCobaltEvents(),
      UnorderedElementsAreArray({
          cobalt::Event(cobalt::LastRebootReason::kFactoryDataReset, expected_uptime.to_usecs()),
      }));
}

using GracefulWithCrashReporterTest = ReporterTest<GracefulRebootWithCrashTestParam>;

INSTANTIATE_TEST_SUITE_P(WithVariousRebootLogs, GracefulWithCrashReporterTest,
                         ::testing::ValuesIn(std::vector<GracefulRebootWithCrashTestParam>({
                             {
                                 "SessionFailure",
                                 "SESSION FAILURE",
                                 "fuchsia-session-failure",
                                 zx::msec(65487494),
                                 cobalt::LastRebootReason::kSessionFailure,
                             },
                             {
                                 "SystemFailure",
                                 "SYSTEM FAILURE",
                                 "fuchsia-system-failure",
                                 zx::msec(65487494),
                                 cobalt::LastRebootReason::kSystemFailure,
                             },
                         })),
                         [](const testing::TestParamInfo<GracefulRebootWithCrashTestParam>& info) {
                           return info.param.test_name;
                         });
TEST_P(GracefulWithCrashReporterTest, Succeed) {
  const auto param = GetParam();

  const std::string zircon_reboot_log = fxl::StringPrintf(
      "ZIRCON REBOOT REASON (NO CRASH)\n\nUPTIME (ms)\n%lu", param.output_uptime.to_msecs());
  WriteZirconRebootLogContents(zircon_reboot_log);

  WriteGracefulRebootLogContents(param.graceful_reboot_log);

  SetUpCrashReporterServer(
      std::make_unique<stubs::CrashReporter>(stubs::CrashReporter::Expectations{
          .crash_signature = param.output_crash_signature,
          .reboot_log =
              fxl::StringPrintf("%s\nGRACEFUL REBOOT REASON (%s)", zircon_reboot_log.c_str(),
                                param.graceful_reboot_log.c_str()),
          .uptime = param.output_uptime,
      }));
  SetUpCobaltServer(std::make_unique<stubs::CobaltLoggerFactory>());

  ReportOnRebootLog();

  EXPECT_THAT(ReceivedCobaltEvents(),
              UnorderedElementsAreArray({
                  cobalt::Event(param.output_last_reboot_reason, param.output_uptime.to_usecs()),
              }));
}

}  // namespace
}  // namespace last_reboot
}  // namespace forensics
