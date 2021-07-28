// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/feedback/feedback_data.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/developer/forensics/feedback/constants.h"
#include "src/developer/forensics/feedback/device_id_provider.h"
#include "src/developer/forensics/feedback_data/config.h"
#include "src/developer/forensics/testing/stubs/cobalt_logger.h"
#include "src/developer/forensics/testing/stubs/cobalt_logger_factory.h"
#include "src/developer/forensics/testing/unit_test_fixture.h"
#include "src/developer/forensics/utils/cobalt/logger.h"
#include "src/lib/files/file.h"
#include "src/lib/files/path.h"
#include "src/lib/files/scoped_temp_dir.h"
#include "src/lib/timekeeper/async_test_clock.h"
#include "src/lib/timekeeper/clock.h"

namespace forensics::feedback {
namespace {

class FeedbackDataTest : public UnitTestFixture {
 public:
  FeedbackDataTest()
      : clock_(dispatcher()),
        cobalt_(dispatcher(), services(), &clock_),
        device_id_provider_(dispatcher(), services()) {
    SetUpCobaltServer(std::make_unique<stubs::CobaltLoggerFactory>());
  }

  timekeeper::Clock* Clock() { return &clock_; }
  cobalt::Logger* Cobalt() { return &cobalt_; }
  DeviceIdProvider* DeviceIdProvider() { return &device_id_provider_; }

  ~FeedbackDataTest() { FX_CHECK(files::DeletePath(kPreviousLogsFilePath, /*recursive=*/true)); }

 private:
  timekeeper::AsyncTestClock clock_;
  cobalt::Logger cobalt_;
  RemoteDeviceIdProvider device_id_provider_;
};

TEST_F(FeedbackDataTest, DeletesPreviousBootLogs) {
  ASSERT_TRUE(files::WriteFile(kPreviousLogsFilePath, "previous boot logs"));

  const auto kDeletePreviousBootLogsTime = zx::min(10);
  {
    FeedbackData feedback_data(dispatcher(), services(), Clock(), &InspectRoot(), Cobalt(),
                               DeviceIdProvider(),
                               FeedbackData::Options{
                                   .config = {},
                                   .is_first_instance = true,
                                   .limit_inspect_data = false,
                                   .spawn_system_log_recorder = false,
                                   .delete_previous_boot_logs_time = std::nullopt,
                                   .device_id_path = "n/a",
                                   .current_boot_id = Error::kMissingValue,
                                   .previous_boot_id = Error::kMissingValue,
                                   .current_build_version = Error::kMissingValue,
                                   .previous_build_version = Error::kMissingValue,
                                   .last_reboot_reason = Error::kMissingValue,
                                   .last_reboot_uptime = Error::kMissingValue,
                               });

    RunLoopFor(kDeletePreviousBootLogsTime);
    EXPECT_TRUE(files::IsFile(kPreviousLogsFilePath));
  }

  {
    FeedbackData feedback_data(dispatcher(), services(), Clock(), &InspectRoot(), Cobalt(),
                               DeviceIdProvider(),
                               FeedbackData::Options{
                                   .config = {},
                                   .is_first_instance = true,
                                   .limit_inspect_data = false,
                                   .spawn_system_log_recorder = false,
                                   .delete_previous_boot_logs_time = kDeletePreviousBootLogsTime,
                                   .device_id_path = "n/a",
                                   .current_boot_id = Error::kMissingValue,
                                   .previous_boot_id = Error::kMissingValue,
                                   .current_build_version = Error::kMissingValue,
                                   .previous_build_version = Error::kMissingValue,
                                   .last_reboot_reason = Error::kMissingValue,
                                   .last_reboot_uptime = Error::kMissingValue,
                               });

    RunLoopFor(kDeletePreviousBootLogsTime);
    EXPECT_FALSE(files::IsFile(kPreviousLogsFilePath));
  }
}

}  // namespace
}  // namespace forensics::feedback
