// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/screen_reader/tests/mocks/mock_screen_reader_message_generator.h"

#include <lib/syslog/cpp/macros.h>

namespace accessibility_test {

void MockScreenReaderMessageGenerator::set_description(
    std::vector<UtteranceAndContext> description) {
  description_ = std::move(description);
}

void MockScreenReaderMessageGenerator::set_message(fuchsia::intl::l10n::MessageIds id,
                                                   UtteranceAndContext message) {
  messages_[id] = std::move(message);
}

std::vector<a11y::ScreenReaderMessageGenerator::UtteranceAndContext>
MockScreenReaderMessageGenerator::DescribeNode(
    const fuchsia::accessibility::semantics::Node* node) {
  if (description_) {
    auto value = std::move(*description_);
    description_ = std::nullopt;
    return value;
  }
  std::vector<UtteranceAndContext> description;
  UtteranceAndContext utterance;
  if (node->has_attributes() && node->attributes().has_label()) {
    utterance.utterance.set_message(node->attributes().label());
  }
  description.push_back(std::move(utterance));
  return description;
}

a11y::ScreenReaderMessageGenerator::UtteranceAndContext
MockScreenReaderMessageGenerator::GenerateUtteranceByMessageId(
    fuchsia::intl::l10n::MessageIds message_id, zx::duration delay,
    const std::vector<std::string>& arg_names, const std::vector<std::string>& arg_values) {
  UtteranceAndContext utterance;
  if (messages_.find(message_id) != messages_.end()) {
    utterance = std::move(messages_[message_id]);
    messages_.erase(message_id);
  }

  return utterance;
}

}  // namespace accessibility_test
