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

TEST(ScreenReaderUtilTest, NodeIsDescribableToggleSwitch) {
  fuchsia::accessibility::semantics::Node node;
  node.set_node_id(0u);
  node.set_role(fuchsia::accessibility::semantics::Role::TOGGLE_SWITCH);
  EXPECT_TRUE(a11y::NodeIsDescribable(&node));
}
TEST(ScreenReaderUtilTest, NodeIsDescribableRadioButton) {
  fuchsia::accessibility::semantics::Node node;
  node.set_node_id(0u);
  node.set_role(fuchsia::accessibility::semantics::Role::RADIO_BUTTON);
  EXPECT_TRUE(a11y::NodeIsDescribable(&node));
}

TEST(ScreenReaderUtilTest, NodeIsDescribableCheckBox) {
  fuchsia::accessibility::semantics::Node node;
  node.set_node_id(0u);
  node.set_role(fuchsia::accessibility::semantics::Role::CHECK_BOX);
  EXPECT_TRUE(a11y::NodeIsDescribable(&node));
}

TEST(ScreenReaderUtilTest, NodeIsDescribableSlider) {
  fuchsia::accessibility::semantics::Node node;
  node.set_node_id(0u);
  node.set_role(fuchsia::accessibility::semantics::Role::SLIDER);
  EXPECT_TRUE(a11y::NodeIsDescribable(&node));
}

TEST(ScreenReaderUtilTest, NodeIsDescribableLink) {
  fuchsia::accessibility::semantics::Node node;
  node.set_node_id(0u);
  node.set_role(fuchsia::accessibility::semantics::Role::LINK);
  EXPECT_TRUE(a11y::NodeIsDescribable(&node));
}

TEST(ScreenReaderUtilTest, NodeIsDescribableImage) {
  fuchsia::accessibility::semantics::Node node;
  node.set_node_id(0u);
  node.set_role(fuchsia::accessibility::semantics::Role::IMAGE);
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

TEST(ScreenReaderUtilTest, HiddenNodesAreNotDescribableButton) {
  fuchsia::accessibility::semantics::Node node;
  node.set_node_id(0u);
  node.set_role(fuchsia::accessibility::semantics::Role::BUTTON);
  node.mutable_states()->set_hidden(true);
  EXPECT_FALSE(a11y::NodeIsDescribable(&node));
}

TEST(ScreenReaderUtilTest, HiddenNodesAreNotDescribableToggleSwitch) {
  fuchsia::accessibility::semantics::Node node;
  node.set_node_id(0u);
  node.set_role(fuchsia::accessibility::semantics::Role::TOGGLE_SWITCH);
  node.mutable_states()->set_hidden(true);
  EXPECT_FALSE(a11y::NodeIsDescribable(&node));
}

TEST(ScreenReaderUtilTest, HiddenNodesAreNotDescribableRadioButton) {
  fuchsia::accessibility::semantics::Node node;
  node.set_node_id(0u);
  node.set_role(fuchsia::accessibility::semantics::Role::RADIO_BUTTON);
  node.mutable_states()->set_hidden(true);
  EXPECT_FALSE(a11y::NodeIsDescribable(&node));
}

TEST(ScreenReaderUtilTest, HiddenNodesAreNotDescribableLink) {
  fuchsia::accessibility::semantics::Node node;
  node.set_node_id(0u);
  node.set_role(fuchsia::accessibility::semantics::Role::LINK);
  node.mutable_states()->set_hidden(true);
  EXPECT_FALSE(a11y::NodeIsDescribable(&node));
}

TEST(ScreenReaderUtilTest, HiddenNodesAreNotDescribableCheckBox) {
  fuchsia::accessibility::semantics::Node node;
  node.set_node_id(0u);
  node.set_role(fuchsia::accessibility::semantics::Role::CHECK_BOX);
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

TEST(ScreenReaderUtilTest, SameInformationAsParentLinearMotifPresent) {
  const fuchsia::accessibility::semantics::Node root_node = []() {
    fuchsia::accessibility::semantics::Node node;
    node.set_node_id(0u);
    *(node.mutable_child_ids()) = {1u, 2u};
    return node;
  }();

  const fuchsia::accessibility::semantics::Node node_1 = []() {
    fuchsia::accessibility::semantics::Node node;
    node.set_node_id(1u);
    return node;
  }();

  const fuchsia::accessibility::semantics::Node node_2 = []() {
    fuchsia::accessibility::semantics::Node node;
    node.set_node_id(2u);
    *(node.mutable_child_ids()) = {3u};
    fuchsia::accessibility::semantics::Attributes attributes;
    attributes.set_label("label");
    node.set_attributes(std::move(attributes));
    return node;
  }();

  const fuchsia::accessibility::semantics::Node node_3 = []() {
    fuchsia::accessibility::semantics::Node node;
    node.set_node_id(3u);
    *(node.mutable_child_ids()) = {4u};
    fuchsia::accessibility::semantics::Attributes attributes;
    attributes.set_label("label");
    node.set_attributes(std::move(attributes));
    return node;
  }();

  const fuchsia::accessibility::semantics::Node node_4 = []() {
    fuchsia::accessibility::semantics::Node node;
    node.set_node_id(4u);
    fuchsia::accessibility::semantics::Attributes attributes;
    attributes.set_label("label");
    node.set_attributes(std::move(attributes));
    return node;
  }();

  // Nodes 2, 3, and 4 are all a part of the same linear motif, so calling
  // SameInformationAsParent() on nodes 3 and 4 should return true.
  EXPECT_FALSE(a11y::SameInformationAsParent(&node_2, &node_1));
  EXPECT_TRUE(a11y::SameInformationAsParent(&node_3, &node_2));
  EXPECT_TRUE(a11y::SameInformationAsParent(&node_4, &node_3));
}

TEST(ScreenReaderUtilTest, SameInformationAsParentDifferentLabels) {
  const fuchsia::accessibility::semantics::Node node_1 = []() {
    fuchsia::accessibility::semantics::Node node;
    node.set_node_id(1u);
    *(node.mutable_child_ids()) = {2u};
    fuchsia::accessibility::semantics::Attributes attributes;
    attributes.set_label("label");
    node.set_attributes(std::move(attributes));
    return node;
  }();

  const fuchsia::accessibility::semantics::Node node_2 = []() {
    fuchsia::accessibility::semantics::Node node;
    node.set_node_id(2u);
    fuchsia::accessibility::semantics::Attributes attributes;
    attributes.set_label("different label");
    node.set_attributes(std::move(attributes));
    return node;
  }();

  EXPECT_FALSE(a11y::SameInformationAsParent(&node_2, &node_1));
}

TEST(ScreenReaderUtilTest, SameInformationAsParentMultipleChildren) {
  const fuchsia::accessibility::semantics::Node node_1 = []() {
    fuchsia::accessibility::semantics::Node node;
    node.set_node_id(1u);
    *(node.mutable_child_ids()) = {2u, 3u};
    fuchsia::accessibility::semantics::Attributes attributes;
    attributes.set_label("label");
    node.set_attributes(std::move(attributes));
    return node;
  }();

  const fuchsia::accessibility::semantics::Node node_2 = []() {
    fuchsia::accessibility::semantics::Node node;
    node.set_node_id(2u);
    *(node.mutable_child_ids()) = {};
    fuchsia::accessibility::semantics::Attributes attributes;
    attributes.set_label("label");
    node.set_attributes(std::move(attributes));
    return node;
  }();

  const fuchsia::accessibility::semantics::Node node_3 = [&node_2]() {
    fuchsia::accessibility::semantics::Node node;
    node_2.Clone(&node);
    node.set_node_id(3u);
    return node;
  }();

  EXPECT_FALSE(a11y::SameInformationAsParent(&node_2, &node_1));
  EXPECT_FALSE(a11y::SameInformationAsParent(&node_3, &node_1));
}

TEST(ScreenReaderUtilTest, SameInformationAsParentDifferentActions) {
  const fuchsia::accessibility::semantics::Node node_1 = []() {
    fuchsia::accessibility::semantics::Node node;
    node.set_node_id(1u);
    *(node.mutable_child_ids()) = {2u};
    *(node.mutable_actions()) = {fuchsia::accessibility::semantics::Action::SECONDARY};
    fuchsia::accessibility::semantics::Attributes attributes;
    attributes.set_label("label");
    node.set_attributes(std::move(attributes));
    return node;
  }();

  const fuchsia::accessibility::semantics::Node node_2 = []() {
    fuchsia::accessibility::semantics::Node node;
    node.set_node_id(2u);
    *(node.mutable_child_ids()) = {3u};
    *(node.mutable_actions()) = {fuchsia::accessibility::semantics::Action::DEFAULT,
                                 fuchsia::accessibility::semantics::Action::SET_VALUE};
    fuchsia::accessibility::semantics::Attributes attributes;
    attributes.set_label("label");
    node.set_attributes(std::move(attributes));
    return node;
  }();

  const fuchsia::accessibility::semantics::Node node_3 = []() {
    fuchsia::accessibility::semantics::Node node;
    node.set_node_id(3u);
    *(node.mutable_actions()) = {fuchsia::accessibility::semantics::Action::DEFAULT};
    fuchsia::accessibility::semantics::Attributes attributes;
    attributes.set_label("label");
    node.set_attributes(std::move(attributes));
    return node;
  }();

  EXPECT_FALSE(a11y::SameInformationAsParent(&node_2, &node_1));
  EXPECT_TRUE(a11y::SameInformationAsParent(&node_3, &node_2));
}

TEST(ScreenReaderUtilTest, GetSliderValueRangeValueOnly) {
  fuchsia::accessibility::semantics::Node node;
  fuchsia::accessibility::semantics::States states;
  states.set_range_value(50.f);
  node.set_states(std::move(states));

  EXPECT_EQ(a11y::GetSliderValue(node), "50");
}

TEST(ScreenReaderUtilTest, GetSliderValueValueOnly) {
  fuchsia::accessibility::semantics::Node node;
  fuchsia::accessibility::semantics::States states;
  const std::string value = "50%";
  states.set_value(value);
  node.set_states(std::move(states));

  EXPECT_EQ(a11y::GetSliderValue(node), value);
}

TEST(ScreenReaderUtilTest, GetSliderValueBothValueAndRangeValue) {
  fuchsia::accessibility::semantics::Node node;
  fuchsia::accessibility::semantics::States states;
  states.set_range_value(50.f);
  states.set_value("should be ignored");
  node.set_states(std::move(states));

  EXPECT_EQ(a11y::GetSliderValue(node), "50");
}

TEST(ScreenReaderUtilTest, GetContainerNodesContainerIsTable) {
  MockSemanticsSource mock_semantics_source;
  const zx_koid_t koid = 0;

  {
    fuchsia::accessibility::semantics::Node node;
    node.set_node_id(0u);
    *(node.mutable_child_ids()) = {1u};
    node.set_role(fuchsia::accessibility::semantics::Role::TABLE);
    mock_semantics_source.CreateSemanticNode(koid, std::move(node));
  }

  {
    fuchsia::accessibility::semantics::Node node;
    node.set_node_id(1u);
    *(node.mutable_child_ids()) = {2u};
    mock_semantics_source.CreateSemanticNode(koid, std::move(node));
  }

  {
    fuchsia::accessibility::semantics::Node node;
    node.set_node_id(2u);
    mock_semantics_source.CreateSemanticNode(koid, std::move(node));
  }

  auto container_nodes = GetContainerNodes(koid, 2u, &mock_semantics_source);
  ASSERT_EQ(container_nodes.size(), 1u);
  EXPECT_EQ(container_nodes[0]->node_id(), 0u);
}

TEST(ScreenReaderUtilTest, GetContainerNodesNestedContainers) {
  MockSemanticsSource mock_semantics_source;
  const zx_koid_t koid = 0;

  {
    fuchsia::accessibility::semantics::Node node;
    node.set_node_id(0u);
    *(node.mutable_child_ids()) = {1u};
    node.set_role(fuchsia::accessibility::semantics::Role::TABLE);
    mock_semantics_source.CreateSemanticNode(koid, std::move(node));
  }

  {
    fuchsia::accessibility::semantics::Node node;
    node.set_node_id(1u);
    *(node.mutable_child_ids()) = {2u};
    node.set_role(fuchsia::accessibility::semantics::Role::LIST);
    mock_semantics_source.CreateSemanticNode(koid, std::move(node));
  }

  {
    fuchsia::accessibility::semantics::Node node;
    node.set_node_id(2u);
    *(node.mutable_child_ids()) = {3u};
    mock_semantics_source.CreateSemanticNode(koid, std::move(node));
  }

  auto container_nodes = GetContainerNodes(koid, 3u, &mock_semantics_source);
  ASSERT_EQ(container_nodes.size(), 2u);
  EXPECT_EQ(container_nodes[0]->node_id(), 0u);
  EXPECT_EQ(container_nodes[1]->node_id(), 1u);
}

TEST(ScreenReaderUtilTest, GetContainerNodesNoContainers) {
  MockSemanticsSource mock_semantics_source;
  const zx_koid_t koid = 0;

  {
    fuchsia::accessibility::semantics::Node node;
    node.set_node_id(0u);
    *(node.mutable_child_ids()) = {1u};
    mock_semantics_source.CreateSemanticNode(koid, std::move(node));
  }

  {
    fuchsia::accessibility::semantics::Node node;
    node.set_node_id(1u);
    *(node.mutable_child_ids()) = {2u};
    mock_semantics_source.CreateSemanticNode(koid, std::move(node));
  }

  {
    fuchsia::accessibility::semantics::Node node;
    node.set_node_id(2u);
    node.set_role(fuchsia::accessibility::semantics::Role::TABLE);
    mock_semantics_source.CreateSemanticNode(koid, std::move(node));
  }

  auto container_node = GetContainerNodes(koid, 1u, &mock_semantics_source);
  ASSERT_TRUE(container_node.empty());
}

TEST(ScreenReaderUtilTest, GetContainerNodesTargetNodeIsItselfAContainer) {
  MockSemanticsSource mock_semantics_source;
  const zx_koid_t koid = 0;

  {
    fuchsia::accessibility::semantics::Node node;
    node.set_node_id(0u);
    *(node.mutable_child_ids()) = {1u};
    mock_semantics_source.CreateSemanticNode(koid, std::move(node));
  }

  {
    fuchsia::accessibility::semantics::Node node;
    node.set_node_id(1u);
    node.set_role(fuchsia::accessibility::semantics::Role::TABLE);
    mock_semantics_source.CreateSemanticNode(koid, std::move(node));
  }

  auto container_node = GetContainerNodes(koid, 1u, &mock_semantics_source);
  ASSERT_TRUE(container_node.empty());
}

}  // namespace
}  // namespace accessibility_test
