// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/last_reboot/reboot_log.h"

#include <lib/syslog/cpp/macros.h>

#include <string>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/developer/forensics/last_reboot/reboot_watcher.h"
#include "src/developer/forensics/testing/unit_test_fixture.h"
#include "src/lib/files/path.h"
#include "src/lib/files/scoped_temp_dir.h"
#include "src/lib/fxl/strings/string_printf.h"

namespace forensics {
namespace last_reboot {
namespace {

using GracefulRebootReason = fuchsia::hardware::power::statecontrol::RebootReason;

struct RebootReasonTestParam {
  std::string test_name;
  std::optional<std::string> zircon_reboot_log;
  std::optional<GracefulRebootReason> graceful_reboot_reason;
  RebootReason output_reboot_reason;
};

struct UptimeTestParam {
  std::string test_name;
  std::optional<std::string> zircon_reboot_log;
  std::optional<zx::duration> output_uptime;
};

struct RebootLogStrTestParam {
  std::string test_name;
  std::optional<std::string> zircon_reboot_log;
  std::optional<GracefulRebootReason> graceful_reboot_reason;
  std::optional<std::string> output_reboot_log_str;
};

template <typename TestParam>
class RebootLogTest : public UnitTestFixture, public testing::WithParamInterface<TestParam> {
 protected:
  void SetUp() override { FX_CHECK(tmp_dir_.NewTempFileWithData("", &not_a_fdr_path_)); }

  void WriteZirconRebootLogContents(const std::string& contents) {
    FX_CHECK(tmp_dir_.NewTempFileWithData(contents, &zircon_reboot_log_path_))
        << "Failed to create temporary Zircon reboot log";
  }

  void WriteGracefulRebootLogContents(const std::string& contents) {
    FX_CHECK(tmp_dir_.NewTempFileWithData(contents, &graceful_reboot_log_path_))
        << "Failed to create temporary graceful reboot log";
  }

  void WriteGracefulRebootLogContents(const GracefulRebootReason reason) {
    FX_CHECK(tmp_dir_.NewTempFileWithData("", &graceful_reboot_log_path_))
        << "Failed to create temporary graceful reboot log";

    cobalt::Logger cobalt(dispatcher(), services());

    ImminentGracefulRebootWatcher watcher(services(), graceful_reboot_log_path_, &cobalt);
    watcher.OnReboot(reason, [] {});
  }

  void SetAsFdr() { FX_CHECK(files::DeletePath(not_a_fdr_path_, /*recursive=*/true)); }

  std::string zircon_reboot_log_path_;
  std::string graceful_reboot_log_path_;
  std::string not_a_fdr_path_;

 private:
  files::ScopedTempDir tmp_dir_;
};

using RebootLogReasonTest = RebootLogTest<RebootReasonTestParam>;

INSTANTIATE_TEST_SUITE_P(WithVariousRebootLogs, RebootLogReasonTest,
                         ::testing::ValuesIn(std::vector<RebootReasonTestParam>({
                             {
                                 "ZirconCleanNoGraceful",
                                 "ZIRCON REBOOT REASON (NO CRASH)\n\nUPTIME (ms)\n1234",
                                 std::nullopt,
                                 RebootReason::kGenericGraceful,
                             },
                             {
                                 "ZirconCleanGracefulUserRequest",
                                 "ZIRCON REBOOT REASON (NO CRASH)\n\nUPTIME (ms)\n1234",
                                 GracefulRebootReason::USER_REQUEST,
                                 RebootReason::kUserRequest,
                             },
                             {
                                 "ZirconCleanGracefulSystemUpdate",
                                 "ZIRCON REBOOT REASON (NO CRASH)\n\nUPTIME (ms)\n1234",
                                 GracefulRebootReason::SYSTEM_UPDATE,
                                 RebootReason::kSystemUpdate,
                             },
                             {
                                 "ZirconCleanGracefulHighTemperature",
                                 "ZIRCON REBOOT REASON (NO CRASH)\n\nUPTIME (ms)\n1234",
                                 GracefulRebootReason::HIGH_TEMPERATURE,
                                 RebootReason::kHighTemperature,
                             },
                             {
                                 "ZirconCleanGracefulSessionFailure",
                                 "ZIRCON REBOOT REASON (NO CRASH)\n\nUPTIME (ms)\n1234",
                                 GracefulRebootReason::SESSION_FAILURE,
                                 RebootReason::kSessionFailure,
                             },
                             {
                                 "ZirconCleanGracefulNotSupported",
                                 "ZIRCON REBOOT REASON (NO CRASH)\n\nUPTIME (ms)\n1234",
                                 static_cast<GracefulRebootReason>(1000u),
                                 RebootReason::kGenericGraceful,
                             },
                             {
                                 "Cold",
                                 std::nullopt,
                                 GracefulRebootReason::USER_REQUEST,
                                 RebootReason::kCold,

                             },
                             {
                                 "KernelPanic",
                                 "ZIRCON REBOOT REASON (KERNEL PANIC)\n\nUPTIME (ms)\n1234",
                                 GracefulRebootReason::USER_REQUEST,
                                 RebootReason::kKernelPanic,
                             },
                             {
                                 "OOM",
                                 "ZIRCON REBOOT REASON (OOM)\n\nUPTIME (ms)\n1234",
                                 GracefulRebootReason::USER_REQUEST,
                                 RebootReason::kOOM,
                             },
                             {
                                 "SwWatchdog",
                                 "ZIRCON REBOOT REASON (SW WATCHDOG)\n\nUPTIME (ms)\n1234",
                                 GracefulRebootReason::USER_REQUEST,
                                 RebootReason::kSoftwareWatchdogTimeout,
                             },
                             {
                                 "HwWatchdog",
                                 "ZIRCON REBOOT REASON (HW WATCHDOG)\n\nUPTIME (ms)\n1234",
                                 GracefulRebootReason::USER_REQUEST,
                                 RebootReason::kHardwareWatchdogTimeout,
                             },
                             {
                                 "Brownout",
                                 "ZIRCON REBOOT REASON (BROWNOUT)\n\nUPTIME (ms)\n1234",
                                 GracefulRebootReason::USER_REQUEST,
                                 RebootReason::kBrownout,
                             },
                             {
                                 "Spontaneous",
                                 "ZIRCON REBOOT REASON (UNKNOWN)\n\nUPTIME (ms)\n1234",
                                 GracefulRebootReason::USER_REQUEST,
                                 RebootReason::kSpontaneous,
                             },
                             {
                                 "NotParseable",
                                 "NOT PARSEABLE",
                                 GracefulRebootReason::USER_REQUEST,
                                 RebootReason::kNotParseable,

                             },
                         })),
                         [](const testing::TestParamInfo<RebootReasonTestParam>& info) {
                           return info.param.test_name;
                         });

TEST_P(RebootLogReasonTest, Succeed) {
  const auto param = GetParam();
  if (param.zircon_reboot_log.has_value()) {
    WriteZirconRebootLogContents(param.zircon_reboot_log.value());
  }

  if (param.graceful_reboot_reason.has_value()) {
    WriteGracefulRebootLogContents(param.graceful_reboot_reason.value());
  }

  const RebootLog reboot_log(RebootLog::ParseRebootLog(zircon_reboot_log_path_,
                                                       graceful_reboot_log_path_, not_a_fdr_path_));

  EXPECT_EQ(reboot_log.RebootReason(), param.output_reboot_reason);
}

TEST_F(RebootLogReasonTest, Succeed_ZirconCleanGracefulFdr) {
  WriteZirconRebootLogContents("ZIRCON REBOOT REASON (NO CRASH)\n\nUPTIME (ms)\n1234");
  WriteGracefulRebootLogContents(GracefulRebootReason::SYSTEM_UPDATE);
  SetAsFdr();

  const RebootLog reboot_log(RebootLog::ParseRebootLog(zircon_reboot_log_path_,
                                                       graceful_reboot_log_path_, not_a_fdr_path_));

  EXPECT_EQ(reboot_log.RebootReason(), RebootReason::kFdr);
}

TEST_F(RebootLogReasonTest, Succeed_ZirconCleanGracefulNotParseable) {
  WriteZirconRebootLogContents("ZIRCON REBOOT REASON (NO CRASH)\n\nUPTIME (ms)\n1234");
  WriteGracefulRebootLogContents("NOT PARSEABLE");

  const RebootLog reboot_log(RebootLog::ParseRebootLog(zircon_reboot_log_path_,
                                                       graceful_reboot_log_path_, not_a_fdr_path_));

  EXPECT_EQ(reboot_log.RebootReason(), RebootReason::kGenericGraceful);

  ASSERT_TRUE(reboot_log.HasUptime());
  EXPECT_EQ(reboot_log.Uptime(), zx::msec(1234));
}

using RebootLogUptimeTest = RebootLogTest<UptimeTestParam>;

INSTANTIATE_TEST_SUITE_P(WithVariousRebootLogs, RebootLogUptimeTest,
                         ::testing::ValuesIn(std::vector<UptimeTestParam>({
                             {
                                 "WellFormedLog",
                                 "ZIRCON REBOOT REASON (NO CRASH)\n\nUPTIME (ms)\n1234",
                                 zx::msec(1234),
                             },
                             {
                                 "NoZirconRebootLog",
                                 std::nullopt,
                                 std::nullopt,
                             },
                             {
                                 "EmptyZirconRebootLog",
                                 "",
                                 std::nullopt,
                             },
                             {
                                 "TooFewLines",
                                 "BAD REBOOT REASON (NO CRASH)\n\nUPTIME (ms)\n",
                                 std::nullopt,
                             },
                             {
                                 "BadUptimeString",
                                 "BAD REBOOT REASON (NO CRASH)\n\nDOWNTIME (ms)\n1234",
                                 std::nullopt,
                             },
                         })),
                         [](const testing::TestParamInfo<UptimeTestParam>& info) {
                           return info.param.test_name;
                         });

TEST_P(RebootLogUptimeTest, Succeed) {
  const auto param = GetParam();
  if (param.zircon_reboot_log.has_value()) {
    WriteZirconRebootLogContents(param.zircon_reboot_log.value());
  }

  const RebootLog reboot_log(RebootLog::ParseRebootLog(zircon_reboot_log_path_,
                                                       graceful_reboot_log_path_, not_a_fdr_path_));

  if (param.output_uptime.has_value()) {
    ASSERT_TRUE(reboot_log.HasUptime());
    EXPECT_EQ(reboot_log.Uptime(), param.output_uptime.value());
  } else {
    EXPECT_FALSE(reboot_log.HasUptime());
  }
}

using RebootLogStrTest = RebootLogTest<RebootLogStrTestParam>;

INSTANTIATE_TEST_SUITE_P(
    WithVariousRebootLogs, RebootLogStrTest,
    ::testing::ValuesIn(std::vector<RebootLogStrTestParam>({
        {
            "ConcatenatesZirconAndGraceful",
            "ZIRCON REBOOT REASON (NO CRASH)\n\nUPTIME (ms)\n1234",
            GracefulRebootReason::USER_REQUEST,
            "ZIRCON REBOOT REASON (NO CRASH)\n\nUPTIME (ms)\n1234\nGRACEFUL "
            "REBOOT REASON (USER REQUEST)",
        },
        {
            // This test is the same as the above test, but is used to show that there may be an
            // ungraceful zircon reboot reason and a graceful reboot reason.
            "ConcatenatesZirconUngracefulAndGraceful",
            "ZIRCON REBOOT REASON (KERNEL PANIC)\n\nUPTIME (ms)\n1234",
            GracefulRebootReason::USER_REQUEST,
            "ZIRCON REBOOT REASON (KERNEL PANIC)\n\nUPTIME (ms)\n1234\nGRACEFUL "
            "REBOOT REASON (USER REQUEST)",
        },
        {
            "NoGracefulRebootLog",
            "ZIRCON REBOOT REASON (NO CRASH)\n\nUPTIME (ms)\n1234",
            std::nullopt,
            "ZIRCON REBOOT REASON (NO CRASH)\n\nUPTIME (ms)\n1234",
        },
        {
            "NoZirconRebootLog",
            std::nullopt,
            GracefulRebootReason::USER_REQUEST,
            "GRACEFUL REBOOT REASON (USER REQUEST)",
        },
    })),
    [](const testing::TestParamInfo<RebootLogStrTestParam>& info) { return info.param.test_name; });

TEST_P(RebootLogStrTest, Succeed) {
  const auto param = GetParam();
  if (param.zircon_reboot_log.has_value()) {
    WriteZirconRebootLogContents(param.zircon_reboot_log.value());
  }

  if (param.graceful_reboot_reason.has_value()) {
    WriteGracefulRebootLogContents(param.graceful_reboot_reason.value());
  }

  const RebootLog reboot_log(RebootLog::ParseRebootLog(zircon_reboot_log_path_,
                                                       graceful_reboot_log_path_, not_a_fdr_path_));

  if (param.output_reboot_log_str.has_value()) {
    ASSERT_TRUE(reboot_log.HasRebootLogStr());
    EXPECT_EQ(reboot_log.RebootLogStr(), param.output_reboot_log_str.value());
  } else {
    EXPECT_FALSE(reboot_log.HasRebootLogStr());
  }
}

TEST_F(RebootLogStrTest, Succeed_SetsFDR) {
  WriteGracefulRebootLogContents(GracefulRebootReason::USER_REQUEST);
  SetAsFdr();

  const RebootLog reboot_log(RebootLog::ParseRebootLog(zircon_reboot_log_path_,
                                                       graceful_reboot_log_path_, not_a_fdr_path_));
  ASSERT_TRUE(reboot_log.HasRebootLogStr());
  EXPECT_EQ(reboot_log.RebootLogStr(), "GRACEFUL REBOOT REASON (FDR)");
}

}  // namespace
}  // namespace last_reboot
}  // namespace forensics
