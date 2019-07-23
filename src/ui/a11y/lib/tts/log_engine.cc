// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/tts/log_engine.h"

#include <src/lib/fxl/logging.h>

namespace a11y {

namespace {

// Returns a string representation of an utterance.
std::string UtteranceToString(const fuchsia::accessibility::tts::Utterance& utterance) {
  if (utterance.message().empty()) {
    return "**empty utterance**";
  }
  return utterance.message();
}

}  // namespace

LogEngine::LogEngine(sys::ComponentContext* startup_context) {
  FXL_CHECK(startup_context);
  registry_ = startup_context->svc()->Connect<fuchsia::accessibility::tts::EngineRegistry>();
  auto engine_handle = bindings_.AddBinding(this);
  registry_->RegisterEngine(std::move(engine_handle), [](auto) {});
}

void LogEngine::Enqueue(fuchsia::accessibility::tts::Utterance utterance,
                        EnqueueCallback callback) {
  FXL_LOG(INFO) << "Received utterance: " << UtteranceToString(utterance);
  utterances_.emplace_back(std::move(utterance));
  fuchsia::accessibility::tts::Engine_Enqueue_Result result;
  result.set_response(fuchsia::accessibility::tts::Engine_Enqueue_Response{});
  callback(std::move(result));
}

void LogEngine::Speak(SpeakCallback callback) {
  FXL_LOG(INFO) << "Received a Speak. Dispatching the following utterances:";
  for (const auto& utterance : utterances_) {
    FXL_LOG(INFO) << "  - " << UtteranceToString(utterance);
  }
  utterances_.clear();
  fuchsia::accessibility::tts::Engine_Speak_Result result;
  result.set_response(fuchsia::accessibility::tts::Engine_Speak_Response{});
  callback(std::move(result));
}

void LogEngine::Cancel(CancelCallback callback) {
  FXL_LOG(INFO) << "Received a Cancel";
  callback();
}

}  // namespace a11y
