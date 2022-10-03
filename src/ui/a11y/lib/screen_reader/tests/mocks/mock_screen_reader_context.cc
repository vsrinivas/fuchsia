// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/screen_reader/tests/mocks/mock_screen_reader_context.h"

#include <lib/syslog/cpp/macros.h>

namespace accessibility_test {

fpromise::promise<> MockScreenReaderContext::MockSpeaker::SpeakNodePromise(
    const fuchsia::accessibility::semantics::Node* node, Options options,
    a11y::ScreenReaderMessageGenerator::ScreenReaderMessageContext message_context) {
  received_speak_ = true;
  speak_node_ids_.push_back(node->node_id());
  speak_node_options_.push_back(options);
  speak_node_message_contexts_.push_back(message_context);
  return fpromise::make_ok_promise();
}

fpromise::promise<> MockScreenReaderContext::MockSpeaker::SpeakNodeCanonicalizedLabelPromise(
    const fuchsia::accessibility::semantics::Node* node, Options options) {
  received_speak_label_ = true;
  speak_node_ids_.push_back(node->node_id());
  speak_node_options_.push_back(options);
  speak_node_message_contexts_.emplace_back();
  return fpromise::make_ok_promise();
}

fpromise::promise<> MockScreenReaderContext::MockSpeaker::SpeakMessagePromise(
    fuchsia::accessibility::tts::Utterance utterance, Options options) {
  received_speak_ = true;
  FX_CHECK(utterance.has_message());
  messages_.push_back(utterance.message());
  return fpromise::make_ok_promise();
}

fpromise::promise<> MockScreenReaderContext::MockSpeaker::SpeakMessageByIdPromise(
    fuchsia::intl::l10n::MessageIds message_id, Options options) {
  received_speak_ = true;
  message_ids_.push_back(message_id);
  return fpromise::make_ok_promise();
}

fpromise::promise<> MockScreenReaderContext::MockSpeaker::CancelTts() {
  received_cancel_ = true;
  return fpromise::make_ok_promise();
}

MockScreenReaderContext::MockScreenReaderContext() : ScreenReaderContext() {
  auto a11y_focus_manager = std::make_unique<MockA11yFocusManager>();
  mock_a11y_focus_manager_ptr_ = a11y_focus_manager.get();
  a11y_focus_manager_ = std::move(a11y_focus_manager);
  auto mock_speaker = std::make_unique<MockSpeaker>();
  mock_speaker_ptr_ = mock_speaker.get();
  speaker_ = std::move(mock_speaker);
}

MockScreenReaderContext::MockSpeaker::~MockSpeaker() {
  if (on_destruction_callback_) {
    on_destruction_callback_(this);
  }
}

void MockScreenReaderContext::MockSpeaker::set_on_destruction_callback(
    OnDestructionCallback callback) {
  on_destruction_callback_ = std::move(callback);
}

}  // namespace accessibility_test
