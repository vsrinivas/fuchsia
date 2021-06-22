// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/feedback_data/main_service.h"

#include <fuchsia/feedback/cpp/fidl.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/syslog/logger.h>

#include <memory>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/developer/forensics/feedback_data/constants.h"
#include "src/developer/forensics/testing/stubs/cobalt_logger_factory.h"
#include "src/developer/forensics/testing/unit_test_fixture.h"
#include "src/lib/files/directory.h"
#include "src/lib/files/file.h"
#include "src/lib/files/path.h"
#include "src/lib/files/scoped_temp_dir.h"
#include "src/lib/timekeeper/async_test_clock.h"

namespace forensics {
namespace feedback_data {
namespace {

using fuchsia::feedback::ComponentDataRegisterSyncPtr;
using fuchsia::feedback::DataProviderControllerSyncPtr;
using fuchsia::feedback::DataProviderPtr;
using fuchsia::feedback::DataProviderSyncPtr;
using fuchsia::feedback::DeviceIdProviderSyncPtr;
using fuchsia::feedback::GetSnapshotParameters;
using fuchsia::feedback::Snapshot;
using inspect::testing::ChildrenMatch;
using inspect::testing::NameMatches;
using inspect::testing::NodeMatches;
using inspect::testing::PropertyList;
using inspect::testing::StringIs;
using inspect::testing::UintIs;
using ::testing::Contains;
using ::testing::IsEmpty;
using ::testing::UnorderedElementsAreArray;

class MainServiceTest : public UnitTestFixture {
 public:
  MainServiceTest() : clock_(dispatcher()), cobalt_(dispatcher(), services(), &clock_) {
    SetUpCobaltServer(std::make_unique<stubs::CobaltLoggerFactory>());
    RunLoopUntilIdle();
  }

 protected:
  void CreateMainService(const bool is_first_instance) {
    main_service_ = std::make_unique<MainService>(
        dispatcher(), services(), &cobalt_, &InspectRoot(), &clock_, Config{}, Error::kMissingValue,
        Error::kMissingValue, Error::kMissingValue, Error::kMissingValue, is_first_instance);
  }

  timekeeper::AsyncTestClock clock_;
  cobalt::Logger cobalt_;
  std::unique_ptr<MainService> main_service_;
};

TEST_F(MainServiceTest, DeletesUsedPreviousBootLogsAfterOneHours) {
  CreateMainService(/*is_first_instance=*/false);

  files::ScopedTempDir temp_dir;

  std::string previous_boot_logs_file;
  ASSERT_TRUE(temp_dir.NewTempFileWithData("previous boot logs", &previous_boot_logs_file));

  main_service_->DeletePreviousBootLogsAt(zx::min(10), previous_boot_logs_file);

  RunLoopFor(zx::min(10));
  EXPECT_FALSE(files::IsFile(previous_boot_logs_file));
}

TEST_F(MainServiceTest, CheckInspect) {
  CreateMainService(/*is_first_instance=*/true);
  EXPECT_THAT(
      InspectTree(),
      ChildrenMatch(UnorderedElementsAreArray({
          AllOf(NodeMatches(NameMatches("fidl")),
                ChildrenMatch(UnorderedElementsAreArray({
                    NodeMatches(AllOf(NameMatches("fuchsia.feedback.ComponentDataRegister"),
                                      PropertyList(UnorderedElementsAreArray({
                                          UintIs("total_num_connections", 0u),
                                          UintIs("current_num_connections", 0u),
                                      })))),
                    NodeMatches(AllOf(NameMatches("fuchsia.feedback.DataProvider"),
                                      PropertyList(UnorderedElementsAreArray({
                                          UintIs("total_num_connections", 0u),
                                          UintIs("current_num_connections", 0u),
                                      })))),
                    NodeMatches(AllOf(NameMatches("fuchsia.feedback.DataProviderController"),
                                      PropertyList(UnorderedElementsAreArray({
                                          UintIs("total_num_connections", 0u),
                                          UintIs("current_num_connections", 0u),
                                      })))),
                    NodeMatches(AllOf(NameMatches("fuchsia.feedback.DeviceIdProvider"),
                                      PropertyList(UnorderedElementsAreArray({
                                          UintIs("total_num_connections", 0u),
                                          UintIs("current_num_connections", 0u),
                                      })))),
                }))),
          AllOf(NodeMatches(
                    AllOf(NameMatches("inspect_budget"), PropertyList(UnorderedElementsAreArray({
                                                             StringIs("is_budget_enabled", "false"),
                                                         })))),
                ChildrenMatch(IsEmpty())),
      })));
}

TEST_F(MainServiceTest, ComponentDataRegister_CheckInspect) {
  CreateMainService(/*is_first_instance=*/true);
  ComponentDataRegisterSyncPtr data_register_1;
  main_service_->HandleComponentDataRegisterRequest(data_register_1.NewRequest());
  EXPECT_THAT(
      InspectTree(),
      ChildrenMatch(Contains(AllOf(NodeMatches(NameMatches("fidl")),
                                   ChildrenMatch(Contains(NodeMatches(
                                       AllOf(NameMatches("fuchsia.feedback.ComponentDataRegister"),
                                             PropertyList(UnorderedElementsAreArray({
                                                 UintIs("total_num_connections", 1u),
                                                 UintIs("current_num_connections", 1u),
                                             }))))))))));

  ComponentDataRegisterSyncPtr data_register_2;
  main_service_->HandleComponentDataRegisterRequest(data_register_2.NewRequest());
  EXPECT_THAT(
      InspectTree(),
      ChildrenMatch(Contains(AllOf(NodeMatches(NameMatches("fidl")),
                                   ChildrenMatch(Contains(NodeMatches(
                                       AllOf(NameMatches("fuchsia.feedback.ComponentDataRegister"),
                                             PropertyList(UnorderedElementsAreArray({
                                                 UintIs("total_num_connections", 2u),
                                                 UintIs("current_num_connections", 2u),
                                             }))))))))));

  data_register_1.Unbind();
  RunLoopUntilIdle();
  EXPECT_THAT(
      InspectTree(),
      ChildrenMatch(Contains(AllOf(NodeMatches(NameMatches("fidl")),
                                   ChildrenMatch(Contains(NodeMatches(
                                       AllOf(NameMatches("fuchsia.feedback.ComponentDataRegister"),
                                             PropertyList(UnorderedElementsAreArray({
                                                 UintIs("total_num_connections", 2u),
                                                 UintIs("current_num_connections", 1u),
                                             }))))))))));

  ComponentDataRegisterSyncPtr data_register_3;
  main_service_->HandleComponentDataRegisterRequest(data_register_3.NewRequest());
  EXPECT_THAT(
      InspectTree(),
      ChildrenMatch(Contains(AllOf(NodeMatches(NameMatches("fidl")),
                                   ChildrenMatch(Contains(NodeMatches(
                                       AllOf(NameMatches("fuchsia.feedback.ComponentDataRegister"),
                                             PropertyList(UnorderedElementsAreArray({
                                                 UintIs("total_num_connections", 3u),
                                                 UintIs("current_num_connections", 2u),
                                             }))))))))));

  data_register_2.Unbind();
  data_register_3.Unbind();
  RunLoopUntilIdle();
  EXPECT_THAT(
      InspectTree(),
      ChildrenMatch(Contains(AllOf(NodeMatches(NameMatches("fidl")),
                                   ChildrenMatch(Contains(NodeMatches(
                                       AllOf(NameMatches("fuchsia.feedback.ComponentDataRegister"),
                                             PropertyList(UnorderedElementsAreArray({
                                                 UintIs("total_num_connections", 3u),
                                                 UintIs("current_num_connections", 0u),
                                             }))))))))));
}

TEST_F(MainServiceTest, DataProvider_CheckInspect) {
  CreateMainService(/*is_first_instance=*/true);
  DataProviderSyncPtr data_provider_1;
  main_service_->HandleDataProviderRequest(data_provider_1.NewRequest());
  EXPECT_THAT(
      InspectTree(),
      ChildrenMatch(Contains(AllOf(
          NodeMatches(NameMatches("fidl")),
          ChildrenMatch(Contains(NodeMatches(AllOf(NameMatches("fuchsia.feedback.DataProvider"),
                                                   PropertyList(UnorderedElementsAreArray({
                                                       UintIs("total_num_connections", 1u),
                                                       UintIs("current_num_connections", 1u),
                                                   }))))))))));

  DataProviderSyncPtr data_provider_2;
  main_service_->HandleDataProviderRequest(data_provider_2.NewRequest());
  EXPECT_THAT(
      InspectTree(),
      ChildrenMatch(Contains(AllOf(
          NodeMatches(NameMatches("fidl")),
          ChildrenMatch(Contains(NodeMatches(AllOf(NameMatches("fuchsia.feedback.DataProvider"),
                                                   PropertyList(UnorderedElementsAreArray({
                                                       UintIs("total_num_connections", 2u),
                                                       UintIs("current_num_connections", 2u),
                                                   }))))))))));

  data_provider_1.Unbind();
  RunLoopUntilIdle();
  EXPECT_THAT(
      InspectTree(),
      ChildrenMatch(Contains(AllOf(
          NodeMatches(NameMatches("fidl")),
          ChildrenMatch(Contains(NodeMatches(AllOf(NameMatches("fuchsia.feedback.DataProvider"),
                                                   PropertyList(UnorderedElementsAreArray({
                                                       UintIs("total_num_connections", 2u),
                                                       UintIs("current_num_connections", 1u),
                                                   }))))))))));

  DataProviderSyncPtr data_provider_3;
  main_service_->HandleDataProviderRequest(data_provider_3.NewRequest());
  EXPECT_THAT(
      InspectTree(),
      ChildrenMatch(Contains(AllOf(
          NodeMatches(NameMatches("fidl")),
          ChildrenMatch(Contains(NodeMatches(AllOf(NameMatches("fuchsia.feedback.DataProvider"),
                                                   PropertyList(UnorderedElementsAreArray({
                                                       UintIs("total_num_connections", 3u),
                                                       UintIs("current_num_connections", 2u),
                                                   }))))))))));

  data_provider_2.Unbind();
  data_provider_3.Unbind();
  RunLoopUntilIdle();
  EXPECT_THAT(
      InspectTree(),
      ChildrenMatch(Contains(AllOf(
          NodeMatches(NameMatches("fidl")),
          ChildrenMatch(Contains(NodeMatches(AllOf(NameMatches("fuchsia.feedback.DataProvider"),
                                                   PropertyList(UnorderedElementsAreArray({
                                                       UintIs("total_num_connections", 3u),
                                                       UintIs("current_num_connections", 0u),
                                                   }))))))))));
}

TEST_F(MainServiceTest, DataProviderController_CheckInspect) {
  CreateMainService(/*is_first_instance=*/true);
  DataProviderControllerSyncPtr data_provider_controller_1;
  main_service_->HandleDataProviderControllerRequest(data_provider_controller_1.NewRequest());
  EXPECT_THAT(
      InspectTree(),
      ChildrenMatch(Contains(AllOf(NodeMatches(NameMatches("fidl")),
                                   ChildrenMatch(Contains(NodeMatches(
                                       AllOf(NameMatches("fuchsia.feedback.DataProviderController"),
                                             PropertyList(UnorderedElementsAreArray({
                                                 UintIs("total_num_connections", 1u),
                                                 UintIs("current_num_connections", 1u),
                                             }))))))))));

  DataProviderControllerSyncPtr data_provider_controller_2;
  main_service_->HandleDataProviderControllerRequest(data_provider_controller_2.NewRequest());
  EXPECT_THAT(
      InspectTree(),
      ChildrenMatch(Contains(AllOf(NodeMatches(NameMatches("fidl")),
                                   ChildrenMatch(Contains(NodeMatches(
                                       AllOf(NameMatches("fuchsia.feedback.DataProviderController"),
                                             PropertyList(UnorderedElementsAreArray({
                                                 UintIs("total_num_connections", 2u),
                                                 UintIs("current_num_connections", 2u),
                                             }))))))))));

  data_provider_controller_1.Unbind();
  RunLoopUntilIdle();
  EXPECT_THAT(
      InspectTree(),
      ChildrenMatch(Contains(AllOf(NodeMatches(NameMatches("fidl")),
                                   ChildrenMatch(Contains(NodeMatches(
                                       AllOf(NameMatches("fuchsia.feedback.DataProviderController"),
                                             PropertyList(UnorderedElementsAreArray({
                                                 UintIs("total_num_connections", 2u),
                                                 UintIs("current_num_connections", 1u),
                                             }))))))))));

  DataProviderControllerSyncPtr data_provider_controller_3;
  main_service_->HandleDataProviderControllerRequest(data_provider_controller_3.NewRequest());
  EXPECT_THAT(
      InspectTree(),
      ChildrenMatch(Contains(AllOf(NodeMatches(NameMatches("fidl")),
                                   ChildrenMatch(Contains(NodeMatches(
                                       AllOf(NameMatches("fuchsia.feedback.DataProviderController"),
                                             PropertyList(UnorderedElementsAreArray({
                                                 UintIs("total_num_connections", 3u),
                                                 UintIs("current_num_connections", 2u),
                                             }))))))))));

  data_provider_controller_2.Unbind();
  data_provider_controller_3.Unbind();
  RunLoopUntilIdle();
  EXPECT_THAT(
      InspectTree(),
      ChildrenMatch(Contains(AllOf(NodeMatches(NameMatches("fidl")),
                                   ChildrenMatch(Contains(NodeMatches(
                                       AllOf(NameMatches("fuchsia.feedback.DataProviderController"),
                                             PropertyList(UnorderedElementsAreArray({
                                                 UintIs("total_num_connections", 3u),
                                                 UintIs("current_num_connections", 0u),
                                             }))))))))));
}

TEST_F(MainServiceTest, DeviceIdProvider_CheckInspect) {
  CreateMainService(/*is_first_instance=*/true);
  DeviceIdProviderSyncPtr device_id_provider_1;
  main_service_->HandleDeviceIdProviderRequest(device_id_provider_1.NewRequest());
  EXPECT_THAT(
      InspectTree(),
      ChildrenMatch(Contains(AllOf(
          NodeMatches(NameMatches("fidl")),
          ChildrenMatch(Contains(NodeMatches(AllOf(NameMatches("fuchsia.feedback.DeviceIdProvider"),
                                                   PropertyList(UnorderedElementsAreArray({
                                                       UintIs("total_num_connections", 1u),
                                                       UintIs("current_num_connections", 1u),
                                                   }))))))))));

  DeviceIdProviderSyncPtr device_id_provider_2;
  main_service_->HandleDeviceIdProviderRequest(device_id_provider_2.NewRequest());
  EXPECT_THAT(
      InspectTree(),
      ChildrenMatch(Contains(AllOf(
          NodeMatches(NameMatches("fidl")),
          ChildrenMatch(Contains(NodeMatches(AllOf(NameMatches("fuchsia.feedback.DeviceIdProvider"),
                                                   PropertyList(UnorderedElementsAreArray({
                                                       UintIs("total_num_connections", 2u),
                                                       UintIs("current_num_connections", 2u),
                                                   }))))))))));

  device_id_provider_1.Unbind();
  RunLoopUntilIdle();
  EXPECT_THAT(
      InspectTree(),
      ChildrenMatch(Contains(AllOf(
          NodeMatches(NameMatches("fidl")),
          ChildrenMatch(Contains(NodeMatches(AllOf(NameMatches("fuchsia.feedback.DeviceIdProvider"),
                                                   PropertyList(UnorderedElementsAreArray({
                                                       UintIs("total_num_connections", 2u),
                                                       UintIs("current_num_connections", 1u),
                                                   }))))))))));

  DeviceIdProviderSyncPtr device_id_provider_3;
  main_service_->HandleDeviceIdProviderRequest(device_id_provider_3.NewRequest());
  EXPECT_THAT(
      InspectTree(),
      ChildrenMatch(Contains(AllOf(
          NodeMatches(NameMatches("fidl")),
          ChildrenMatch(Contains(NodeMatches(AllOf(NameMatches("fuchsia.feedback.DeviceIdProvider"),
                                                   PropertyList(UnorderedElementsAreArray({
                                                       UintIs("total_num_connections", 3u),
                                                       UintIs("current_num_connections", 2u),
                                                   }))))))))));

  device_id_provider_2.Unbind();
  device_id_provider_3.Unbind();
  RunLoopUntilIdle();
  EXPECT_THAT(
      InspectTree(),
      ChildrenMatch(Contains(AllOf(
          NodeMatches(NameMatches("fidl")),
          ChildrenMatch(Contains(NodeMatches(AllOf(NameMatches("fuchsia.feedback.DeviceIdProvider"),
                                                   PropertyList(UnorderedElementsAreArray({
                                                       UintIs("total_num_connections", 3u),
                                                       UintIs("current_num_connections", 0u),
                                                   }))))))))));
}

}  // namespace
}  // namespace feedback_data
}  // namespace forensics
