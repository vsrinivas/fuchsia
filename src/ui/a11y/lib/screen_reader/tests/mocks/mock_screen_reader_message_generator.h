// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_A11Y_LIB_SCREEN_READER_TESTS_MOCKS_MOCK_SCREEN_READER_MESSAGE_GENERATOR_H_
#define SRC_UI_A11Y_LIB_SCREEN_READER_TESTS_MOCKS_MOCK_SCREEN_READER_MESSAGE_GENERATOR_H_

#include <fuchsia/accessibility/semantics/cpp/fidl.h>

#include <map>
#include <vector>

#include "src/ui/a11y/lib/screen_reader/screen_reader_message_generator.h"

namespace accessibility_test {

class MockScreenReaderMessageGenerator : public a11y::ScreenReaderMessageGenerator {
 public:
  MockScreenReaderMessageGenerator() = default;
  ~MockScreenReaderMessageGenerator() override = default;

  // Sets the description that will be returned to the next call to DescribeNode(). Note that this
  // works for only one call, and if multiple calls are going to be made, this function must be
  // invoked after each call to DescribeNode().
  void set_description(std::vector<UtteranceAndContext> description);

  // Sets the message that will be returned when calling
  // GenerateUtteranceByMessageId()
  // with |message_id|. This value is erased after each call to
  // GenerateUtteranceByMessageId(), so this function must be invoked between
  // successive calls.
  void set_message(fuchsia::intl::l10n::MessageIds id, UtteranceAndContext message);

  // |ScreenReaderMessageGenerator|
  std::vector<UtteranceAndContext> DescribeNode(
      const fuchsia::accessibility::semantics::Node* node) override;

  // |ScreenReaderMessageGenerator|
  UtteranceAndContext GenerateUtteranceByMessageId(
      fuchsia::intl::l10n::MessageIds message_id, zx::duration delay = zx::msec(0),
      const std::vector<std::string>& arg_names = std::vector<std::string>(),
      const std::vector<std::string>& arg_values = std::vector<std::string>()) override;

 private:
  std::optional<std::vector<UtteranceAndContext>> description_;
  std::map<fuchsia::intl::l10n::MessageIds, UtteranceAndContext> messages_;
};

}  // namespace accessibility_test

#endif  // SRC_UI_A11Y_LIB_SCREEN_READER_TESTS_MOCKS_MOCK_SCREEN_READER_MESSAGE_GENERATOR_H_
