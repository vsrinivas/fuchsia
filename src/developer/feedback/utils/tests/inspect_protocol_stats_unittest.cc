// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/feedback/utils/inspect_protocol_stats.h"

#include <lib/inspect/testing/cpp/inspect.h>
#include <lib/syslog/cpp/macros.h>

#include <string>

#include "src/developer/feedback/utils/inspect_node_manager.h"

namespace forensics {
namespace {

using inspect::testing::ChildrenMatch;
using inspect::testing::NameMatches;
using inspect::testing::NodeMatches;
using inspect::testing::PropertyList;
using inspect::testing::UintIs;
using testing::AllOf;
using testing::UnorderedElementsAreArray;

class InspectProtocolStatsTest : public testing::Test {
 public:
  void SetUpProtocolStats(const std::string &path) {
    inspector_ = std::make_unique<inspect::Inspector>();
    inspect_node_manager_ = std::make_unique<InspectNodeManager>(&inspector_->GetRoot());
    protocol_stats_ = std::make_unique<InspectProtocolStats>(inspect_node_manager_.get(), path);
  }

 protected:
  inspect::Hierarchy InspectTree() {
    auto result = inspect::ReadFromVmo(inspector_->DuplicateVmo());
    FX_CHECK(result.is_ok());
    return result.take_value();
  }

  std::unique_ptr<InspectNodeManager> inspect_node_manager_;
  std::unique_ptr<InspectProtocolStats> protocol_stats_;

 private:
  std::unique_ptr<inspect::Inspector> inspector_;
};

TEST_F(InspectProtocolStatsTest, Check_MakingAndClosingConnections) {
  SetUpProtocolStats("/fidl");

  EXPECT_THAT(InspectTree(), ChildrenMatch(ElementsAre(NodeMatches(AllOf(
                                 NameMatches("fidl"), PropertyList(UnorderedElementsAreArray({
                                                          UintIs("current_num_connections", 0u),
                                                          UintIs("total_num_connections", 0u),
                                                      })))))));
  // 2 New connections: 2 created, 2 active
  protocol_stats_->NewConnection();
  protocol_stats_->NewConnection();

  EXPECT_THAT(InspectTree(), ChildrenMatch(ElementsAre(NodeMatches(AllOf(
                                 NameMatches("fidl"), PropertyList(UnorderedElementsAreArray({
                                                          UintIs("current_num_connections", 2u),
                                                          UintIs("total_num_connections", 2u),
                                                      })))))));

  // Close 1 connection: 2 created, 1 active
  protocol_stats_->CloseConnection();

  EXPECT_THAT(InspectTree(), ChildrenMatch(ElementsAre(NodeMatches(AllOf(
                                 NameMatches("fidl"), PropertyList(UnorderedElementsAreArray({
                                                          UintIs("current_num_connections", 1u),
                                                          UintIs("total_num_connections", 2u),
                                                      })))))));

  // 1 New Connection: 3 created, 2 active
  protocol_stats_->NewConnection();

  EXPECT_THAT(InspectTree(), ChildrenMatch(ElementsAre(NodeMatches(AllOf(
                                 NameMatches("fidl"), PropertyList(UnorderedElementsAreArray({
                                                          UintIs("current_num_connections", 2u),
                                                          UintIs("total_num_connections", 3u),
                                                      })))))));

  // Close 2 connections: 3 created, 0 active
  protocol_stats_->CloseConnection();
  protocol_stats_->CloseConnection();

  EXPECT_THAT(InspectTree(), ChildrenMatch(ElementsAre(NodeMatches(AllOf(
                                 NameMatches("fidl"), PropertyList(UnorderedElementsAreArray({
                                                          UintIs("current_num_connections", 0u),
                                                          UintIs("total_num_connections", 3u),
                                                      })))))));
}

}  // namespace
}  // namespace forensics
