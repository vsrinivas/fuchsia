// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/screen_reader/util/util.h"

#include <fuchsia/accessibility/semantics/cpp/fidl.h>

#include <memory>

#include <gtest/gtest.h>

#include "src/ui/a11y/lib/semantics/tests/mocks/mock_semantics_source.h"

namespace accessibility_test {
namespace {

TEST(ScreenReaderUtilTest, NodeIsDescribableNullNode) {
  EXPECT_FALSE(a11y::NodeIsDescribable(nullptr));
}

TEST(ScreenReaderUtilTest, NodeIsDescribableButton) {
  fuchsia::accessibility::semantics::Node node;
  node.set_node_id(0u);
  node.set_role(fuchsia::accessibility::semantics::Role::BUTTON);
  EXPECT_TRUE(a11y::NodeIsDescribable(&node));
}

TEST(ScreenReaderUtilTest, NodeIsDescribableLabelled) {
  fuchsia::accessibility::semantics::Node node;
  node.set_node_id(0u);
  fuchsia::accessibility::semantics::Attributes attributes;
  attributes.set_label("label");
  node.set_attributes(std::move(attributes));
  EXPECT_TRUE(a11y::NodeIsDescribable(&node));
}

TEST(ScreenReaderUtilTest, NodeIsDescribableEmptyLabel) {
  fuchsia::accessibility::semantics::Node node;
  node.set_node_id(0u);
  fuchsia::accessibility::semantics::Attributes attributes;
  attributes.set_label("");
  node.set_attributes(std::move(attributes));
  EXPECT_FALSE(a11y::NodeIsDescribable(&node));
}

TEST(ScreenReaderUtilTest, NodeIsDescribableNoLabel) {
  fuchsia::accessibility::semantics::Node node;
  node.set_node_id(0u);
  EXPECT_FALSE(a11y::NodeIsDescribable(&node));
}

TEST(ScreenReaderUtilTest, HiddenNodesAreNotDescribable) {
  fuchsia::accessibility::semantics::Node node;
  node.set_node_id(0u);
  node.set_role(fuchsia::accessibility::semantics::Role::BUTTON);
  node.mutable_states()->set_hidden(true);
  EXPECT_FALSE(a11y::NodeIsDescribable(&node));
}

TEST(ScreenReaderUtilTest, FormatFloatInteger) {
  auto result = a11y::FormatFloat(1.0f);
  EXPECT_EQ(result, "1");
}

TEST(ScreenReaderUtilTest, FormatFloatDecimal) {
  auto result = a11y::FormatFloat(1.01f);
  EXPECT_EQ(result, "1.01");
}

TEST(ScreenReaderUtilTest, FormatFloatZero) {
  auto result = a11y::FormatFloat(0.0f);
  EXPECT_EQ(result, "0");
}

TEST(ScreenReaderUtilTest, GetNodesToExcludeLinearMotifPresent) {
  MockSemanticsSource mock_semantics_source;
  const zx_koid_t koid = 0;

  {
    fuchsia::accessibility::semantics::Node node;
    node.set_node_id(0u);
    *(node.mutable_child_ids()) = {1u, 2u};
    mock_semantics_source.CreateSemanticNode(koid, std::move(node));
  }

  {
    fuchsia::accessibility::semantics::Node node;
    node.set_node_id(1u);
    mock_semantics_source.CreateSemanticNode(koid, std::move(node));
  }

  {
    fuchsia::accessibility::semantics::Node node;
    node.set_node_id(2u);
    *(node.mutable_child_ids()) = {3u};
    fuchsia::accessibility::semantics::Attributes attributes;
    attributes.set_label("label");
    node.set_attributes(std::move(attributes));
    mock_semantics_source.CreateSemanticNode(koid, std::move(node));
  }

  {
    fuchsia::accessibility::semantics::Node node;
    node.set_node_id(3u);
    *(node.mutable_child_ids()) = {4u};
    fuchsia::accessibility::semantics::Attributes attributes;
    attributes.set_label("label");
    node.set_attributes(std::move(attributes));
    mock_semantics_source.CreateSemanticNode(koid, std::move(node));
  }

  {
    fuchsia::accessibility::semantics::Node node;
    node.set_node_id(4u);
    fuchsia::accessibility::semantics::Attributes attributes;
    attributes.set_label("label");
    node.set_attributes(std::move(attributes));
    mock_semantics_source.CreateSemanticNode(koid, std::move(node));
  }

  // Nodes 2, 3, and 4 are all a part of the same linear motif, so calling
  // GetNodesToExclude() on any of the three nodes should return a set
  // containing the other two nodes.
  {
    auto nodes_to_exclude = GetNodesToExclude(koid, 2u, &mock_semantics_source);
    EXPECT_EQ(nodes_to_exclude.size(), 2u);
    EXPECT_NE(nodes_to_exclude.find(3u), nodes_to_exclude.end());
    EXPECT_NE(nodes_to_exclude.find(4u), nodes_to_exclude.end());
  }

  {
    auto nodes_to_exclude = GetNodesToExclude(koid, 3u, &mock_semantics_source);
    EXPECT_EQ(nodes_to_exclude.size(), 2u);
    EXPECT_NE(nodes_to_exclude.find(2u), nodes_to_exclude.end());
    EXPECT_NE(nodes_to_exclude.find(4u), nodes_to_exclude.end());
  }

  {
    auto nodes_to_exclude = GetNodesToExclude(koid, 4u, &mock_semantics_source);
    EXPECT_EQ(nodes_to_exclude.size(), 2u);
    EXPECT_NE(nodes_to_exclude.find(2u), nodes_to_exclude.end());
    EXPECT_NE(nodes_to_exclude.find(3u), nodes_to_exclude.end());
  }
}

TEST(ScreenReaderUtilTest, GetNodesToExcludeDifferentLabels) {
  MockSemanticsSource mock_semantics_source;
  const zx_koid_t koid = 0;

  {
    fuchsia::accessibility::semantics::Node node;
    node.set_node_id(0u);
    *(node.mutable_child_ids()) = {1u, 2u};
    mock_semantics_source.CreateSemanticNode(koid, std::move(node));
  }

  {
    fuchsia::accessibility::semantics::Node node;
    node.set_node_id(1u);
    mock_semantics_source.CreateSemanticNode(koid, std::move(node));
  }

  {
    fuchsia::accessibility::semantics::Node node;
    node.set_node_id(2u);
    *(node.mutable_child_ids()) = {3u};
    fuchsia::accessibility::semantics::Attributes attributes;
    attributes.set_label("label");
    node.set_attributes(std::move(attributes));
    mock_semantics_source.CreateSemanticNode(koid, std::move(node));
  }

  {
    fuchsia::accessibility::semantics::Node node;
    node.set_node_id(3u);
    *(node.mutable_child_ids()) = {4u};
    fuchsia::accessibility::semantics::Attributes attributes;
    attributes.set_label("label");
    node.set_attributes(std::move(attributes));
    mock_semantics_source.CreateSemanticNode(koid, std::move(node));
  }

  {
    fuchsia::accessibility::semantics::Node node;
    node.set_node_id(4u);
    fuchsia::accessibility::semantics::Attributes attributes;
    attributes.set_label("different label");
    node.set_attributes(std::move(attributes));
    mock_semantics_source.CreateSemanticNode(koid, std::move(node));
  }

  auto nodes_to_exclude = GetNodesToExclude(koid, 2u, &mock_semantics_source);
  EXPECT_TRUE(nodes_to_exclude.empty());
}

TEST(ScreenReaderUtilTest, GetNodesToExcludeBranchedSubtree) {
  MockSemanticsSource mock_semantics_source;
  const zx_koid_t koid = 0;

  {
    fuchsia::accessibility::semantics::Node node;
    node.set_node_id(0u);
    *(node.mutable_child_ids()) = {1u, 2u};
    mock_semantics_source.CreateSemanticNode(koid, std::move(node));
  }

  {
    fuchsia::accessibility::semantics::Node node;
    node.set_node_id(1u);
    mock_semantics_source.CreateSemanticNode(koid, std::move(node));
  }

  {
    fuchsia::accessibility::semantics::Node node;
    node.set_node_id(2u);
    *(node.mutable_child_ids()) = {3u, 4u};
    fuchsia::accessibility::semantics::Attributes attributes;
    attributes.set_label("label");
    node.set_attributes(std::move(attributes));
    mock_semantics_source.CreateSemanticNode(koid, std::move(node));
  }

  {
    fuchsia::accessibility::semantics::Node node;
    node.set_node_id(3u);
    *(node.mutable_child_ids()) = {};
    fuchsia::accessibility::semantics::Attributes attributes;
    attributes.set_label("label");
    node.set_attributes(std::move(attributes));
    mock_semantics_source.CreateSemanticNode(koid, std::move(node));
  }

  {
    fuchsia::accessibility::semantics::Node node;
    node.set_node_id(4u);
    fuchsia::accessibility::semantics::Attributes attributes;
    attributes.set_label("label");
    node.set_attributes(std::move(attributes));
    mock_semantics_source.CreateSemanticNode(koid, std::move(node));
  }

  auto nodes_to_exclude = GetNodesToExclude(koid, 2u, &mock_semantics_source);
  EXPECT_TRUE(nodes_to_exclude.empty());
}

TEST(ScreenReaderUtilTest, GetNodesToExcludeInternalLinearMotif) {
  MockSemanticsSource mock_semantics_source;
  const zx_koid_t koid = 0;

  {
    fuchsia::accessibility::semantics::Node node;
    node.set_node_id(0u);
    *(node.mutable_child_ids()) = {1u, 2u};
    mock_semantics_source.CreateSemanticNode(koid, std::move(node));
  }

  {
    fuchsia::accessibility::semantics::Node node;
    node.set_node_id(1u);
    mock_semantics_source.CreateSemanticNode(koid, std::move(node));
  }

  {
    fuchsia::accessibility::semantics::Node node;
    node.set_node_id(2u);
    *(node.mutable_child_ids()) = {3u};
    fuchsia::accessibility::semantics::Attributes attributes;
    attributes.set_label("label");
    node.set_attributes(std::move(attributes));
    mock_semantics_source.CreateSemanticNode(koid, std::move(node));
  }

  {
    fuchsia::accessibility::semantics::Node node;
    node.set_node_id(3u);
    *(node.mutable_child_ids()) = {4u};
    fuchsia::accessibility::semantics::Attributes attributes;
    attributes.set_label("label");
    node.set_attributes(std::move(attributes));
    mock_semantics_source.CreateSemanticNode(koid, std::move(node));
  }

  {
    fuchsia::accessibility::semantics::Node node;
    node.set_node_id(4u);
    fuchsia::accessibility::semantics::Attributes attributes;
    attributes.set_label("different label");
    node.set_attributes(std::move(attributes));
    mock_semantics_source.CreateSemanticNode(koid, std::move(node));
  }

  {
    auto nodes_to_exclude = GetNodesToExclude(koid, 2u, &mock_semantics_source);
    EXPECT_TRUE(nodes_to_exclude.empty());
  }

  {
    auto nodes_to_exclude = GetNodesToExclude(koid, 3u, &mock_semantics_source);
    EXPECT_TRUE(nodes_to_exclude.empty());
  }
}

TEST(ScreenReaderUtilTest, GetNodesToExcludeDifferentActions) {
  MockSemanticsSource mock_semantics_source;
  const zx_koid_t koid = 0;

  {
    fuchsia::accessibility::semantics::Node node;
    node.set_node_id(0u);
    *(node.mutable_child_ids()) = {1u, 2u};
    mock_semantics_source.CreateSemanticNode(koid, std::move(node));
  }

  {
    fuchsia::accessibility::semantics::Node node;
    node.set_node_id(1u);
    mock_semantics_source.CreateSemanticNode(koid, std::move(node));
  }

  {
    fuchsia::accessibility::semantics::Node node;
    node.set_node_id(2u);
    *(node.mutable_child_ids()) = {3u};
    *(node.mutable_actions()) = {fuchsia::accessibility::semantics::Action::SECONDARY};
    fuchsia::accessibility::semantics::Attributes attributes;
    attributes.set_label("label");
    node.set_attributes(std::move(attributes));
    mock_semantics_source.CreateSemanticNode(koid, std::move(node));
  }

  {
    fuchsia::accessibility::semantics::Node node;
    node.set_node_id(3u);
    *(node.mutable_child_ids()) = {4u};
    *(node.mutable_actions()) = {fuchsia::accessibility::semantics::Action::DEFAULT,
                                 fuchsia::accessibility::semantics::Action::SET_VALUE};
    fuchsia::accessibility::semantics::Attributes attributes;
    attributes.set_label("label");
    node.set_attributes(std::move(attributes));
    mock_semantics_source.CreateSemanticNode(koid, std::move(node));
  }

  {
    fuchsia::accessibility::semantics::Node node;
    node.set_node_id(4u);
    *(node.mutable_actions()) = {fuchsia::accessibility::semantics::Action::DEFAULT};
    fuchsia::accessibility::semantics::Attributes attributes;
    attributes.set_label("label");
    node.set_attributes(std::move(attributes));
    mock_semantics_source.CreateSemanticNode(koid, std::move(node));
  }

  {
    auto nodes_to_exclude = GetNodesToExclude(koid, 2u, &mock_semantics_source);
    EXPECT_TRUE(nodes_to_exclude.empty());
  }

  {
    auto nodes_to_exclude = GetNodesToExclude(koid, 3u, &mock_semantics_source);
    EXPECT_EQ(nodes_to_exclude.size(), 1u);
    EXPECT_NE(nodes_to_exclude.find(4u), nodes_to_exclude.end());
  }

  {
    auto nodes_to_exclude = GetNodesToExclude(koid, 4u, &mock_semantics_source);
    EXPECT_TRUE(nodes_to_exclude.empty());
  }
}

}  // namespace
}  // namespace accessibility_test
