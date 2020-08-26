// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/screen_reader/tests/mocks/mock_screen_reader_context.h"

#include <lib/syslog/cpp/macros.h>

namespace accessibility_test {

fit::promise<> MockScreenReaderContext::MockSpeaker::SpeakNodePromise(
    const fuchsia::accessibility::semantics::Node* node, Options options) {
  received_speak_ = true;
  node_ids_.push_back(node->node_id());
  return fit::make_ok_promise();
}

fit::promise<> MockScreenReaderContext::MockSpeaker::SpeakMessagePromise(
    fuchsia::accessibility::tts::Utterance utterance, Options options) {
  received_speak_ = true;
  if (utterance.has_message()) {
    messages_.push_back(utterance.message());
  }
  return fit::make_ok_promise();
}

fit::promise<> MockScreenReaderContext::MockSpeaker::SpeakMessageByIdPromise(
    fuchsia::intl::l10n::MessageIds message_id, Options options) {
  received_speak_ = true;
  message_ids_.push_back(message_id);
  return fit::make_ok_promise();
}

fit::promise<> MockScreenReaderContext::MockSpeaker::CancelTts() {
  received_cancel_ = true;
  messages_.clear();
  return fit::make_ok_promise();
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
