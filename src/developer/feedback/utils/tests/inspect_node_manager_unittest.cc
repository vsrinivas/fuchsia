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
  inspect_node_manager_->Get("");
  inspect_node_manager_->Get("/");
  EXPECT_THAT(InspectTree(), ChildrenMatch(IsEmpty()));
}

TEST_F(InspectNodeManagerTest, Check_Get_MultipleLevelOneNodes) {
  inspect_node_manager_->Get("/child1");
  inspect_node_manager_->Get("/child2");
  inspect_node_manager_->Get("/child3");

  EXPECT_THAT(InspectTree(), ChildrenMatch(UnorderedElementsAreArray({
                                 NodeMatches(NameMatches("child1")),
                                 NodeMatches(NameMatches("child2")),
                                 NodeMatches(NameMatches("child3")),
                             })));
}

TEST_F(InspectNodeManagerTest, Check_Get_NodeAlreadyExists) {
  inspect_node_manager_->Get("/child1");
  inspect_node_manager_->Get("/child1");

  EXPECT_THAT(InspectTree(), ChildrenMatch(UnorderedElementsAreArray({
                                 NodeMatches(NameMatches("child1")),
                             })));
}

TEST_F(InspectNodeManagerTest, Check_Get_OneLevelTwoNode) {
  inspect_node_manager_->Get("/child1/grandchild1.1");

  EXPECT_THAT(InspectTree(),
              ChildrenMatch(ElementsAre(
                  AllOf(NodeMatches(NameMatches("child1")),
                        ChildrenMatch(ElementsAre(NodeMatches(NameMatches("grandchild1.1"))))))));
}

TEST_F(InspectNodeManagerTest, Check_Get_MultipleLevelTwoNodes) {
  inspect_node_manager_->Get("/child1");
  inspect_node_manager_->Get("/child1/grandchild1.1");
  inspect_node_manager_->Get("/child1/grandchild1.2");

  EXPECT_THAT(InspectTree(),
              ChildrenMatch(ElementsAre(AllOf(NodeMatches(NameMatches("child1")),
                                              ChildrenMatch(UnorderedElementsAreArray({
                                                  NodeMatches(NameMatches("grandchild1.1")),
                                                  NodeMatches(NameMatches("grandchild1.2")),
                                              }))))));
}

TEST_F(InspectNodeManagerTest, Check_Get_OneLevelThreeNode) {
  inspect_node_manager_->Get("/child1/grandchild1.1/greatgrandchild1.1.1");

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
  inspect::Node& node = inspect_node_manager_->Get("/child1/grandchild1.1/greatgrandchild1.1.1");

  inspect::StringProperty s = node.CreateString("string", "value");
  EXPECT_THAT(InspectTree(),
              ChildrenMatch(ElementsAre(
                  AllOf(NodeMatches(NameMatches("child1")),
                        ChildrenMatch(UnorderedElementsAreArray({
                            AllOf(NodeMatches(NameMatches("grandchild1.1")),
                                  ChildrenMatch(ElementsAre(NodeMatches(AllOf(
                                      NameMatches("greatgrandchild1.1.1"),
                                      PropertyList(ElementsAre(StringIs("string", "value")))))))),
                        }))))));

  inspect::UintProperty u = node.CreateUint("uint", 10u);
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

TEST_F(InspectNodeManagerTest, Check_Remove_LevelOneNode) {
  inspect_node_manager_->Get("/child1");

  EXPECT_THAT(InspectTree(), ChildrenMatch(UnorderedElementsAreArray({
                                 NodeMatches(NameMatches("child1")),
                             })));

  EXPECT_TRUE(inspect_node_manager_->Remove("/child1"));
  EXPECT_THAT(InspectTree(), ChildrenMatch(IsEmpty()));
}

TEST_F(InspectNodeManagerTest, Check_Remove_LevelTwoNode) {
  inspect_node_manager_->Get("/child1/grandchild1.1");

  EXPECT_THAT(InspectTree(),
              ChildrenMatch(ElementsAre(
                  AllOf(NodeMatches(NameMatches("child1")),
                        ChildrenMatch(ElementsAre(NodeMatches(NameMatches("grandchild1.1"))))))));

  EXPECT_TRUE(inspect_node_manager_->Remove("/child1/grandchild1.1"));
  EXPECT_THAT(InspectTree(), ChildrenMatch(ElementsAre(AllOf(NodeMatches(NameMatches("child1")),
                                                             ChildrenMatch(IsEmpty())))));
}

TEST_F(InspectNodeManagerTest, Attempt_Remove_NodesDoNotExist) {
  // Try to remove a node that doesn't exist, then create it.
  EXPECT_FALSE(inspect_node_manager_->Remove("/child1"));
  inspect_node_manager_->Get("/child1");

  // Try to remove a node that doesn't exist, then create it.
  EXPECT_FALSE(inspect_node_manager_->Remove("/child1/grandchild1.1"));
  inspect_node_manager_->Get("/child1/grandchild1.1");

  EXPECT_THAT(InspectTree(),
              ChildrenMatch(ElementsAre(
                  AllOf(NodeMatches(NameMatches("child1")),
                        ChildrenMatch(ElementsAre(NodeMatches(NameMatches("grandchild1.1"))))))));
}

TEST_F(InspectNodeManagerTest, Check_SanitizedPath) {
  constexpr char expected_sanitized_name[] = {
      'p',  'r',  'o',  'g', 'r',  'a',  'm',  0x07, 'n',  0x07, 0x07, 'a',
      0x07, 0x07, 0x07, 'm', 0x07, 0x07, 0x07, 0x07, 0x07, 'e',  '\0',
  };

  std::string name_with_backslashes = "program/n//a///m/////e";
  auto sanitized_name = InspectNodeManager::SanitizeString(name_with_backslashes);

  EXPECT_EQ(sanitized_name, expected_sanitized_name);

  const std::string full_path = "/child1/" + sanitized_name;
  inspect_node_manager_->Get(full_path);

  EXPECT_THAT(InspectTree(),
              ChildrenMatch(ElementsAre(AllOf(
                  NodeMatches(NameMatches("child1")),
                  ChildrenMatch(ElementsAre(NodeMatches(NameMatches(name_with_backslashes))))))));

  EXPECT_TRUE(inspect_node_manager_->Remove(full_path));
  EXPECT_THAT(InspectTree(), ChildrenMatch(ElementsAre(AllOf(NodeMatches(NameMatches("child1")),
                                                             ChildrenMatch(IsEmpty())))));
}

}  // namespace
}  // namespace feedback
