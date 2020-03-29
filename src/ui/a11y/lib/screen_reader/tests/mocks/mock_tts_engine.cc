// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/screen_reader/tests/mocks/mock_tts_engine.h"

namespace accessibility_test {

MockTtsEngine::MockTtsEngine() = default;

void MockTtsEngine::Enqueue(fuchsia::accessibility::tts::Utterance utterance,
                            EnqueueCallback callback) {
  fuchsia::accessibility::tts::Engine_Enqueue_Result result;
  if (should_fail_enqueue_) {
    result.set_err(fuchsia::accessibility::tts::Error::BAD_STATE);
  } else {
    result.set_response(fuchsia::accessibility::tts::Engine_Enqueue_Response{});
    utterances_.push_back(std::move(utterance));
  }
  callback(std::move(result));
}

void MockTtsEngine::Speak(SpeakCallback callback) {
  fuchsia::accessibility::tts::Engine_Speak_Result result;
  if (should_fail_speak_) {
    result.set_err(fuchsia::accessibility::tts::Error::BAD_STATE);
  } else {
    result.set_response(fuchsia::accessibility::tts::Engine_Speak_Response{});
    received_speak_ = true;
  }
  callback(std::move(result));
}

void MockTtsEngine::Cancel(CancelCallback callback) {
  received_cancel_ = true;
  utterances_.clear();
  callback();
}

}  // namespace accessibility_test
