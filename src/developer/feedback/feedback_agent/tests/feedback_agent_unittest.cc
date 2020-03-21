// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/feedback/feedback_agent/feedback_agent.h"

#include <fuchsia/feedback/cpp/fidl.h>
#include <lib/inspect/cpp/hierarchy.h>
#include <lib/inspect/cpp/inspector.h>
#include <lib/inspect/cpp/reader.h>
#include <lib/inspect/testing/cpp/inspect.h>

#include <memory>

#include "src/developer/feedback/testing/cobalt_test_fixture.h"
#include "src/developer/feedback/testing/stubs/cobalt_logger_factory.h"
#include "src/developer/feedback/testing/unit_test_fixture.h"
#include "src/lib/syslog/cpp/logger.h"
#include "third_party/googletest/googlemock/include/gmock/gmock.h"
#include "third_party/googletest/googletest/include/gtest/gtest.h"

namespace feedback {
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

class FeedbackAgentTest : public UnitTestFixture, public CobaltTestFixture {
 public:
  FeedbackAgentTest() : UnitTestFixture(), CobaltTestFixture(/*unit_test_fixture=*/this) {}

  void SetUp() {
    SetUpCobaltLoggerFactory(std::make_unique<stubs::CobaltLoggerFactory>());
    RunLoopUntilIdle();
    inspector_ = std::make_unique<inspect::Inspector>();
    feedback_agent_ = FeedbackAgent::TryCreate(dispatcher(), services(), &inspector_->GetRoot());
  }

 protected:
  inspect::Hierarchy InspectTree() {
    auto result = inspect::ReadFromVmo(inspector_->DuplicateVmo());
    FX_CHECK(result.is_ok());
    return result.take_value();
  }

 private:
  std::unique_ptr<inspect::Inspector> inspector_;

 protected:
  std::unique_ptr<FeedbackAgent> feedback_agent_;
};

TEST_F(FeedbackAgentTest, CheckInspect) {
  EXPECT_THAT(InspectTree(),
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
              })));
}

TEST_F(FeedbackAgentTest, ComponentDataRegister_CheckInspect) {
  ComponentDataRegisterSyncPtr data_register_1;
  feedback_agent_->HandleComponentDataRegisterRequest(data_register_1.NewRequest());
  EXPECT_THAT(InspectTree(), ChildrenMatch(Contains(NodeMatches(
                                 AllOf(NameMatches("fuchsia.feedback.ComponentDataRegister"),
                                       PropertyList(UnorderedElementsAreArray({
                                           UintIs("total_num_connections", 1u),
                                           UintIs("current_num_connections", 1u),
                                       })))))));

  ComponentDataRegisterSyncPtr data_register_2;
  feedback_agent_->HandleComponentDataRegisterRequest(data_register_2.NewRequest());
  EXPECT_THAT(InspectTree(), ChildrenMatch(Contains(NodeMatches(
                                 AllOf(NameMatches("fuchsia.feedback.ComponentDataRegister"),
                                       PropertyList(UnorderedElementsAreArray({
                                           UintIs("total_num_connections", 2u),
                                           UintIs("current_num_connections", 2u),
                                       })))))));

  data_register_1.Unbind();
  RunLoopUntilIdle();
  EXPECT_THAT(InspectTree(), ChildrenMatch(Contains(NodeMatches(
                                 AllOf(NameMatches("fuchsia.feedback.ComponentDataRegister"),
                                       PropertyList(UnorderedElementsAreArray({
                                           UintIs("total_num_connections", 2u),
                                           UintIs("current_num_connections", 1u),
                                       })))))));

  ComponentDataRegisterSyncPtr data_register_3;
  feedback_agent_->HandleComponentDataRegisterRequest(data_register_3.NewRequest());
  EXPECT_THAT(InspectTree(), ChildrenMatch(Contains(NodeMatches(
                                 AllOf(NameMatches("fuchsia.feedback.ComponentDataRegister"),
                                       PropertyList(UnorderedElementsAreArray({
                                           UintIs("total_num_connections", 3u),
                                           UintIs("current_num_connections", 2u),
                                       })))))));

  data_register_2.Unbind();
  data_register_3.Unbind();
  RunLoopUntilIdle();
  EXPECT_THAT(InspectTree(), ChildrenMatch(Contains(NodeMatches(
                                 AllOf(NameMatches("fuchsia.feedback.ComponentDataRegister"),
                                       PropertyList(UnorderedElementsAreArray({
                                           UintIs("total_num_connections", 3u),
                                           UintIs("current_num_connections", 0u),
                                       })))))));
}

TEST_F(FeedbackAgentTest, DataProvider_CheckInspect) {
  DataProviderSyncPtr data_provider_1;
  feedback_agent_->HandleDataProviderRequest(data_provider_1.NewRequest());
  EXPECT_THAT(InspectTree(),
              ChildrenMatch(Contains(NodeMatches(AllOf(NameMatches("fuchsia.feedback.DataProvider"),
                                                       PropertyList(UnorderedElementsAreArray({
                                                           UintIs("total_num_connections", 1u),
                                                           UintIs("current_num_connections", 1u),
                                                       })))))));

  DataProviderSyncPtr data_provider_2;
  feedback_agent_->HandleDataProviderRequest(data_provider_2.NewRequest());
  EXPECT_THAT(InspectTree(),
              ChildrenMatch(Contains(NodeMatches(AllOf(NameMatches("fuchsia.feedback.DataProvider"),
                                                       PropertyList(UnorderedElementsAreArray({
                                                           UintIs("total_num_connections", 2u),
                                                           UintIs("current_num_connections", 2u),
                                                       })))))));

  data_provider_1.Unbind();
  RunLoopUntilIdle();
  EXPECT_THAT(InspectTree(),
              ChildrenMatch(Contains(NodeMatches(AllOf(NameMatches("fuchsia.feedback.DataProvider"),
                                                       PropertyList(UnorderedElementsAreArray({
                                                           UintIs("total_num_connections", 2u),
                                                           UintIs("current_num_connections", 1u),
                                                       })))))));

  DataProviderSyncPtr data_provider_3;
  feedback_agent_->HandleDataProviderRequest(data_provider_3.NewRequest());
  EXPECT_THAT(InspectTree(),
              ChildrenMatch(Contains(NodeMatches(AllOf(NameMatches("fuchsia.feedback.DataProvider"),
                                                       PropertyList(UnorderedElementsAreArray({
                                                           UintIs("total_num_connections", 3u),
                                                           UintIs("current_num_connections", 2u),
                                                       })))))));

  data_provider_2.Unbind();
  data_provider_3.Unbind();
  RunLoopUntilIdle();
  EXPECT_THAT(InspectTree(),
              ChildrenMatch(Contains(NodeMatches(AllOf(NameMatches("fuchsia.feedback.DataProvider"),
                                                       PropertyList(UnorderedElementsAreArray({
                                                           UintIs("total_num_connections", 3u),
                                                           UintIs("current_num_connections", 0u),
                                                       })))))));
}

TEST_F(FeedbackAgentTest, DeviceIdProvider_CheckInspect) {
  DeviceIdProviderSyncPtr device_id_provider_1;
  feedback_agent_->HandleDeviceIdProviderRequest(device_id_provider_1.NewRequest());
  EXPECT_THAT(
      InspectTree(),
      ChildrenMatch(Contains(NodeMatches(AllOf(NameMatches("fuchsia.feedback.DeviceIdProvider"),
                                               PropertyList(UnorderedElementsAreArray({
                                                   UintIs("total_num_connections", 1u),
                                                   UintIs("current_num_connections", 1u),
                                               })))))));

  DeviceIdProviderSyncPtr device_id_provider_2;
  feedback_agent_->HandleDeviceIdProviderRequest(device_id_provider_2.NewRequest());
  EXPECT_THAT(
      InspectTree(),
      ChildrenMatch(Contains(NodeMatches(AllOf(NameMatches("fuchsia.feedback.DeviceIdProvider"),
                                               PropertyList(UnorderedElementsAreArray({
                                                   UintIs("total_num_connections", 2u),
                                                   UintIs("current_num_connections", 2u),
                                               })))))));

  device_id_provider_1.Unbind();
  RunLoopUntilIdle();
  EXPECT_THAT(
      InspectTree(),
      ChildrenMatch(Contains(NodeMatches(AllOf(NameMatches("fuchsia.feedback.DeviceIdProvider"),
                                               PropertyList(UnorderedElementsAreArray({
                                                   UintIs("total_num_connections", 2u),
                                                   UintIs("current_num_connections", 1u),
                                               })))))));

  DeviceIdProviderSyncPtr device_id_provider_3;
  feedback_agent_->HandleDeviceIdProviderRequest(device_id_provider_3.NewRequest());
  EXPECT_THAT(
      InspectTree(),
      ChildrenMatch(Contains(NodeMatches(AllOf(NameMatches("fuchsia.feedback.DeviceIdProvider"),
                                               PropertyList(UnorderedElementsAreArray({
                                                   UintIs("total_num_connections", 3u),
                                                   UintIs("current_num_connections", 2u),
                                               })))))));

  device_id_provider_2.Unbind();
  device_id_provider_3.Unbind();
  RunLoopUntilIdle();
  EXPECT_THAT(
      InspectTree(),
      ChildrenMatch(Contains(NodeMatches(AllOf(NameMatches("fuchsia.feedback.DeviceIdProvider"),
                                               PropertyList(UnorderedElementsAreArray({
                                                   UintIs("total_num_connections", 3u),
                                                   UintIs("current_num_connections", 0u),
                                               })))))));
}

}  // namespace
}  // namespace feedback
