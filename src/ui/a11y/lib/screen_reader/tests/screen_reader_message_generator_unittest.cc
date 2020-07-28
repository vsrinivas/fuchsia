// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/screen_reader/screen_reader_message_generator.h"

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
using fuchsia::accessibility::semantics::States;
using fuchsia::intl::l10n::MessageIds;

class ScreenReaderMessageGeneratorTest : public gtest::RealLoopFixture {
 public:
  void SetUp() override {
    auto mock_message_formatter = std::make_unique<MockMessageFormatter>();
    mock_message_formatter_ptr_ = mock_message_formatter.get();
    screen_reader_message_generator_ =
        std::make_unique<a11y::ScreenReaderMessageGenerator>(std::move(mock_message_formatter));
  }

 protected:
  std::unique_ptr<a11y::ScreenReaderMessageGenerator> screen_reader_message_generator_;
  MockMessageFormatter* mock_message_formatter_ptr_;
};

TEST_F(ScreenReaderMessageGeneratorTest, BasicNode) {
  Node node;
  auto result = screen_reader_message_generator_->DescribeNode(&node);
  ASSERT_EQ(result.size(), 1u);
  EXPECT_FALSE(result[0].utterance.has_message());
}

TEST_F(ScreenReaderMessageGeneratorTest, NodeWithALabel) {
  Node node;
  node.mutable_attributes()->set_label("foo");
  auto result = screen_reader_message_generator_->DescribeNode(&node);
  ASSERT_EQ(result.size(), 1u);
  ASSERT_TRUE(result[0].utterance.has_message());
  ASSERT_EQ(result[0].utterance.message(), "foo");
}

TEST_F(ScreenReaderMessageGeneratorTest, NodeButton) {
  Node node;
  node.mutable_attributes()->set_label("foo");
  node.set_role(Role::BUTTON);
  mock_message_formatter_ptr_->SetMessageForId(static_cast<uint64_t>(MessageIds::ROLE_BUTTON),
                                               "button");
  auto result = screen_reader_message_generator_->DescribeNode(&node);
  ASSERT_EQ(result.size(), 2u);
  ASSERT_TRUE(result[0].utterance.has_message());
  ASSERT_EQ(result[0].utterance.message(), "foo");
  ASSERT_TRUE(result[1].utterance.has_message());
  ASSERT_EQ(result[1].utterance.message(), "button");
}

TEST_F(ScreenReaderMessageGeneratorTest, NodeHeader) {
  Node node;
  node.mutable_attributes()->set_label("foo");
  node.set_role(Role::HEADER);
  mock_message_formatter_ptr_->SetMessageForId(static_cast<uint64_t>(MessageIds::ROLE_HEADER),
                                               "header");
  auto result = screen_reader_message_generator_->DescribeNode(&node);
  ASSERT_EQ(result.size(), 2u);
  ASSERT_TRUE(result[0].utterance.has_message());
  ASSERT_EQ(result[0].utterance.message(), "foo");
  ASSERT_TRUE(result[1].utterance.has_message());
  ASSERT_EQ(result[1].utterance.message(), "header");
}

TEST_F(ScreenReaderMessageGeneratorTest, NodeImage) {
  Node node;
  node.mutable_attributes()->set_label("foo");
  node.set_role(Role::IMAGE);
  mock_message_formatter_ptr_->SetMessageForId(static_cast<uint64_t>(MessageIds::ROLE_IMAGE),
                                               "image");
  auto result = screen_reader_message_generator_->DescribeNode(&node);
  ASSERT_EQ(result.size(), 2u);
  ASSERT_TRUE(result[0].utterance.has_message());
  ASSERT_EQ(result[0].utterance.message(), "foo");
  ASSERT_TRUE(result[1].utterance.has_message());
  ASSERT_EQ(result[1].utterance.message(), "image");
}

TEST_F(ScreenReaderMessageGeneratorTest, NodeSlider) {
  Node node;
  node.mutable_attributes()->set_label("foo");
  node.set_role(Role::SLIDER);
  node.set_states(States());
  node.mutable_states()->set_range_value(10.0);
  mock_message_formatter_ptr_->SetMessageForId(static_cast<uint64_t>(MessageIds::ROLE_SLIDER),
                                               "slider");
  auto result = screen_reader_message_generator_->DescribeNode(&node);
  ASSERT_EQ(result.size(), 2u);
  ASSERT_TRUE(result[0].utterance.has_message());
  ASSERT_EQ(result[0].utterance.message(), "foo, 10");
  ASSERT_TRUE(result[1].utterance.has_message());
  ASSERT_EQ(result[1].utterance.message(), "slider");
}

}  // namespace
}  // namespace accessibility_test
