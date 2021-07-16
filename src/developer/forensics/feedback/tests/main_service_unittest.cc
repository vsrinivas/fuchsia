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
using ::testing::IsSupersetOf;
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
            },
            CrashReports::Options{
                .config = {},
                .snapshot_manager_max_annotations_size = StorageSize::Bytes(0),
                .snapshot_manager_max_archives_size = StorageSize::Bytes(0),
                .snapshot_manager_window_duration = zx::sec(0),
                .build_version = ErrorOr<std::string>("build_version"),
                .default_annotations = {},
            }) {
    AddHandler(main_service_.GetHandler<fuchsia::feedback::LastRebootInfoProvider>());
    AddHandler(main_service_.GetHandler<fuchsia::feedback::CrashReporter>());
    AddHandler(main_service_.GetHandler<fuchsia::feedback::CrashReportingProductRegister>());
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
      ChildrenMatch(IsSupersetOf({
          AllOf(NodeMatches(NameMatches("fidl")),
                ChildrenMatch(UnorderedElementsAreArray({
                    NodeMatches(AllOf(NameMatches("fuchsia.feedback.LastRebootInfoProvider"),
                                      PropertyList(UnorderedElementsAreArray({
                                          UintIs("total_num_connections", 1u),
                                          UintIs("current_num_connections", 1u),
                                      })))),
                    NodeMatches(AllOf(NameMatches("fuchsia.feedback.CrashReporter"),
                                      PropertyList(UnorderedElementsAreArray({
                                          UintIs("total_num_connections", 0u),
                                          UintIs("current_num_connections", 0u),
                                      })))),
                    NodeMatches(AllOf(NameMatches("fuchsia.feedback.CrashReportingProductRegister"),
                                      PropertyList(UnorderedElementsAreArray({
                                          UintIs("total_num_connections", 0u),
                                          UintIs("current_num_connections", 0u),
                                      })))),
                }))),
      })));

  last_reboot_info_provider_ptr_.Unbind();
  RunLoopUntilIdle();
  EXPECT_THAT(
      InspectTree(),
      ChildrenMatch(IsSupersetOf({
          AllOf(NodeMatches(NameMatches("fidl")),
                ChildrenMatch(UnorderedElementsAreArray({
                    NodeMatches(AllOf(NameMatches("fuchsia.feedback.LastRebootInfoProvider"),
                                      PropertyList(UnorderedElementsAreArray({
                                          UintIs("total_num_connections", 1u),
                                          UintIs("current_num_connections", 0u),
                                      })))),
                    NodeMatches(AllOf(NameMatches("fuchsia.feedback.CrashReporter"),
                                      PropertyList(UnorderedElementsAreArray({
                                          UintIs("total_num_connections", 0u),
                                          UintIs("current_num_connections", 0u),
                                      })))),
                    NodeMatches(AllOf(NameMatches("fuchsia.feedback.CrashReportingProductRegister"),
                                      PropertyList(UnorderedElementsAreArray({
                                          UintIs("total_num_connections", 0u),
                                          UintIs("current_num_connections", 0u),
                                      })))),
                }))),
      })));
}

TEST_F(MainServiceTest, CrashReports) {
  fuchsia::feedback::CrashReporterPtr crash_reporter_ptr_;
  services()->Connect(crash_reporter_ptr_.NewRequest(dispatcher()));

  fuchsia::feedback::CrashReportingProductRegisterPtr crash_reporting_product_register_ptr_;
  services()->Connect(crash_reporting_product_register_ptr_.NewRequest(dispatcher()));

  bool crash_reporter_called = false;
  crash_reporter_ptr_->File(
      std::move(fuchsia::feedback::CrashReport().set_program_name("program_name")),
      [&](::fpromise::result<void, zx_status_t>) { crash_reporter_called = true; });

  bool crash_reporting_product_register_called = false;
  crash_reporting_product_register_ptr_->UpsertWithAck(
      "component_url",
      std::move(fuchsia::feedback::CrashReportingProduct()
                    .set_name("product_name")
                    .set_version("product_version")),
      [&]() { crash_reporting_product_register_called = true; });

  RunLoopUntilIdle();
  EXPECT_TRUE(crash_reporter_called);
  EXPECT_TRUE(crash_reporting_product_register_called);
  EXPECT_THAT(
      InspectTree(),
      ChildrenMatch(IsSupersetOf({
          AllOf(NodeMatches(NameMatches("fidl")),
                ChildrenMatch(UnorderedElementsAreArray({
                    NodeMatches(AllOf(NameMatches("fuchsia.feedback.LastRebootInfoProvider"),
                                      PropertyList(UnorderedElementsAreArray({
                                          UintIs("total_num_connections", 0u),
                                          UintIs("current_num_connections", 0u),
                                      })))),
                    NodeMatches(AllOf(NameMatches("fuchsia.feedback.CrashReporter"),
                                      PropertyList(UnorderedElementsAreArray({
                                          UintIs("total_num_connections", 1u),
                                          UintIs("current_num_connections", 1u),
                                      })))),
                    NodeMatches(AllOf(NameMatches("fuchsia.feedback.CrashReportingProductRegister"),
                                      PropertyList(UnorderedElementsAreArray({
                                          UintIs("total_num_connections", 1u),
                                          UintIs("current_num_connections", 1u),
                                      })))),
                }))),
      })));

  crash_reporter_ptr_.Unbind();
  crash_reporting_product_register_ptr_.Unbind();
  RunLoopUntilIdle();
  EXPECT_THAT(
      InspectTree(),
      ChildrenMatch(IsSupersetOf({
          AllOf(NodeMatches(NameMatches("fidl")),
                ChildrenMatch(UnorderedElementsAreArray({
                    NodeMatches(AllOf(NameMatches("fuchsia.feedback.LastRebootInfoProvider"),
                                      PropertyList(UnorderedElementsAreArray({
                                          UintIs("total_num_connections", 0u),
                                          UintIs("current_num_connections", 0u),
                                      })))),
                    NodeMatches(AllOf(NameMatches("fuchsia.feedback.CrashReporter"),
                                      PropertyList(UnorderedElementsAreArray({
                                          UintIs("total_num_connections", 1u),
                                          UintIs("current_num_connections", 0u),
                                      })))),
                    NodeMatches(AllOf(NameMatches("fuchsia.feedback.CrashReportingProductRegister"),
                                      PropertyList(UnorderedElementsAreArray({
                                          UintIs("total_num_connections", 1u),
                                          UintIs("current_num_connections", 0u),
                                      })))),
                }))),
      })));
}

}  // namespace
}  // namespace forensics::feedback
