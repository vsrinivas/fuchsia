// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/screen_reader/util/util.h"

#include <gtest/gtest.h>
#include <memory>

#include <fuchsia/accessibility/semantics/cpp/fidl.h>

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

}  // namespace
}  // namespace accessibility_test
