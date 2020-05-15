// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/feedback/last_reboot/main_service.h"

#include <fuchsia/feedback/cpp/fidl.h>
#include <lib/inspect/cpp/hierarchy.h>
#include <lib/inspect/cpp/inspector.h>
#include <lib/inspect/cpp/reader.h>
#include <lib/inspect/testing/cpp/inspect.h>
#include <lib/syslog/cpp/macros.h>

#include <memory>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/developer/feedback/testing/unit_test_fixture.h"

namespace feedback {
namespace {

using fuchsia::feedback::LastRebootInfoProviderSyncPtr;
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
  MainServiceTest()
      : inspector_(std::make_unique<inspect::Inspector>()),
        main_service_(RebootLog(RebootReason::kKernelPanic, std::nullopt, std::nullopt),
                      &inspector_->GetRoot()) {}

 protected:
  inspect::Hierarchy InspectTree() {
    auto result = inspect::ReadFromVmo(inspector_->DuplicateVmo());
    FX_CHECK(result.is_ok());
    return result.take_value();
  }

 private:
  std::unique_ptr<inspect::Inspector> inspector_;

 protected:
  MainService main_service_;
};

TEST_F(MainServiceTest, CheckInspect) {
  EXPECT_THAT(
      InspectTree(),
      ChildrenMatch(UnorderedElementsAreArray({
          AllOf(NodeMatches(NameMatches("fidl")),
                ChildrenMatch(UnorderedElementsAreArray({
                    NodeMatches(AllOf(NameMatches("fuchsia.feedback.LastRebootInfoProvider"),
                                      PropertyList(UnorderedElementsAreArray({
                                          UintIs("total_num_connections", 0u),
                                          UintIs("current_num_connections", 0u),
                                      })))),
                }))),
      })));
}

TEST_F(MainServiceTest, LastRebootInfoProvider_CheckInspect) {
  LastRebootInfoProviderSyncPtr last_reboot_info_provider_1;
  main_service_.HandleLastRebootInfoProviderRequest(last_reboot_info_provider_1.NewRequest());
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

  LastRebootInfoProviderSyncPtr last_reboot_info_provider_2;
  main_service_.HandleLastRebootInfoProviderRequest(last_reboot_info_provider_2.NewRequest());
  EXPECT_THAT(
      InspectTree(),
      ChildrenMatch(UnorderedElementsAreArray({
          AllOf(NodeMatches(NameMatches("fidl")),
                ChildrenMatch(UnorderedElementsAreArray({
                    NodeMatches(AllOf(NameMatches("fuchsia.feedback.LastRebootInfoProvider"),
                                      PropertyList(UnorderedElementsAreArray({
                                          UintIs("total_num_connections", 2u),
                                          UintIs("current_num_connections", 2u),
                                      })))),
                }))),
      })));

  last_reboot_info_provider_1.Unbind();
  RunLoopUntilIdle();
  EXPECT_THAT(
      InspectTree(),
      ChildrenMatch(UnorderedElementsAreArray({
          AllOf(NodeMatches(NameMatches("fidl")),
                ChildrenMatch(UnorderedElementsAreArray({
                    NodeMatches(AllOf(NameMatches("fuchsia.feedback.LastRebootInfoProvider"),
                                      PropertyList(UnorderedElementsAreArray({
                                          UintIs("total_num_connections", 2u),
                                          UintIs("current_num_connections", 1u),
                                      })))),
                }))),
      })));

  LastRebootInfoProviderSyncPtr last_reboot_info_provider_3;
  main_service_.HandleLastRebootInfoProviderRequest(last_reboot_info_provider_3.NewRequest());
  EXPECT_THAT(
      InspectTree(),
      ChildrenMatch(UnorderedElementsAreArray({
          AllOf(NodeMatches(NameMatches("fidl")),
                ChildrenMatch(UnorderedElementsAreArray({
                    NodeMatches(AllOf(NameMatches("fuchsia.feedback.LastRebootInfoProvider"),
                                      PropertyList(UnorderedElementsAreArray({
                                          UintIs("total_num_connections", 3u),
                                          UintIs("current_num_connections", 2u),
                                      })))),
                }))),
      })));

  last_reboot_info_provider_2.Unbind();
  last_reboot_info_provider_3.Unbind();
  RunLoopUntilIdle();
  EXPECT_THAT(
      InspectTree(),
      ChildrenMatch(UnorderedElementsAreArray({
          AllOf(NodeMatches(NameMatches("fidl")),
                ChildrenMatch(UnorderedElementsAreArray({
                    NodeMatches(AllOf(NameMatches("fuchsia.feedback.LastRebootInfoProvider"),
                                      PropertyList(UnorderedElementsAreArray({
                                          UintIs("total_num_connections", 3u),
                                          UintIs("current_num_connections", 0u),
                                      })))),
                }))),
      })));
}

}  // namespace
}  // namespace feedback
