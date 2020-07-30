// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/syslog/cpp/macros.h>

// This header file has been generated from the strings library fuchsia.intl.l10n.
#include "fuchsia/intl/l10n/cpp/fidl.h"
#include "src/ui/a11y/lib/screen_reader/screen_reader_message_generator.h"
#include "src/ui/a11y/lib/screen_reader/util/util.h"

namespace a11y {
namespace {

using fuchsia::accessibility::semantics::Role;
using fuchsia::accessibility::tts::Utterance;
using fuchsia::intl::l10n::MessageIds;

static constexpr zx::duration kDefaultDelay = zx::msec(40);

// Returns a message that describes the label and range value of a slider.
std::string GetSliderLabelAndRangeMessage(const fuchsia::accessibility::semantics::Node* node) {
  std::string message;
  if (node->has_attributes() && node->attributes().has_label()) {
    message += node->attributes().label();
  }

  if (node->has_states() && node->states().has_range_value()) {
    message = message + ", " + FormatFloat(node->states().range_value());
  }

  return message;
}

}  // namespace

ScreenReaderMessageGenerator::ScreenReaderMessageGenerator(
    std::unique_ptr<i18n::MessageFormatter> message_formatter)
    : message_formatter_(std::move(message_formatter)) {}

std::vector<ScreenReaderMessageGenerator::UtteranceAndContext>
ScreenReaderMessageGenerator::DescribeNode(const fuchsia::accessibility::semantics::Node* node) {
  std::vector<UtteranceAndContext> description;
  {
    Utterance utterance;
    if (node->has_attributes() && node->attributes().has_label()) {
      utterance.set_message(node->attributes().label());
    }
    // Note that empty descriptions (no labels), are allowed. It is common for developers forget to
    // add accessible labels to their UI elements, which causes them to not have one. It is
    // desirable still to tell the user what the node is (a button), so the Screen Reader can read
    // something like: (pause) button.
    description.emplace_back(UtteranceAndContext{.utterance = std::move(utterance)});
  }
  {
    Utterance utterance;
    if (node->has_role()) {
      if (node->role() == Role::BUTTON) {
        description.emplace_back(GenerateUtteranceByMessageId(MessageIds::ROLE_BUTTON));
      } else if (node->role() == Role::HEADER) {
        description.emplace_back(GenerateUtteranceByMessageId(MessageIds::ROLE_HEADER));
      } else if (node->role() == Role::IMAGE) {
        description.emplace_back(GenerateUtteranceByMessageId(MessageIds::ROLE_IMAGE));
      } else if (node->role() == Role::SLIDER) {
        // Add the slider's range value to the label utterance, if specified.
        auto& label_utterance = description.back().utterance;
        label_utterance.set_message(GetSliderLabelAndRangeMessage(node));

        // Add a role description for the slider.
        description.emplace_back(GenerateUtteranceByMessageId(MessageIds::ROLE_SLIDER));
      }
    }
  }
  return description;
}

ScreenReaderMessageGenerator::UtteranceAndContext
ScreenReaderMessageGenerator::GenerateUtteranceByMessageId(
    fuchsia::intl::l10n::MessageIds message_id, zx::duration delay) {
  UtteranceAndContext utterance;
  auto message = message_formatter_->FormatStringById(static_cast<uint64_t>(message_id));
  FX_DCHECK(message);
  utterance.utterance.set_message(std::move(*message));
  utterance.delay = delay;
  return utterance;
}

}  // namespace a11y
