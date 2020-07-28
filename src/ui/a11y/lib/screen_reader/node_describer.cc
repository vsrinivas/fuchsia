// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/syslog/cpp/macros.h>

// This header file has been generated from the strings library fuchsia.intl.l10n.
#include "fuchsia/intl/l10n/cpp/fidl.h"
#include "src/ui/a11y/lib/screen_reader/node_describer.h"
#include "src/ui/a11y/lib/screen_reader/util/util.h"

namespace a11y {
namespace {

using fuchsia::accessibility::semantics::Role;
using fuchsia::accessibility::tts::Utterance;
using fuchsia::intl::l10n::MessageIds;

static constexpr zx::duration kDefaultDelay = zx::msec(40);

// Returns a message that describes a node where Role == BUTTON.
NodeDescriber::UtteranceAndContext DescribeButton(a11y::i18n::MessageFormatter* formatter) {
  NodeDescriber::UtteranceAndContext utterance;
  auto message = formatter->FormatStringById(static_cast<uint64_t>(MessageIds::ROLE_BUTTON));
  FX_DCHECK(message);
  utterance.utterance.set_message(std::move(*message));
  utterance.delay = kDefaultDelay;
  return utterance;
}

// Returns a message that describes a node where Role == HEADER.
NodeDescriber::UtteranceAndContext DescribeHeader(a11y::i18n::MessageFormatter* formatter) {
  NodeDescriber::UtteranceAndContext utterance;
  auto message = formatter->FormatStringById(static_cast<uint64_t>(MessageIds::ROLE_HEADER));
  FX_DCHECK(message);
  utterance.utterance.set_message(std::move(*message));
  utterance.delay = kDefaultDelay;
  return utterance;
}

// Returns a message that describes a node where Role == IMAGE.
NodeDescriber::UtteranceAndContext DescribeImage(a11y::i18n::MessageFormatter* formatter) {
  NodeDescriber::UtteranceAndContext utterance;
  auto message = formatter->FormatStringById(static_cast<uint64_t>(MessageIds::ROLE_IMAGE));
  FX_DCHECK(message);
  utterance.utterance.set_message(std::move(*message));
  utterance.delay = kDefaultDelay;
  return utterance;
}

// Returns a message that describes a node where Role == SLIDER.
NodeDescriber::UtteranceAndContext DescribeSlider(a11y::i18n::MessageFormatter* formatter) {
  NodeDescriber::UtteranceAndContext utterance;
  auto message = formatter->FormatStringById(static_cast<uint64_t>(MessageIds::ROLE_SLIDER));
  FX_DCHECK(message);
  utterance.utterance.set_message(std::move(*message));
  utterance.delay = kDefaultDelay;
  return utterance;
}

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

NodeDescriber::NodeDescriber(std::unique_ptr<i18n::MessageFormatter> message_formatter)
    : message_formatter_(std::move(message_formatter)) {}

std::vector<NodeDescriber::UtteranceAndContext> NodeDescriber::DescribeNode(
    const fuchsia::accessibility::semantics::Node* node) {
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
        description.emplace_back(DescribeButton(message_formatter_.get()));
      } else if (node->role() == Role::HEADER) {
        description.emplace_back(DescribeHeader(message_formatter_.get()));
      } else if (node->role() == Role::IMAGE) {
        description.emplace_back(DescribeImage(message_formatter_.get()));
      } else if (node->role() == Role::SLIDER) {
        // Add the slider's range value to the label utterance, if specified.
        auto& label_utterance = description.back().utterance;
        label_utterance.set_message(GetSliderLabelAndRangeMessage(node));

        // Add a role description for the slider.
        description.emplace_back(DescribeSlider(message_formatter_.get()));
      }
    }
  }
  return description;
}

}  // namespace a11y
