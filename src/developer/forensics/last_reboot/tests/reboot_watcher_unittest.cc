// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/last_reboot/reboot_watcher.h"

#include <fuchsia/hardware/power/statecontrol/cpp/fidl.h>

#include <string>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/developer/forensics/testing/gpretty_printers.h"
#include "src/developer/forensics/testing/stubs/cobalt_logger.h"
#include "src/developer/forensics/testing/stubs/cobalt_logger_factory.h"
#include "src/developer/forensics/testing/unit_test_fixture.h"
#include "src/lib/files/file.h"
#include "src/lib/files/path.h"
#include "src/lib/files/scoped_temp_dir.h"
#include "src/lib/fxl/strings/split_string.h"
#include "src/lib/fxl/strings/string_printf.h"

namespace forensics {
namespace last_reboot {
namespace {

using fuchsia::hardware::power::statecontrol::RebootReason;

constexpr char kFilename[] = "graceful_reboot_reason.txt";

struct TestParam {
  std::string test_name;
  RebootReason input_reboot_reason;
  std::string output_reason;
};

class ImminentGracefulRebootWatcherTest : public UnitTestFixture,
                                          public testing::WithParamInterface<TestParam> {
 public:
  ImminentGracefulRebootWatcherTest() : cobalt_(dispatcher(), services()) {}

 protected:
  std::string Path() { return files::JoinPath(tmp_dir_.path(), kFilename); }

  cobalt::Logger cobalt_;

 private:
  files::ScopedTempDir tmp_dir_;
};

INSTANTIATE_TEST_SUITE_P(WithVariousRebootReasons, ImminentGracefulRebootWatcherTest,
                         ::testing::ValuesIn(std::vector<TestParam>({
                             {
                                 "UserRequest",
                                 RebootReason::USER_REQUEST,
                                 "USER REQUEST",
                             },
                             {
                                 "SystemUpdate",
                                 RebootReason::SYSTEM_UPDATE,
                                 "SYSTEM UPDATE",
                             },
                             {
                                 "HighTemperature",
                                 RebootReason::HIGH_TEMPERATURE,
                                 "HIGH TEMPERATURE",
                             },
                             {
                                 "SessionFailure",
                                 RebootReason::SESSION_FAILURE,
                                 "SESSION FAILURE",
                             },
                             {
                                 "SystemFailure",
                                 RebootReason::SYSTEM_FAILURE,
                                 "SYSTEM FAILURE",
                             },
                             {
                                 "NotSupported",
                                 static_cast<RebootReason>(100u),
                                 "NOT SUPPORTED",
                             },
                         })),
                         [](const testing::TestParamInfo<TestParam>& info) {
                           return info.param.test_name;
                         });

TEST_P(ImminentGracefulRebootWatcherTest, Succeed) {
  const auto param = GetParam();

  SetUpCobaltServer(std::make_unique<stubs::CobaltLoggerFactory>());

  ImminentGracefulRebootWatcher watcher(services(), Path(), &cobalt_);

  bool callback_executed = false;
  watcher.OnReboot(param.input_reboot_reason, [&] { callback_executed = true; });
  ASSERT_TRUE(callback_executed);

  std::string contents;
  ASSERT_TRUE(files::ReadFileToString(Path(), &contents));
  EXPECT_EQ(contents, param.output_reason.c_str());

  RunLoopUntilIdle();

  const auto& received_events = ReceivedCobaltEvents();
  ASSERT_EQ(received_events.size(), 1u);
  ASSERT_EQ(received_events[0].dimensions.size(), 1u);
  const auto result =
      static_cast<cobalt::RebootReasonWriteResult>(received_events[0].dimensions[0]);
  EXPECT_EQ(result, cobalt::RebootReasonWriteResult::kSuccess);
}

}  // namespace
}  // namespace last_reboot
}  // namespace forensics
