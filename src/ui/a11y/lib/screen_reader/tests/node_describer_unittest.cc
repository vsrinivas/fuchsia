// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/screen_reader/node_describer.h"

#include <fuchsia/accessibility/semantics/cpp/fidl.h>

#include <memory>

#include "src/lib/testing/loop_fixture/real_loop_fixture.h"
#include "src/ui/a11y/lib/screen_reader/i18n/tests/mocks/mock_message_formatter.h"

// This header file has been generated from the strings library fuchsia.intl.l10n.
#include "fuchsia/intl/l10n/cpp/fidl.h"

namespace accessibility_test {
namespace {

using fuchsia::accessibility::semantics::Node;
using fuchsia::accessibility::semantics::Role;
using fuchsia::intl::l10n::MessageIds;

class NodeDescriberTest : public gtest::RealLoopFixture {
 public:
  void SetUp() override {
    auto mock_message_formatter = std::make_unique<MockMessageFormatter>();
    mock_message_formatter_ptr_ = mock_message_formatter.get();
    node_describer_ = std::make_unique<a11y::NodeDescriber>(std::move(mock_message_formatter));
  }

 protected:
  std::unique_ptr<a11y::NodeDescriber> node_describer_;
  MockMessageFormatter* mock_message_formatter_ptr_;
};

TEST_F(NodeDescriberTest, BasicNode) {
  Node node;
  auto result = node_describer_->DescribeNode(&node);
  ASSERT_EQ(result.size(), 1u);
  EXPECT_FALSE(result[0].utterance.has_message());
}

TEST_F(NodeDescriberTest, NodeWithALabel) {
  Node node;
  node.mutable_attributes()->set_label("foo");
  auto result = node_describer_->DescribeNode(&node);
  ASSERT_EQ(result.size(), 1u);
  ASSERT_TRUE(result[0].utterance.has_message());
  ASSERT_EQ(result[0].utterance.message(), "foo");
}

TEST_F(NodeDescriberTest, NodeButton) {
  Node node;
  node.mutable_attributes()->set_label("foo");
  node.set_role(Role::BUTTON);
  mock_message_formatter_ptr_->SetMessageForId(static_cast<uint64_t>(MessageIds::ROLE_BUTTON),
                                               "button");
  auto result = node_describer_->DescribeNode(&node);
  ASSERT_EQ(result.size(), 2u);
  ASSERT_TRUE(result[0].utterance.has_message());
  ASSERT_EQ(result[0].utterance.message(), "foo");
  ASSERT_TRUE(result[1].utterance.has_message());
  ASSERT_EQ(result[1].utterance.message(), "button");
}

}  // namespace
}  // namespace accessibility_test
