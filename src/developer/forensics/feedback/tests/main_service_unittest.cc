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
#include "src/lib/fxl/strings/substitute.h"
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

constexpr bool kIsFirstInstance = true;

class MainServiceTest : public UnitTestFixture {
 public:
  MainServiceTest()
      : clock_(dispatcher()),
        cobalt_(dispatcher(), services(), &clock_),
        main_service_(dispatcher(), services(), &clock_, &InspectRoot(), &cobalt_,
                      /*startup_annotations=*/{},
                      MainService::Options{
                          "",
                          LastReboot::Options{
                              .is_first_instance = kIsFirstInstance,
                              .reboot_log = RebootLog(RebootReason::kUserRequest, "reboot log",
                                                      zx::sec(100), std::nullopt),
                              .graceful_reboot_reason_write_path = "n/a",
                              .oom_crash_reporting_delay = zx::sec(1),
                          },
                          CrashReports::Options{
                              .config = {},
                              .snapshot_store_max_archives_size = StorageSize::Bytes(0),
                              .snapshot_collector_window_duration = zx::sec(0),
                          },
                          FeedbackData::Options{
                              .config{},
                              .is_first_instance = kIsFirstInstance,
                              .limit_inspect_data = false,
                              .spawn_system_log_recorder = false,
                              .delete_previous_boot_logs_time = std::nullopt,
                          },
                      }) {
    AddHandler(main_service_.GetHandler<fuchsia::feedback::LastRebootInfoProvider>());
    AddHandler(main_service_.GetHandler<fuchsia::feedback::CrashReporter>());
    AddHandler(main_service_.GetHandler<fuchsia::feedback::CrashReportingProductRegister>());
    AddHandler(main_service_.GetHandler<fuchsia::feedback::ComponentDataRegister>());
    AddHandler(main_service_.GetHandler<fuchsia::feedback::DataProvider>());
    AddHandler(main_service_.GetHandler<fuchsia::feedback::DataProviderController>());
  }

 private:
  timekeeper::AsyncTestClock clock_;
  cobalt::Logger cobalt_;
  MainService main_service_;
};

auto ProtocolMatcher(const std::string& feedback_protocol, const size_t total_num_connections,
                     const size_t current_num_connections) {
  return NodeMatches(AllOf(NameMatches(fxl::Substitute("fuchsia.feedback.$0", feedback_protocol)),
                           PropertyList(UnorderedElementsAreArray({
                               UintIs("total_num_connections", total_num_connections),
                               UintIs("current_num_connections", current_num_connections),
                           }))));
}

TEST_F(MainServiceTest, LastReboot) {
  fuchsia::feedback::LastRebootInfoProviderPtr last_reboot_info_provider_ptr_;
  services()->Connect(last_reboot_info_provider_ptr_.NewRequest(dispatcher()));

  bool called = false;
  last_reboot_info_provider_ptr_->Get([&](fuchsia::feedback::LastReboot) { called = true; });

  RunLoopUntilIdle();

  EXPECT_TRUE(called);
  EXPECT_THAT(InspectTree(), ChildrenMatch(IsSupersetOf({
                                 AllOf(NodeMatches(NameMatches("fidl")),
                                       ChildrenMatch(IsSupersetOf({
                                           ProtocolMatcher("LastRebootInfoProvider", 1u, 1u),
                                           ProtocolMatcher("CrashReporter", 0u, 0u),
                                           ProtocolMatcher("CrashReportingProductRegister", 0u, 0u),
                                           ProtocolMatcher("ComponentDataRegister", 0u, 0u),
                                           ProtocolMatcher("DataProvider", 0u, 0u),
                                           ProtocolMatcher("DataProviderController", 0u, 0u),
                                       }))),
                             })));

  last_reboot_info_provider_ptr_.Unbind();
  RunLoopUntilIdle();
  EXPECT_THAT(InspectTree(), ChildrenMatch(IsSupersetOf({
                                 AllOf(NodeMatches(NameMatches("fidl")),
                                       ChildrenMatch(IsSupersetOf({
                                           ProtocolMatcher("LastRebootInfoProvider", 1u, 0u),
                                           ProtocolMatcher("CrashReporter", 0u, 0u),
                                           ProtocolMatcher("CrashReportingProductRegister", 0u, 0u),
                                           ProtocolMatcher("ComponentDataRegister", 0u, 0u),
                                           ProtocolMatcher("DataProvider", 0u, 0u),
                                           ProtocolMatcher("DataProviderController", 0u, 0u),
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
  EXPECT_THAT(InspectTree(), ChildrenMatch(IsSupersetOf({
                                 AllOf(NodeMatches(NameMatches("fidl")),
                                       ChildrenMatch(IsSupersetOf({
                                           ProtocolMatcher("LastRebootInfoProvider", 0u, 0u),
                                           ProtocolMatcher("CrashReporter", 1u, 1u),
                                           ProtocolMatcher("CrashReportingProductRegister", 1u, 1u),
                                           ProtocolMatcher("ComponentDataRegister", 0u, 0u),
                                           ProtocolMatcher("DataProvider", 0u, 0u),
                                           ProtocolMatcher("DataProviderController", 0u, 0u),
                                       }))),
                             })));

  crash_reporter_ptr_.Unbind();
  crash_reporting_product_register_ptr_.Unbind();
  RunLoopUntilIdle();
  EXPECT_THAT(InspectTree(), ChildrenMatch(IsSupersetOf({
                                 AllOf(NodeMatches(NameMatches("fidl")),
                                       ChildrenMatch(IsSupersetOf({
                                           ProtocolMatcher("LastRebootInfoProvider", 0u, 0u),
                                           ProtocolMatcher("CrashReporter", 1u, 0u),
                                           ProtocolMatcher("CrashReportingProductRegister", 1u, 0u),
                                           ProtocolMatcher("ComponentDataRegister", 0u, 0u),
                                           ProtocolMatcher("DataProvider", 0u, 0u),
                                           ProtocolMatcher("DataProviderController", 0u, 0u),
                                       }))),
                             })));
}

TEST_F(MainServiceTest, FeedbackData) {
  fuchsia::feedback::ComponentDataRegisterPtr component_data_ptr_;
  services()->Connect(component_data_ptr_.NewRequest(dispatcher()));

  fuchsia::feedback::DataProviderPtr data_provider_ptr_;
  services()->Connect(data_provider_ptr_.NewRequest(dispatcher()));

  fuchsia::feedback::DataProviderControllerPtr data_provider_controller_ptr_;
  services()->Connect(data_provider_controller_ptr_.NewRequest(dispatcher()));

  bool component_data_called = false;
  component_data_ptr_->Upsert(fuchsia::feedback::ComponentData{},
                              [&component_data_called]() { component_data_called = true; });

  fuchsia::feedback::GetSnapshotParameters snapshot_params;
  snapshot_params.set_collection_timeout_per_data(0u);
  bool data_provider_called = false;
  data_provider_ptr_->GetSnapshot(
      std::move(snapshot_params),
      [&data_provider_called](fuchsia::feedback::Snapshot) { data_provider_called = true; });

  bool data_provider_controller_called = false;
  data_provider_controller_ptr_->DisableAndDropPersistentLogs(
      [&data_provider_controller_called]() { data_provider_controller_called = true; });

  RunLoopUntilIdle();
  EXPECT_TRUE(component_data_called);
  EXPECT_TRUE(data_provider_called);
  EXPECT_TRUE(data_provider_controller_called);
  EXPECT_THAT(InspectTree(), ChildrenMatch(IsSupersetOf({
                                 AllOf(NodeMatches(NameMatches("fidl")),
                                       ChildrenMatch(IsSupersetOf({
                                           ProtocolMatcher("LastRebootInfoProvider", 0u, 0u),
                                           ProtocolMatcher("CrashReporter", 0u, 0u),
                                           ProtocolMatcher("CrashReportingProductRegister", 0u, 0u),
                                           ProtocolMatcher("ComponentDataRegister", 1u, 1u),
                                           ProtocolMatcher("DataProvider", 1u, 1u),
                                           ProtocolMatcher("DataProviderController", 1u, 1u),
                                       }))),
                             })));

  component_data_ptr_.Unbind();
  data_provider_ptr_.Unbind();
  data_provider_controller_ptr_.Unbind();

  RunLoopUntilIdle();
  EXPECT_THAT(InspectTree(), ChildrenMatch(IsSupersetOf({
                                 AllOf(NodeMatches(NameMatches("fidl")),
                                       ChildrenMatch(IsSupersetOf({
                                           ProtocolMatcher("LastRebootInfoProvider", 0u, 0u),
                                           ProtocolMatcher("CrashReporter", 0u, 0u),
                                           ProtocolMatcher("CrashReportingProductRegister", 0u, 0u),
                                           ProtocolMatcher("ComponentDataRegister", 1u, 0u),
                                           ProtocolMatcher("DataProvider", 1u, 0u),
                                           ProtocolMatcher("DataProviderController", 1u, 0u),
                                       }))),
                             })));
}

}  // namespace
}  // namespace forensics::feedback
