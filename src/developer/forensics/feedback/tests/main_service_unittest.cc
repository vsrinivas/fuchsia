// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/feedback/main_service.h"

#include <fuchsia/feedback/cpp/fidl.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/developer/forensics/testing/stubs/cobalt_logger.h"
#include "src/developer/forensics/testing/stubs/cobalt_logger_factory.h"
#include "src/developer/forensics/testing/stubs/crash_reporter.h"
#include "src/developer/forensics/testing/stubs/reboot_methods_watcher_register.h"
#include "src/developer/forensics/testing/unit_test_fixture.h"
#include "src/lib/timekeeper/async_test_clock.h"

namespace forensics::feedback {
namespace {

using inspect::testing::ChildrenMatch;
using inspect::testing::NameMatches;
using inspect::testing::NodeMatches;
using inspect::testing::PropertyList;
using inspect::testing::UintIs;
using ::testing::UnorderedElementsAreArray;

class MainServiceTest : public UnitTestFixture {
 public:
  MainServiceTest()
      : clock_(dispatcher()),
        main_service_(
            dispatcher(), services(), &clock_, &InspectRoot(),
            LastReboot::Options{
                .is_first_instance = true,
                .reboot_log = RebootLog(RebootReason::kUserRequest, "reboot log", zx::sec(100)),
                .graceful_reboot_reason_write_path = "n/a",
                .oom_crash_reporting_delay = zx::sec(1),
            }) {
    AddHandler(main_service_.GetHandler<fuchsia::feedback::LastRebootInfoProvider>());
  }

 private:
  timekeeper::AsyncTestClock clock_;
  MainService main_service_;
};

TEST_F(MainServiceTest, LastReboot) {
  fuchsia::feedback::LastRebootInfoProviderPtr last_reboot_info_provider_ptr_;
  services()->Connect(last_reboot_info_provider_ptr_.NewRequest(dispatcher()));

  bool called = false;
  last_reboot_info_provider_ptr_->Get([&](fuchsia::feedback::LastReboot) { called = true; });

  RunLoopUntilIdle();

  EXPECT_TRUE(called);
  EXPECT_THAT(
      InspectTree(),
      ChildrenMatch(UnorderedElementsAreArray({
          AllOf(NodeMatches(NameMatches("fidl")),
                ChildrenMatch(UnorderedElementsAreArray({
                    NodeMatches(AllOf(NameMatches("fuchsia.feedback.LastRebootInfoProvider"),
                                      PropertyList(UnorderedElementsAreArray({
                                          UintIs("total_num_connections", 1u),
                                          UintIs("current_num_connections", 1u),
                                      })))),
                }))),
      })));

  last_reboot_info_provider_ptr_.Unbind();
  RunLoopUntilIdle();
  EXPECT_THAT(
      InspectTree(),
      ChildrenMatch(UnorderedElementsAreArray({
          AllOf(NodeMatches(NameMatches("fidl")),
                ChildrenMatch(UnorderedElementsAreArray({
                    NodeMatches(AllOf(NameMatches("fuchsia.feedback.LastRebootInfoProvider"),
                                      PropertyList(UnorderedElementsAreArray({
                                          UintIs("total_num_connections", 1u),
                                          UintIs("current_num_connections", 0u),
                                      })))),
                }))),
      })));
}

}  // namespace
}  // namespace forensics::feedback
