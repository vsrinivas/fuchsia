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
  ASSERT_EQ(result.size(), 0u);
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

TEST_F(ScreenReaderMessageGeneratorTest, NodeButtonNoLabel) {
  Node node;
  node.set_role(Role::BUTTON);
  mock_message_formatter_ptr_->SetMessageForId(static_cast<uint64_t>(MessageIds::ROLE_BUTTON),
                                               "button");
  auto result = screen_reader_message_generator_->DescribeNode(&node);
  ASSERT_EQ(result.size(), 1u);
  ASSERT_TRUE(result[0].utterance.has_message());
  ASSERT_EQ(result[0].utterance.message(), "button");
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

TEST_F(ScreenReaderMessageGeneratorTest, NodeSliderWithRangeValue) {
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

TEST_F(ScreenReaderMessageGeneratorTest, NodeSliderWithValue) {
  Node node;
  node.mutable_attributes()->set_label("foo");
  node.set_role(Role::SLIDER);
  node.set_states(States());
  node.mutable_states()->set_value("10%");
  mock_message_formatter_ptr_->SetMessageForId(static_cast<uint64_t>(MessageIds::ROLE_SLIDER),
                                               "slider");
  auto result = screen_reader_message_generator_->DescribeNode(&node);
  ASSERT_EQ(result.size(), 2u);
  ASSERT_TRUE(result[0].utterance.has_message());
  ASSERT_EQ(result[0].utterance.message(), "foo, 10%");
  ASSERT_TRUE(result[1].utterance.has_message());
  ASSERT_EQ(result[1].utterance.message(), "slider");
}

TEST_F(ScreenReaderMessageGeneratorTest, NodeSliderNoRangeValue) {
  Node node;
  node.mutable_attributes()->set_label("foo");
  node.set_role(Role::SLIDER);
  node.set_states(States());
  mock_message_formatter_ptr_->SetMessageForId(static_cast<uint64_t>(MessageIds::ROLE_SLIDER),
                                               "slider");
  auto result = screen_reader_message_generator_->DescribeNode(&node);
  ASSERT_EQ(result.size(), 2u);
  ASSERT_TRUE(result[0].utterance.has_message());
  ASSERT_EQ(result[0].utterance.message(), "foo");
  ASSERT_TRUE(result[1].utterance.has_message());
  ASSERT_EQ(result[1].utterance.message(), "slider");
}

TEST_F(ScreenReaderMessageGeneratorTest, NodeRangeValueNoSliderRole) {
  Node node;
  node.mutable_attributes()->set_label("foo");
  node.set_states(States());
  node.mutable_states()->set_range_value(10.0);
  mock_message_formatter_ptr_->SetMessageForId(static_cast<uint64_t>(MessageIds::ROLE_SLIDER),
                                               "slider");
  auto result = screen_reader_message_generator_->DescribeNode(&node);
  ASSERT_EQ(result.size(), 1u);
  ASSERT_TRUE(result[0].utterance.has_message());
  ASSERT_EQ(result[0].utterance.message(), "foo, 10");
}

TEST_F(ScreenReaderMessageGeneratorTest, NodeSliderNoLabel) {
  Node node;
  node.set_role(Role::SLIDER);
  node.set_states(States());
  node.mutable_states()->set_range_value(10.0);
  mock_message_formatter_ptr_->SetMessageForId(static_cast<uint64_t>(MessageIds::ROLE_SLIDER),
                                               "slider");
  auto result = screen_reader_message_generator_->DescribeNode(&node);
  ASSERT_EQ(result.size(), 2u);
  ASSERT_TRUE(result[0].utterance.has_message());
  ASSERT_EQ(result[0].utterance.message(), ", 10");
  ASSERT_TRUE(result[1].utterance.has_message());
  ASSERT_EQ(result[1].utterance.message(), "slider");
}

TEST_F(ScreenReaderMessageGeneratorTest, GenerateByMessageId) {
  mock_message_formatter_ptr_->SetMessageForId(static_cast<uint64_t>(MessageIds::ROLE_SLIDER),
                                               "slider");
  auto result =
      screen_reader_message_generator_->GenerateUtteranceByMessageId(MessageIds::ROLE_SLIDER);
  ASSERT_TRUE(result.utterance.has_message());
  ASSERT_EQ(result.utterance.message(), "slider");
}

TEST_F(ScreenReaderMessageGeneratorTest, ClickableNode) {
  Node node;
  node.mutable_attributes()->set_label("foo");
  node.mutable_actions()->push_back(fuchsia::accessibility::semantics::Action::DEFAULT);
  node.set_role(Role::BUTTON);
  mock_message_formatter_ptr_->SetMessageForId(static_cast<uint64_t>(MessageIds::ROLE_BUTTON),
                                               "button");
  mock_message_formatter_ptr_->SetMessageForId(static_cast<uint64_t>(MessageIds::DOUBLE_TAP_HINT),
                                               "double tap to activate");

  auto result = screen_reader_message_generator_->DescribeNode(&node);
  ASSERT_EQ(result.size(), 3u);
  ASSERT_TRUE(result[0].utterance.has_message());
  ASSERT_EQ(result[0].utterance.message(), "foo");
  ASSERT_TRUE(result[1].utterance.has_message());
  ASSERT_EQ(result[1].utterance.message(), "button");
  ASSERT_TRUE(result[2].utterance.has_message());
  ASSERT_EQ(result[2].utterance.message(), "double tap to activate");
}

TEST_F(ScreenReaderMessageGeneratorTest, NodeRadioButtonSelected) {
  Node node;
  node.mutable_attributes()->set_label("foo");
  node.set_role(Role::RADIO_BUTTON);
  node.mutable_states()->set_selected(true);
  mock_message_formatter_ptr_->SetMessageForId(
      static_cast<uint64_t>(MessageIds::RADIO_BUTTON_SELECTED), "foo radio button selected");
  auto result = screen_reader_message_generator_->DescribeNode(&node);
  ASSERT_EQ(result.size(), 1u);
  ASSERT_TRUE(result[0].utterance.has_message());
  ASSERT_EQ(result[0].utterance.message(), "foo radio button selected");
}

TEST_F(ScreenReaderMessageGeneratorTest, NodeRadioButtonUnselected) {
  Node node;
  node.mutable_attributes()->set_label("foo");
  node.set_role(Role::RADIO_BUTTON);
  node.mutable_states()->set_selected(false);
  mock_message_formatter_ptr_->SetMessageForId(
      static_cast<uint64_t>(MessageIds::RADIO_BUTTON_UNSELECTED), "foo radio button unselected");
  auto result = screen_reader_message_generator_->DescribeNode(&node);
  ASSERT_EQ(result.size(), 1u);
  ASSERT_TRUE(result[0].utterance.has_message());
  ASSERT_EQ(result[0].utterance.message(), "foo radio button unselected");
}

TEST_F(ScreenReaderMessageGeneratorTest, NodeRadioButtonEmptyLabel) {
  Node node;
  node.mutable_attributes()->set_label("");
  node.set_role(Role::RADIO_BUTTON);
  node.mutable_states()->set_selected(false);
  mock_message_formatter_ptr_->SetMessageForId(
      static_cast<uint64_t>(MessageIds::RADIO_BUTTON_UNSELECTED), "radio button unselected");
  auto result = screen_reader_message_generator_->DescribeNode(&node);
  ASSERT_EQ(result.size(), 1u);
  ASSERT_TRUE(result[0].utterance.has_message());
  ASSERT_EQ(result[0].utterance.message(), "radio button unselected");
}

TEST_F(ScreenReaderMessageGeneratorTest, NodeRadioButtonMessageFormatterReturnNullopt) {
  Node node;
  node.mutable_attributes()->set_label("");
  node.set_role(Role::RADIO_BUTTON);
  node.mutable_states()->set_selected(false);
  auto result = screen_reader_message_generator_->DescribeNode(&node);
  ASSERT_EQ(result.size(), 1u);
  ASSERT_FALSE(result[0].utterance.has_message());
}

TEST_F(ScreenReaderMessageGeneratorTest, NodeLink) {
  Node node;
  node.mutable_attributes()->set_label("foo");
  node.set_role(Role::LINK);
  mock_message_formatter_ptr_->SetMessageForId(static_cast<uint64_t>(MessageIds::ROLE_LINK),
                                               "link");
  auto result = screen_reader_message_generator_->DescribeNode(&node);
  ASSERT_EQ(result.size(), 2u);
  ASSERT_TRUE(result[0].utterance.has_message());
  ASSERT_EQ(result[0].utterance.message(), "foo");
  ASSERT_TRUE(result[1].utterance.has_message());
  ASSERT_EQ(result[1].utterance.message(), "link");
}

TEST_F(ScreenReaderMessageGeneratorTest, NodeLinkEmptyLabel) {
  Node node;
  node.mutable_attributes()->set_label("");
  node.set_role(Role::LINK);
  mock_message_formatter_ptr_->SetMessageForId(static_cast<uint64_t>(MessageIds::ROLE_LINK),
                                               "link");
  auto result = screen_reader_message_generator_->DescribeNode(&node);
  ASSERT_EQ(result.size(), 1u);
  ASSERT_TRUE(result[0].utterance.has_message());
  ASSERT_EQ(result[0].utterance.message(), "link");
}

TEST_F(ScreenReaderMessageGeneratorTest, NodeCheckBoxWithoutStates) {
  Node node;
  node.mutable_attributes()->set_label("foo");
  node.set_role(Role::CHECK_BOX);
  mock_message_formatter_ptr_->SetMessageForId(static_cast<uint64_t>(MessageIds::ROLE_CHECKBOX),
                                               "check box");
  auto result = screen_reader_message_generator_->DescribeNode(&node);
  ASSERT_EQ(result.size(), 2u);
  ASSERT_TRUE(result[0].utterance.has_message());
  ASSERT_EQ(result[0].utterance.message(), "foo");
  ASSERT_TRUE(result[1].utterance.has_message());
  ASSERT_EQ(result[1].utterance.message(), "check box");
}

TEST_F(ScreenReaderMessageGeneratorTest, NodeCheckBoxWithStates) {
  Node node;
  node.mutable_attributes()->set_label("foo");
  node.set_role(Role::CHECK_BOX);
  mock_message_formatter_ptr_->SetMessageForId(static_cast<uint64_t>(MessageIds::ROLE_CHECKBOX),
                                               "check box");
  mock_message_formatter_ptr_->SetMessageForId(static_cast<uint64_t>(MessageIds::ELEMENT_CHECKED),
                                               "checked");
  mock_message_formatter_ptr_->SetMessageForId(
      static_cast<uint64_t>(MessageIds::ELEMENT_NOT_CHECKED), "not checked");
  mock_message_formatter_ptr_->SetMessageForId(
      static_cast<uint64_t>(MessageIds::ELEMENT_PARTIALLY_CHECKED), "partially checked");
  node.mutable_states()->set_checked_state(
      fuchsia::accessibility::semantics::CheckedState::CHECKED);

  auto result = screen_reader_message_generator_->DescribeNode(&node);
  ASSERT_EQ(result.size(), 3u);
  ASSERT_TRUE(result[0].utterance.has_message());
  ASSERT_EQ(result[0].utterance.message(), "foo");
  ASSERT_TRUE(result[1].utterance.has_message());
  ASSERT_EQ(result[1].utterance.message(), "check box");
  ASSERT_TRUE(result[2].utterance.has_message());
  ASSERT_EQ(result[2].utterance.message(), "checked");

  node.mutable_states()->set_checked_state(
      fuchsia::accessibility::semantics::CheckedState::UNCHECKED);

  result = screen_reader_message_generator_->DescribeNode(&node);
  ASSERT_EQ(result.size(), 3u);
  ASSERT_TRUE(result[0].utterance.has_message());
  ASSERT_EQ(result[0].utterance.message(), "foo");
  ASSERT_TRUE(result[1].utterance.has_message());
  ASSERT_EQ(result[1].utterance.message(), "check box");
  ASSERT_TRUE(result[2].utterance.has_message());
  ASSERT_EQ(result[2].utterance.message(), "not checked");

  node.mutable_states()->set_checked_state(fuchsia::accessibility::semantics::CheckedState::MIXED);

  result = screen_reader_message_generator_->DescribeNode(&node);
  ASSERT_EQ(result.size(), 3u);
  ASSERT_TRUE(result[0].utterance.has_message());
  ASSERT_EQ(result[0].utterance.message(), "foo");
  ASSERT_TRUE(result[1].utterance.has_message());
  ASSERT_EQ(result[1].utterance.message(), "check box");
  ASSERT_TRUE(result[2].utterance.has_message());
  ASSERT_EQ(result[2].utterance.message(), "partially checked");

  node.mutable_states()->set_checked_state(fuchsia::accessibility::semantics::CheckedState::NONE);

  result = screen_reader_message_generator_->DescribeNode(&node);
  ASSERT_EQ(result.size(), 2u);
  ASSERT_TRUE(result[0].utterance.has_message());
  ASSERT_EQ(result[0].utterance.message(), "foo");
  ASSERT_TRUE(result[1].utterance.has_message());
  ASSERT_EQ(result[1].utterance.message(), "check box");
}

TEST_F(ScreenReaderMessageGeneratorTest, NodeToggleSwitchOn) {
  Node node;
  node.mutable_attributes()->set_label("foo");
  node.set_role(Role::TOGGLE_SWITCH);
  node.mutable_states()->set_toggled_state(fuchsia::accessibility::semantics::ToggledState::ON);
  mock_message_formatter_ptr_->SetMessageForId(
      static_cast<uint64_t>(MessageIds::ELEMENT_TOGGLED_ON), "switch on");
  auto result = screen_reader_message_generator_->DescribeNode(&node);
  ASSERT_EQ(result.size(), 2u);
  ASSERT_TRUE(result[0].utterance.has_message());
  ASSERT_EQ(result[0].utterance.message(), "foo");
  ASSERT_TRUE(result[1].utterance.has_message());
  ASSERT_EQ(result[1].utterance.message(), "switch on");
}

TEST_F(ScreenReaderMessageGeneratorTest, NodeToggleSwitchOff) {
  Node node;
  node.mutable_attributes()->set_label("foo");
  node.set_role(Role::TOGGLE_SWITCH);
  node.mutable_states()->set_toggled_state(fuchsia::accessibility::semantics::ToggledState::OFF);
  mock_message_formatter_ptr_->SetMessageForId(
      static_cast<uint64_t>(MessageIds::ELEMENT_TOGGLED_OFF), "switch off");
  auto result = screen_reader_message_generator_->DescribeNode(&node);
  ASSERT_EQ(result.size(), 2u);
  ASSERT_TRUE(result[0].utterance.has_message());
  ASSERT_EQ(result[0].utterance.message(), "foo");
  ASSERT_TRUE(result[1].utterance.has_message());
  ASSERT_EQ(result[1].utterance.message(), "switch off");
}

TEST_F(ScreenReaderMessageGeneratorTest, NodeToggleSwitchIndeterminate) {
  Node node;
  node.mutable_attributes()->set_label("foo");
  node.set_role(Role::TOGGLE_SWITCH);
  node.mutable_states()->set_toggled_state(
      fuchsia::accessibility::semantics::ToggledState::INDETERMINATE);
  mock_message_formatter_ptr_->SetMessageForId(
      static_cast<uint64_t>(MessageIds::ELEMENT_TOGGLED_OFF), "switch off");
  auto result = screen_reader_message_generator_->DescribeNode(&node);
  ASSERT_EQ(result.size(), 2u);
  ASSERT_TRUE(result[0].utterance.has_message());
  ASSERT_EQ(result[0].utterance.message(), "foo");
  ASSERT_TRUE(result[1].utterance.has_message());
  ASSERT_EQ(result[1].utterance.message(), "switch off");
}

TEST_F(ScreenReaderMessageGeneratorTest, NodeToggleSwitchEmptyLabel) {
  Node node;
  node.mutable_attributes()->set_label("");
  node.set_role(Role::TOGGLE_SWITCH);
  node.mutable_states()->set_toggled_state(
      fuchsia::accessibility::semantics::ToggledState::INDETERMINATE);
  mock_message_formatter_ptr_->SetMessageForId(
      static_cast<uint64_t>(MessageIds::ELEMENT_TOGGLED_OFF), "switch off");
  auto result = screen_reader_message_generator_->DescribeNode(&node);
  ASSERT_EQ(result.size(), 1u);
  ASSERT_TRUE(result[0].utterance.has_message());
  ASSERT_EQ(result[0].utterance.message(), "switch off");
}

TEST_F(ScreenReaderMessageGeneratorTest, NodeToggleSwitchMessageFormatterReturnsNullopt) {
  Node node;
  node.mutable_attributes()->set_label("");
  node.set_role(Role::TOGGLE_SWITCH);
  node.mutable_states()->set_toggled_state(
      fuchsia::accessibility::semantics::ToggledState::INDETERMINATE);
  auto result = screen_reader_message_generator_->DescribeNode(&node);
  ASSERT_EQ(result.size(), 1u);
  ASSERT_FALSE(result[0].utterance.has_message());
}

TEST_F(ScreenReaderMessageGeneratorTest, FormatCharacterForSpelling) {
  mock_message_formatter_ptr_->SetMessageForId(
      static_cast<uint64_t>(MessageIds::PERIOD_SYMBOL_NAME), "dot");
  auto result = screen_reader_message_generator_->FormatCharacterForSpelling(".");
  ASSERT_TRUE(result.utterance.has_message());
  ASSERT_EQ(result.utterance.message(), "dot");

  // Sends a character that does not have a special spelling.
  auto result2 = screen_reader_message_generator_->FormatCharacterForSpelling("a");
  ASSERT_TRUE(result2.utterance.has_message());
  ASSERT_EQ(result2.utterance.message(), "a");

  // Sends a letter that is capitalized and should be read as such.
  mock_message_formatter_ptr_->SetMessageForId(
      static_cast<uint64_t>(MessageIds::CAPITALIZED_LETTER), "capital A");
  auto result3 = screen_reader_message_generator_->FormatCharacterForSpelling("A");
  ASSERT_TRUE(result3.utterance.has_message());
  ASSERT_EQ(result3.utterance.message(), "capital A");
}

TEST_F(ScreenReaderMessageGeneratorTest, NodeTextField) {
  Node node;
  node.mutable_attributes()->set_label("foo");
  node.set_role(Role::TEXT_FIELD);
  mock_message_formatter_ptr_->SetMessageForId(static_cast<uint64_t>(MessageIds::ROLE_TEXT_FIELD),
                                               "text field");
  auto result = screen_reader_message_generator_->DescribeNode(&node);
  ASSERT_EQ(result.size(), 2u);
  ASSERT_TRUE(result[0].utterance.has_message());
  ASSERT_EQ(result[0].utterance.message(), "foo");
  ASSERT_TRUE(result[1].utterance.has_message());
  ASSERT_EQ(result[1].utterance.message(), "text field");
}

TEST_F(ScreenReaderMessageGeneratorTest, NodeSearchBox) {
  Node node;
  node.mutable_attributes()->set_label("foo");
  node.set_role(Role::SEARCH_BOX);
  mock_message_formatter_ptr_->SetMessageForId(static_cast<uint64_t>(MessageIds::ROLE_SEARCH_BOX),
                                               "search box");
  auto result = screen_reader_message_generator_->DescribeNode(&node);
  ASSERT_EQ(result.size(), 2u);
  ASSERT_TRUE(result[0].utterance.has_message());
  ASSERT_EQ(result[0].utterance.message(), "foo");
  ASSERT_TRUE(result[1].utterance.has_message());
  ASSERT_EQ(result[1].utterance.message(), "search box");
}

TEST_F(ScreenReaderMessageGeneratorTest, NodeTableRowHeader) {
  Node node;
  node.mutable_attributes()->set_label("label");
  // Row span of 1 should not be read.
  node.mutable_attributes()->mutable_table_cell_attributes()->set_row_span(1);
  node.mutable_attributes()->mutable_table_cell_attributes()->set_column_span(2);
  node.mutable_attributes()->mutable_table_cell_attributes()->set_row_index(3);
  node.set_role(Role::ROW_HEADER);
  mock_message_formatter_ptr_->SetMessageForId(
      static_cast<uint64_t>(MessageIds::ROLE_TABLE_ROW_HEADER), "row header");
  mock_message_formatter_ptr_->SetMessageForId(static_cast<uint64_t>(MessageIds::ROW_SPAN),
                                               "row span");
  mock_message_formatter_ptr_->SetMessageForId(static_cast<uint64_t>(MessageIds::COLUMN_SPAN),
                                               "column span");
  mock_message_formatter_ptr_->SetMessageForId(static_cast<uint64_t>(MessageIds::ROW_SUMMARY),
                                               "row summary");
  mock_message_formatter_ptr_->SetMessageForId(static_cast<uint64_t>(MessageIds::COLUMN_SUMMARY),
                                               "column summary");
  auto result = screen_reader_message_generator_->DescribeNode(&node);
  ASSERT_EQ(result.size(), 4u);
  ASSERT_TRUE(result[0].utterance.has_message());
  ASSERT_EQ(result[0].utterance.message(), "label");
  ASSERT_TRUE(result[1].utterance.has_message());
  ASSERT_EQ(result[1].utterance.message(), "row summary");
  ASSERT_TRUE(result[2].utterance.has_message());
  ASSERT_EQ(result[2].utterance.message(), "column span");
  ASSERT_TRUE(result[3].utterance.has_message());
  ASSERT_EQ(result[3].utterance.message(), "row header");

  {
    const auto& args =
        mock_message_formatter_ptr_->GetArgsForId(static_cast<uint64_t>(MessageIds::COLUMN_SPAN));
    ASSERT_EQ(args.size(), 1u);
    EXPECT_EQ(args[0].first, "column_span");
    EXPECT_EQ(args[0].second, "2");
  }

  {
    const auto& args =
        mock_message_formatter_ptr_->GetArgsForId(static_cast<uint64_t>(MessageIds::ROW_SUMMARY));
    ASSERT_EQ(args.size(), 1u);
    EXPECT_EQ(args[0].first, "row_index");
    EXPECT_EQ(args[0].second, "3");
  }
}

TEST_F(ScreenReaderMessageGeneratorTest, NodeTableColumnHeader) {
  Node node;
  node.mutable_attributes()->set_label("label");
  node.mutable_attributes()->mutable_table_cell_attributes()->set_row_span(2);
  // Column span of 1 should not be read.
  node.mutable_attributes()->mutable_table_cell_attributes()->set_column_span(1);
  node.mutable_attributes()->mutable_table_cell_attributes()->set_column_index(3);
  node.set_role(Role::COLUMN_HEADER);
  mock_message_formatter_ptr_->SetMessageForId(
      static_cast<uint64_t>(MessageIds::ROLE_TABLE_COLUMN_HEADER), "column header");
  mock_message_formatter_ptr_->SetMessageForId(static_cast<uint64_t>(MessageIds::ROW_SPAN),
                                               "row span");
  mock_message_formatter_ptr_->SetMessageForId(static_cast<uint64_t>(MessageIds::COLUMN_SPAN),
                                               "column span");
  mock_message_formatter_ptr_->SetMessageForId(static_cast<uint64_t>(MessageIds::ROW_SUMMARY),
                                               "row summary");
  mock_message_formatter_ptr_->SetMessageForId(static_cast<uint64_t>(MessageIds::COLUMN_SUMMARY),
                                               "column summary");
  auto result = screen_reader_message_generator_->DescribeNode(&node);
  ASSERT_EQ(result.size(), 4u);
  ASSERT_TRUE(result[0].utterance.has_message());
  ASSERT_EQ(result[0].utterance.message(), "label");
  ASSERT_TRUE(result[1].utterance.has_message());
  ASSERT_EQ(result[1].utterance.message(), "column summary");
  ASSERT_TRUE(result[2].utterance.has_message());
  ASSERT_EQ(result[2].utterance.message(), "row span");
  ASSERT_TRUE(result[3].utterance.has_message());
  ASSERT_EQ(result[3].utterance.message(), "column header");

  {
    const auto& args =
        mock_message_formatter_ptr_->GetArgsForId(static_cast<uint64_t>(MessageIds::ROW_SPAN));
    ASSERT_EQ(args.size(), 1u);
    EXPECT_EQ(args[0].first, "row_span");
    EXPECT_EQ(args[0].second, "2");
  }

  {
    const auto& args = mock_message_formatter_ptr_->GetArgsForId(
        static_cast<uint64_t>(MessageIds::COLUMN_SUMMARY));
    ASSERT_EQ(args.size(), 1u);
    EXPECT_EQ(args[0].first, "column_index");
    EXPECT_EQ(args[0].second, "3");
  }
}

TEST_F(ScreenReaderMessageGeneratorTest, NodeTableCellWithAllAttributes) {
  Node node;
  node.mutable_attributes()->set_label("label");
  node.mutable_attributes()->mutable_table_cell_attributes()->set_row_span(2);
  node.mutable_attributes()->mutable_table_cell_attributes()->set_column_span(3);
  node.mutable_attributes()->mutable_table_cell_attributes()->set_row_index(4);
  node.mutable_attributes()->mutable_table_cell_attributes()->set_column_index(5);
  a11y::ScreenReaderMessageGenerator::ScreenReaderMessageContext context;
  a11y::ScreenReaderMessageGenerator::TableCellContext cell_context;
  cell_context.row_header = "row header";
  cell_context.column_header = "column header";
  context.table_cell_context.emplace(cell_context);
  node.set_role(Role::CELL);
  mock_message_formatter_ptr_->SetMessageForId(static_cast<uint64_t>(MessageIds::ROLE_TABLE_CELL),
                                               "table cell");
  mock_message_formatter_ptr_->SetMessageForId(static_cast<uint64_t>(MessageIds::ROW_SPAN),
                                               "row span");
  mock_message_formatter_ptr_->SetMessageForId(static_cast<uint64_t>(MessageIds::COLUMN_SPAN),
                                               "column span");
  mock_message_formatter_ptr_->SetMessageForId(static_cast<uint64_t>(MessageIds::CELL_SUMMARY),
                                               "cell summary");
  auto result = screen_reader_message_generator_->DescribeNode(&node, std::move(context));
  ASSERT_EQ(result.size(), 5u);
  ASSERT_TRUE(result[0].utterance.has_message());
  ASSERT_EQ(result[0].utterance.message(), "row header, column header, label");
  ASSERT_TRUE(result[1].utterance.has_message());
  ASSERT_EQ(result[1].utterance.message(), "row span");
  ASSERT_TRUE(result[2].utterance.has_message());
  ASSERT_EQ(result[2].utterance.message(), "column span");
  ASSERT_TRUE(result[3].utterance.has_message());
  ASSERT_EQ(result[3].utterance.message(), "cell summary");
  ASSERT_TRUE(result[4].utterance.has_message());
  ASSERT_EQ(result[4].utterance.message(), "table cell");

  {
    const auto& args =
        mock_message_formatter_ptr_->GetArgsForId(static_cast<uint64_t>(MessageIds::ROW_SPAN));
    ASSERT_EQ(args.size(), 1u);
    EXPECT_EQ(args[0].first, "row_span");
    EXPECT_EQ(args[0].second, "2");
  }

  {
    const auto& args =
        mock_message_formatter_ptr_->GetArgsForId(static_cast<uint64_t>(MessageIds::COLUMN_SPAN));
    ASSERT_EQ(args.size(), 1u);
    EXPECT_EQ(args[0].first, "column_span");
    EXPECT_EQ(args[0].second, "3");
  }

  {
    const auto& args =
        mock_message_formatter_ptr_->GetArgsForId(static_cast<uint64_t>(MessageIds::CELL_SUMMARY));
    ASSERT_EQ(args.size(), 2u);
    EXPECT_EQ(args[0].first, "row_index");
    EXPECT_EQ(args[0].second, "4");
    EXPECT_EQ(args[1].first, "column_index");
    EXPECT_EQ(args[1].second, "5");
  }
}

TEST_F(ScreenReaderMessageGeneratorTest, NodeTableCellWithLabelOnly) {
  Node node;
  node.set_role(Role::CELL);
  node.mutable_attributes()->set_label("label");
  // Add unused messages to avoid confounding variable of unavailable message
  // string.
  mock_message_formatter_ptr_->SetMessageForId(static_cast<uint64_t>(MessageIds::ROLE_TABLE_CELL),
                                               "table cell");
  mock_message_formatter_ptr_->SetMessageForId(static_cast<uint64_t>(MessageIds::ROW_SPAN),
                                               "row span");
  mock_message_formatter_ptr_->SetMessageForId(static_cast<uint64_t>(MessageIds::COLUMN_SPAN),
                                               "column span");
  mock_message_formatter_ptr_->SetMessageForId(static_cast<uint64_t>(MessageIds::CELL_SUMMARY),
                                               "cell summary");
  auto result = screen_reader_message_generator_->DescribeNode(&node);
  ASSERT_EQ(result.size(), 2u);
  ASSERT_TRUE(result[0].utterance.has_message());
  ASSERT_EQ(result[0].utterance.message(), "label");
  ASSERT_TRUE(result[1].utterance.has_message());
  ASSERT_EQ(result[1].utterance.message(), "table cell");
}

TEST_F(ScreenReaderMessageGeneratorTest, NodeTableCellWithNoAttributes) {
  Node node;
  node.set_role(Role::CELL);
  // Add unused messages to avoid confounding variable of unavailable message
  // string.
  mock_message_formatter_ptr_->SetMessageForId(static_cast<uint64_t>(MessageIds::ROLE_TABLE_CELL),
                                               "table cell");
  mock_message_formatter_ptr_->SetMessageForId(static_cast<uint64_t>(MessageIds::ROW_SPAN),
                                               "row span");
  mock_message_formatter_ptr_->SetMessageForId(static_cast<uint64_t>(MessageIds::COLUMN_SPAN),
                                               "column span");
  mock_message_formatter_ptr_->SetMessageForId(static_cast<uint64_t>(MessageIds::CELL_SUMMARY),
                                               "cell summary");
  auto result = screen_reader_message_generator_->DescribeNode(&node);
  ASSERT_EQ(result.size(), 1u);
  ASSERT_TRUE(result[0].utterance.has_message());
  ASSERT_EQ(result[0].utterance.message(), "table cell");
}

TEST_F(ScreenReaderMessageGeneratorTest, EnteredTable) {
  Node node;
  node.mutable_attributes()->set_label("node label");

  Node table;
  table.set_role(Role::TABLE);
  table.mutable_attributes()->set_label("table label");
  table.mutable_attributes()->mutable_table_attributes()->set_number_of_rows(2);
  table.mutable_attributes()->mutable_table_attributes()->set_number_of_columns(3);

  a11y::ScreenReaderMessageGenerator::ScreenReaderMessageContext message_context;
  message_context.current_container = &table;
  message_context.previous_container = nullptr;

  mock_message_formatter_ptr_->SetMessageForId(static_cast<uint64_t>(MessageIds::ENTERED_TABLE),
                                               "entered table");
  mock_message_formatter_ptr_->SetMessageForId(static_cast<uint64_t>(MessageIds::ROLE_TABLE),
                                               "table");
  mock_message_formatter_ptr_->SetMessageForId(static_cast<uint64_t>(MessageIds::TABLE_DIMENSIONS),
                                               "dimensions");
  auto result = screen_reader_message_generator_->DescribeNode(&node, message_context);
  ASSERT_EQ(result.size(), 5u);
  ASSERT_TRUE(result[0].utterance.has_message());
  EXPECT_EQ(result[0].utterance.message(), "entered table");
  ASSERT_TRUE(result[1].utterance.has_message());
  EXPECT_EQ(result[1].utterance.message(), "table label");
  ASSERT_TRUE(result[2].utterance.has_message());
  EXPECT_EQ(result[2].utterance.message(), "dimensions");
  ASSERT_TRUE(result[3].utterance.has_message());
  EXPECT_EQ(result[3].utterance.message(), "table");
  ASSERT_TRUE(result[4].utterance.has_message());
  EXPECT_EQ(result[4].utterance.message(), "node label");
}

TEST_F(ScreenReaderMessageGeneratorTest, ExitedTable) {
  Node node;
  node.mutable_attributes()->set_label("node label");

  Node table;
  table.set_role(Role::TABLE);

  a11y::ScreenReaderMessageGenerator::ScreenReaderMessageContext message_context;
  message_context.current_container = nullptr;
  message_context.previous_container = &table;

  mock_message_formatter_ptr_->SetMessageForId(static_cast<uint64_t>(MessageIds::EXITED_TABLE),
                                               "exited table");
  auto result = screen_reader_message_generator_->DescribeNode(&node, message_context);
  ASSERT_EQ(result.size(), 2u);
  ASSERT_TRUE(result[0].utterance.has_message());
  EXPECT_EQ(result[0].utterance.message(), "exited table");
  ASSERT_TRUE(result[1].utterance.has_message());
  EXPECT_EQ(result[1].utterance.message(), "node label");
}

TEST_F(ScreenReaderMessageGeneratorTest, ExitedNestedTable) {
  Node node;
  node.mutable_attributes()->set_label("node label");

  Node table;
  table.set_role(Role::TABLE);

  Node table_2;
  table_2.set_role(Role::TABLE);

  a11y::ScreenReaderMessageGenerator::ScreenReaderMessageContext message_context;
  message_context.current_container = &table_2;
  message_context.previous_container = &table;
  message_context.exited_nested_container = true;

  mock_message_formatter_ptr_->SetMessageForId(static_cast<uint64_t>(MessageIds::EXITED_TABLE),
                                               "exited table");
  auto result = screen_reader_message_generator_->DescribeNode(&node, message_context);
  ASSERT_EQ(result.size(), 2u);
  ASSERT_TRUE(result[0].utterance.has_message());
  EXPECT_EQ(result[0].utterance.message(), "exited table");
  ASSERT_TRUE(result[1].utterance.has_message());
  EXPECT_EQ(result[1].utterance.message(), "node label");
}

}  // namespace
}  // namespace accessibility_test
