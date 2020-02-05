// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/bin/a11y_tts_log_engine/log_engine.h"

#include "src/lib/syslog/cpp/logger.h"

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

LogEngine::LogEngine(std::unique_ptr<sys::ComponentContext> startup_context) {
  FX_DCHECK(startup_context);
  registry_ = startup_context->svc()->Connect<fuchsia::accessibility::tts::EngineRegistry>();
  auto engine_handle = bindings_.AddBinding(this);
  FX_LOGS(INFO) << "Registering the Tts Log Engine";
  registry_->RegisterEngine(
      std::move(engine_handle),
      [](const fuchsia::accessibility::tts::EngineRegistry_RegisterEngine_Result& result) {
        if (result.is_err()) {
          FX_LOGS(ERROR) << "Error registering Tts Log Engine: "
                         << static_cast<uint32_t>(result.err())
                         << " (Is another engine already registered?)";
        } else {
          FX_LOGS(INFO) << "Successfully registered Tts Log Engine.";
        }
      });
};

void LogEngine::Enqueue(fuchsia::accessibility::tts::Utterance utterance,
                        EnqueueCallback callback) {
  FX_LOGS(INFO) << "Received utterance: " << UtteranceToString(utterance);
  utterances_.emplace_back(std::move(utterance));
  fuchsia::accessibility::tts::Engine_Enqueue_Result result;
  result.set_response(fuchsia::accessibility::tts::Engine_Enqueue_Response{});
  callback(std::move(result));
}

void LogEngine::Speak(SpeakCallback callback) {
  FX_LOGS(INFO) << "Received a Speak. Dispatching the following utterances:";
  for (const auto& utterance : utterances_) {
    FX_LOGS(INFO) << "  - " << UtteranceToString(utterance);
  }
  utterances_.clear();
  fuchsia::accessibility::tts::Engine_Speak_Result result;
  result.set_response(fuchsia::accessibility::tts::Engine_Speak_Response{});
  callback(std::move(result));
}

void LogEngine::Cancel(CancelCallback callback) {
  FX_LOGS(INFO) << "Received a Cancel";
  callback();
}

}  // namespace a11y
