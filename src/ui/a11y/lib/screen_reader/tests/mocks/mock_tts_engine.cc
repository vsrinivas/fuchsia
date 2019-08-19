// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/screen_reader/tests/mocks/mock_tts_engine.h"

namespace accessibility_test {

MockTtsEngine::MockTtsEngine() {}

void MockTtsEngine::Enqueue(fuchsia::accessibility::tts::Utterance utterance,
                            EnqueueCallback callback) {
  utterances_.push_back(std::move(utterance));
  fuchsia::accessibility::tts::Engine_Enqueue_Result result;
  result.set_response(fuchsia::accessibility::tts::Engine_Enqueue_Response{});
  callback(std::move(result));
}

void MockTtsEngine::Speak(SpeakCallback callback) {
  received_speak_ = true;
  fuchsia::accessibility::tts::Engine_Speak_Result result;
  result.set_response(fuchsia::accessibility::tts::Engine_Speak_Response{});
  callback(std::move(result));
}

void MockTtsEngine::Cancel(CancelCallback callback) {
  received_cancel_ = true;
  utterances_.clear();
  callback();
}

}  // namespace accessibility_test
