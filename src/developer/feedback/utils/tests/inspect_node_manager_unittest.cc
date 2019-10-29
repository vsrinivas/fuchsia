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
using internal::SplitPathOrDie;
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

  void CheckSplitPathOrDie(const std::string& path, const std::vector<std::string>& expected) {
    std::vector<fxl::StringView> split_path = SplitPathOrDie(path);
    EXPECT_THAT(split_path, ElementsAreArray(expected));
  }
  std::unique_ptr<InspectNodeManager> inspect_node_manager_;

 private:
  std::unique_ptr<inspect::Inspector> inspector_;
};

TEST_F(InspectNodeManagerTest, Attempt_SplitPathOrDie_EmptyString) {
  ASSERT_DEATH(SplitPathOrDie(""), HasSubstr("path cannot be empty"));
}

TEST_F(InspectNodeManagerTest, Attempt_SplitPathOrDie_WrongFirstCharacter) {
  ASSERT_DEATH(SplitPathOrDie("a"), HasSubstr("path must start with '\'"));
}

TEST_F(InspectNodeManagerTest, Attempt_SplitPathOrDie_ContainsWhiteSpace) {
  ASSERT_DEATH(SplitPathOrDie("/path/ whitespace"), HasSubstr("path cannot contain whitespace"));
}

TEST_F(InspectNodeManagerTest, Check_SplitPathOrDie_JustRoot) { CheckSplitPathOrDie("/", {}); }

TEST_F(InspectNodeManagerTest, Check_SplitPathOrDie_AllBackslashes) {
  CheckSplitPathOrDie("/////////", {});
}

TEST_F(InspectNodeManagerTest, Check_SplitPathOrDie_NormalPath) {
  CheckSplitPathOrDie("/simple/path", {"simple", "path"});
}

TEST_F(InspectNodeManagerTest, Check_SplitPathOrDie_NormalPath_EndsWithBackslash) {
  CheckSplitPathOrDie("/simple/path/", {"simple", "path"});
}

TEST_F(InspectNodeManagerTest, Check_SplitPathOrDie_Path_StartsWithManyBackslashes) {
  CheckSplitPathOrDie("/////simple/path", {"simple", "path"});
}

TEST_F(InspectNodeManagerTest, Check_SplitPathOrDie_Path_EndsWithManyBackslashes) {
  CheckSplitPathOrDie("/simple/path//////", {"simple", "path"});
}

TEST_F(InspectNodeManagerTest, Check_SplitPathOrDie_Path_MiddleHasManyBackslashes) {
  CheckSplitPathOrDie("/simple///////path", {"simple", "path"});
}

TEST_F(InspectNodeManagerTest, Check_SplitPathOrDie_Path_HasBackslashesEverywhere) {
  CheckSplitPathOrDie("//////simple///////path//////////", {"simple", "path"});
}

TEST_F(InspectNodeManagerTest, Attempt_GetOrDie_BadPaths) {
  ASSERT_DEATH(inspect_node_manager_->GetOrDie(""), HasSubstr("path cannot be empty"));
  ASSERT_DEATH(inspect_node_manager_->GetOrDie("a"), HasSubstr("path must start with '\'"));
  ASSERT_DEATH(inspect_node_manager_->GetOrDie("/path "),
               HasSubstr("path cannot contain whitespace"));
  ASSERT_DEATH(inspect_node_manager_->GetOrDie("/path/white space"),
               HasSubstr("path cannot contain whitespace"));
}

TEST_F(InspectNodeManagerTest, Check_GetOrDie_RootNode) {
  EXPECT_TRUE(inspect_node_manager_->GetOrDie("/"));
}

TEST_F(InspectNodeManagerTest, Check_GetOrDie_MultipleLevelOneNodes) {
  EXPECT_TRUE(inspect_node_manager_->GetOrDie("/child1"));
  EXPECT_TRUE(inspect_node_manager_->GetOrDie("/child2"));
  EXPECT_TRUE(inspect_node_manager_->GetOrDie("/child3"));

  EXPECT_THAT(InspectTree(), ChildrenMatch(UnorderedElementsAreArray({
                                 NodeMatches(NameMatches("child1")),
                                 NodeMatches(NameMatches("child2")),
                                 NodeMatches(NameMatches("child3")),
                             })));
}

TEST_F(InspectNodeManagerTest, Check_GetOrDie_NodeAlreadyExists) {
  EXPECT_TRUE(inspect_node_manager_->GetOrDie("/child1"));
  EXPECT_TRUE(inspect_node_manager_->GetOrDie("/child1"));

  EXPECT_THAT(InspectTree(), ChildrenMatch(UnorderedElementsAreArray({
                                 NodeMatches(NameMatches("child1")),
                             })));
}

TEST_F(InspectNodeManagerTest, Check_GetOrDie_OneLevelTwoNode) {
  EXPECT_TRUE(inspect_node_manager_->GetOrDie("/child1/grandchild1.1"));

  EXPECT_THAT(InspectTree(),
              ChildrenMatch(ElementsAre(
                  AllOf(NodeMatches(NameMatches("child1")),
                        ChildrenMatch(ElementsAre(NodeMatches(NameMatches("grandchild1.1"))))))));
}

TEST_F(InspectNodeManagerTest, Check_GetOrDie_MultipleLevelTwoNodes) {
  EXPECT_TRUE(inspect_node_manager_->GetOrDie("/child1"));
  EXPECT_TRUE(inspect_node_manager_->GetOrDie("/child1/grandchild1.1"));
  EXPECT_TRUE(inspect_node_manager_->GetOrDie("/child1/grandchild1.2"));

  EXPECT_THAT(InspectTree(),
              ChildrenMatch(ElementsAre(AllOf(NodeMatches(NameMatches("child1")),
                                              ChildrenMatch(UnorderedElementsAreArray({
                                                  NodeMatches(NameMatches("grandchild1.1")),
                                                  NodeMatches(NameMatches("grandchild1.2")),
                                              }))))));
}

TEST_F(InspectNodeManagerTest, Check_GetOrDie_OneLevelThreeNode) {
  EXPECT_TRUE(inspect_node_manager_->GetOrDie("/child1/grandchild1.1/greatgrandchild1.1.1"));

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
  inspect::Node* node =
      inspect_node_manager_->GetOrDie("/child1/grandchild1.1/greatgrandchild1.1.1");
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

  node = inspect_node_manager_->GetOrDie("/child1/grandchild1.1/greatgrandchild1.1.1");
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
}  // namespace

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
