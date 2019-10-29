// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/feedback/utils/inspect_node_manager.h"

#include <lib/inspect/cpp/hierarchy.h>
#include <lib/inspect/cpp/inspect.h>
#include <lib/inspect/cpp/reader.h>

#include <string>
#include <vector>

#include "sdk/lib/inspect/testing/cpp/inspect.h"
#include "src/lib/fxl/logging.h"
#include "src/lib/fxl/test/test_settings.h"
#include "src/lib/syslog/cpp/logger.h"
#include "third_party/googletest/googlemock/include/gmock/gmock.h"
#include "third_party/googletest/googletest/include/gtest/gtest.h"

namespace feedback {
namespace {

using inspect::testing::ChildrenMatch;
using inspect::testing::NameMatches;
using inspect::testing::NodeMatches;
using inspect::testing::PropertyList;
using inspect::testing::StringIs;
using inspect::testing::UintIs;
using testing::AllOf;
using testing::Contains;
using testing::ElementsAreArray;
using testing::HasSubstr;
using testing::IsEmpty;
using testing::UnorderedElementsAreArray;

class InspectNodeManagerTest : public testing::Test {
 public:
  void SetUp() override {
    inspector_ = std::make_unique<inspect::Inspector>();
    inspect_node_manager_ = std::make_unique<InspectNodeManager>(&inspector_->GetRoot());
  }

 protected:
  inspect::Hierarchy InspectTree() {
    auto result = inspect::ReadFromVmo(inspector_->DuplicateVmo());
    FXL_CHECK(result.is_ok());
    return result.take_value();
  }

  std::unique_ptr<InspectNodeManager> inspect_node_manager_;

 private:
  std::unique_ptr<inspect::Inspector> inspector_;
};

TEST_F(InspectNodeManagerTest, Check_Get_RootNode) {
  EXPECT_TRUE(inspect_node_manager_->Get(""));
  EXPECT_TRUE(inspect_node_manager_->Get("/"));
  EXPECT_THAT(InspectTree(), ChildrenMatch(IsEmpty()));
}

TEST_F(InspectNodeManagerTest, Check_Get_MultipleLevelOneNodes) {
  EXPECT_TRUE(inspect_node_manager_->Get("/child1"));
  EXPECT_TRUE(inspect_node_manager_->Get("/child2"));
  EXPECT_TRUE(inspect_node_manager_->Get("/child3"));

  EXPECT_THAT(InspectTree(), ChildrenMatch(UnorderedElementsAreArray({
                                 NodeMatches(NameMatches("child1")),
                                 NodeMatches(NameMatches("child2")),
                                 NodeMatches(NameMatches("child3")),
                             })));
}

TEST_F(InspectNodeManagerTest, Check_Get_NodeAlreadyExists) {
  EXPECT_TRUE(inspect_node_manager_->Get("/child1"));
  EXPECT_TRUE(inspect_node_manager_->Get("/child1"));

  EXPECT_THAT(InspectTree(), ChildrenMatch(UnorderedElementsAreArray({
                                 NodeMatches(NameMatches("child1")),
                             })));
}

TEST_F(InspectNodeManagerTest, Check_Get_OneLevelTwoNode) {
  EXPECT_TRUE(inspect_node_manager_->Get("/child1/grandchild1.1"));

  EXPECT_THAT(InspectTree(),
              ChildrenMatch(ElementsAre(
                  AllOf(NodeMatches(NameMatches("child1")),
                        ChildrenMatch(ElementsAre(NodeMatches(NameMatches("grandchild1.1"))))))));
}

TEST_F(InspectNodeManagerTest, Check_Get_MultipleLevelTwoNodes) {
  EXPECT_TRUE(inspect_node_manager_->Get("/child1"));
  EXPECT_TRUE(inspect_node_manager_->Get("/child1/grandchild1.1"));
  EXPECT_TRUE(inspect_node_manager_->Get("/child1/grandchild1.2"));

  EXPECT_THAT(InspectTree(),
              ChildrenMatch(ElementsAre(AllOf(NodeMatches(NameMatches("child1")),
                                              ChildrenMatch(UnorderedElementsAreArray({
                                                  NodeMatches(NameMatches("grandchild1.1")),
                                                  NodeMatches(NameMatches("grandchild1.2")),
                                              }))))));
}

TEST_F(InspectNodeManagerTest, Check_Get_OneLevelThreeNode) {
  EXPECT_TRUE(inspect_node_manager_->Get("/child1/grandchild1.1/greatgrandchild1.1.1"));

  EXPECT_THAT(
      InspectTree(),
      ChildrenMatch(ElementsAre(AllOf(
          NodeMatches(NameMatches("child1")),
          ChildrenMatch(UnorderedElementsAreArray({
              AllOf(NodeMatches(NameMatches("grandchild1.1")),
                    ChildrenMatch(ElementsAre(NodeMatches(NameMatches("greatgrandchild1.1.1"))))),
          }))))));
}

TEST_F(InspectNodeManagerTest, Check_Update_OneLevelThreeNode) {
  inspect::Node* node = inspect_node_manager_->Get("/child1/grandchild1.1/greatgrandchild1.1.1");
  ASSERT_TRUE(node);

  inspect::StringProperty s = node->CreateString("string", "value");
  EXPECT_THAT(InspectTree(),
              ChildrenMatch(ElementsAre(
                  AllOf(NodeMatches(NameMatches("child1")),
                        ChildrenMatch(UnorderedElementsAreArray({
                            AllOf(NodeMatches(NameMatches("grandchild1.1")),
                                  ChildrenMatch(ElementsAre(NodeMatches(AllOf(
                                      NameMatches("greatgrandchild1.1.1"),
                                      PropertyList(ElementsAre(StringIs("string", "value")))))))),
                        }))))));

  node = inspect_node_manager_->Get("/child1/grandchild1.1/greatgrandchild1.1.1");
  ASSERT_TRUE(node);

  inspect::UintProperty u = node->CreateUint("uint", 10u);
  EXPECT_THAT(InspectTree(),
              ChildrenMatch(ElementsAre(AllOf(NodeMatches(NameMatches("child1")),
                                              ChildrenMatch(UnorderedElementsAreArray({
                                                  AllOf(NodeMatches(NameMatches("grandchild1.1")),
                                                        ChildrenMatch(ElementsAre(NodeMatches(AllOf(
                                                            NameMatches("greatgrandchild1.1.1"),
                                                            PropertyList(UnorderedElementsAreArray({
                                                                StringIs("string", "value"),
                                                                UintIs("uint", 10u),
                                                            }))))))),
                                              }))))));
}

}  // namespace
}  // namespace feedback

int main(int argc, char** argv) {
  if (!fxl::SetTestSettings(argc, argv)) {
    return EXIT_FAILURE;
  }

  testing::InitGoogleTest(&argc, argv);
  syslog::InitLogger({"feedback", "test"});
  return RUN_ALL_TESTS();
}
