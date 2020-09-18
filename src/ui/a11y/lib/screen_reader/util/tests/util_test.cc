// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/screen_reader/util/util.h"

#include <fuchsia/accessibility/semantics/cpp/fidl.h>

#include <memory>

#include <gtest/gtest.h>

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

}  // namespace
}  // namespace accessibility_test
