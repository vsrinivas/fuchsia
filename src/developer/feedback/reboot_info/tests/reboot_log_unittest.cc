// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/feedback/reboot_info/reboot_log.h"

#include <lib/syslog/cpp/macros.h>

#include <string>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/lib/files/scoped_temp_dir.h"
#include "src/lib/fxl/strings/string_printf.h"

namespace feedback {
namespace {

struct TestParam {
  std::string test_name;
  std::string input_reboot_log;
  std::optional<zx::duration> output_uptime;
  RebootReason output_reboot_reason;
};

class RebootLogTest : public ::testing::Test, public testing::WithParamInterface<TestParam> {
 protected:
  void WriteRebootLogContents(const std::string& contents) {
    FX_CHECK(tmp_dir_.NewTempFileWithData(contents, &reboot_log_path_))
        << "Failed to create temporary reboot log";
  }

  std::string reboot_log_path_;

 private:
  files::ScopedTempDir tmp_dir_;
};

INSTANTIATE_TEST_SUITE_P(WithVariousRebootLogs, RebootLogTest,
                         ::testing::ValuesIn(std::vector<TestParam>({
                             {
                                 "Clean",
                                 "ZIRCON REBOOT REASON (NO CRASH)\n\nUPTIME (ms)\n74715002",
                                 zx::msec(74715002),
                                 RebootReason::kGenericGraceful,
                             },
                             {
                                 "KernelPanic",
                                 "ZIRCON REBOOT REASON (KERNEL PANIC)\n\nUPTIME (ms)\n74715002",
                                 zx::msec(74715002),
                                 RebootReason::kKernelPanic,
                             },
                             {
                                 "OOM",
                                 "ZIRCON REBOOT REASON (OOM)\n\nUPTIME (ms)\n74715002",
                                 zx::msec(74715002),
                                 RebootReason::kOOM,
                             },
                             {
                                 "SwWatchdog",
                                 "ZIRCON REBOOT REASON (SW WATCHDOG)\n\nUPTIME (ms)\n74715002",
                                 zx::msec(74715002),
                                 RebootReason::kSoftwareWatchdogTimeout,
                             },
                             {
                                 "HwWatchdog",
                                 "ZIRCON REBOOT REASON (HW WATCHDOG)\n\nUPTIME (ms)\n74715002",
                                 zx::msec(74715002),
                                 RebootReason::kHardwareWatchdogTimeout,
                             },
                             {
                                 "Brownout",
                                 "ZIRCON REBOOT REASON (BROWNOUT)\n\nUPTIME (ms)\n74715002",
                                 zx::msec(74715002),
                                 RebootReason::kBrownout,
                             },
                             {
                                 "Spontaneous",
                                 "ZIRCON REBOOT REASON (UNKNOWN)\n\nUPTIME (ms)\n74715002",
                                 zx::msec(74715002),
                                 RebootReason::kSpontaneous,
                             },
                             {
                                 "UnexpectedReason",
                                 "ZIRCON REBOOT REASON (GARBAGE)\n\nUPTIME (ms)\n74715002",
                                 zx::msec(74715002),
                                 RebootReason::kNotParseable,
                             },
                             {
                                 "InvalidReason",
                                 "BAD CRASHLOG",
                                 std::nullopt,
                                 RebootReason::kNotParseable,
                             },
                             {
                                 "NoUptime",
                                 "ZIRCON REBOOT REASON (KERNEL PANIC)",
                                 std::nullopt,
                                 RebootReason::kKernelPanic,
                             },
                             {
                                 "InvalidUptime",
                                 "ZIRCON REBOOT REASON (KERNEL PANIC)\n\nUNRECOGNIZED",
                                 std::nullopt,
                                 RebootReason::kKernelPanic,
                             },
                             {
                                 "EmptyCrashlog",
                                 "",
                                 std::nullopt,
                                 RebootReason::kNotParseable,
                             },
                         })),
                         [](const testing::TestParamInfo<TestParam>& info) {
                           return info.param.test_name;
                         });

TEST_P(RebootLogTest, Succeed) {
  const auto param = GetParam();
  WriteRebootLogContents(param.input_reboot_log);

  const RebootLog reboot_log(RebootLog::ParseRebootLog(reboot_log_path_));

  if (!param.input_reboot_log.empty()) {
    ASSERT_TRUE(reboot_log.HasRebootLogStr());
    EXPECT_EQ(reboot_log.RebootLogStr(), param.input_reboot_log);
  } else {
    ASSERT_FALSE(reboot_log.HasRebootLogStr());
  }

  EXPECT_EQ(reboot_log.RebootReason(), param.output_reboot_reason);

  if (param.output_uptime.has_value()) {
    ASSERT_TRUE(reboot_log.HasUptime());
    EXPECT_EQ(reboot_log.Uptime(), param.output_uptime.value());
  } else {
    EXPECT_FALSE(reboot_log.HasUptime());
  }
}

TEST_F(RebootLogTest, Succeed_NoRebootLogPresent) {
  const RebootLog reboot_log(RebootLog::ParseRebootLog(reboot_log_path_));

  EXPECT_FALSE(reboot_log.HasRebootLogStr());
  EXPECT_EQ(reboot_log.RebootReason(), RebootReason::kCold);
  EXPECT_FALSE(reboot_log.HasUptime());
}

}  // namespace
}  // namespace feedback
