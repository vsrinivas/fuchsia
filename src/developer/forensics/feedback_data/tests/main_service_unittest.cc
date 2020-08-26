// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/feedback_data/main_service.h"

#include <fuchsia/feedback/cpp/fidl.h>
#include <lib/syslog/cpp/macros.h>

#include <memory>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/developer/forensics/testing/stubs/cobalt_logger_factory.h"
#include "src/developer/forensics/testing/unit_test_fixture.h"

namespace forensics {
namespace feedback_data {
namespace {

using fuchsia::feedback::ComponentDataRegisterSyncPtr;
using fuchsia::feedback::DataProviderSyncPtr;
using fuchsia::feedback::DeviceIdProviderSyncPtr;
using inspect::testing::ChildrenMatch;
using inspect::testing::NameMatches;
using inspect::testing::NodeMatches;
using inspect::testing::PropertyList;
using inspect::testing::StringIs;
using inspect::testing::UintIs;
using testing::Contains;
using testing::UnorderedElementsAreArray;

class MainServiceTest : public UnitTestFixture {
 public:
  void SetUp() {
    SetUpCobaltServer(std::make_unique<stubs::CobaltLoggerFactory>());
    RunLoopUntilIdle();
    main_service_ = MainService::TryCreate(dispatcher(), services(), &InspectRoot(),
                                           /*is_first_instance=*/true);
  }

 protected:
  std::unique_ptr<MainService> main_service_;
};

TEST_F(MainServiceTest, CheckInspect) {
  EXPECT_THAT(InspectTree(),
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
                            NodeMatches(AllOf(NameMatches("fuchsia.feedback.DeviceIdProvider"),
                                              PropertyList(UnorderedElementsAreArray({
                                                  UintIs("total_num_connections", 0u),
                                                  UintIs("current_num_connections", 0u),
                                              })))),
                        }))),
              })));
}

TEST_F(MainServiceTest, ComponentDataRegister_CheckInspect) {
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

TEST_F(MainServiceTest, DeviceIdProvider_CheckInspect) {
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
