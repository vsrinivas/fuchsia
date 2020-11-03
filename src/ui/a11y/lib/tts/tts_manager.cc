// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/tts/tts_manager.h"

#include <lib/syslog/cpp/macros.h>

namespace a11y {

TtsManager::TtsManager(sys::ComponentContext* startup_context) : engine_binding_(this) {
  FX_CHECK(startup_context);

  startup_context->outgoing()->AddPublicService(manager_bindings_.GetHandler(this));
  startup_context->outgoing()->AddPublicService(registry_bindings_.GetHandler(this));
}

TtsManager::~TtsManager() = default;

void TtsManager::OpenEngine(
    fidl::InterfaceRequest<fuchsia::accessibility::tts::Engine> engine_request,
    OpenEngineCallback callback) {
  fuchsia::accessibility::tts::TtsManager_OpenEngine_Result result;
  if (engine_binding_.is_bound()) {
    // The engine is in use by another speaker.
    result.set_err(fuchsia::accessibility::tts::Error::BUSY);
  } else {
    engine_binding_.Bind(std::move(engine_request));
    result.set_response(fuchsia::accessibility::tts::TtsManager_OpenEngine_Response{});
  }
  callback(std::move(result));
  CheckIfTtsEngineIsReadyAndRunCallback();
}

void TtsManager::RegisterEngine(fidl::InterfaceHandle<fuchsia::accessibility::tts::Engine> engine,
                                RegisterEngineCallback callback) {
  fuchsia::accessibility::tts::EngineRegistry_RegisterEngine_Result result;
  if (!engine_) {
    engine_.Bind(std::move(engine));
    result.set_response(fuchsia::accessibility::tts::EngineRegistry_RegisterEngine_Response{});
  } else {
    // There is already an engine registered.
    result.set_err(fuchsia::accessibility::tts::Error::BUSY);
  }
  callback(std::move(result));
  CheckIfTtsEngineIsReadyAndRunCallback();
}

void TtsManager::Enqueue(fuchsia::accessibility::tts::Utterance utterance,
                         EnqueueCallback callback) {
  fuchsia::accessibility::tts::Engine_Enqueue_Result result;
  if (!engine_) {
    result.set_err(fuchsia::accessibility::tts::Error::BAD_STATE);
    callback(std::move(result));
  } else {
    engine_->Enqueue(std::move(utterance), std::move(callback));
  }
}

void TtsManager::CheckIfTtsEngineIsReadyAndRunCallback() {
  if (!engine_binding_.is_bound() || !engine_) {
    return;
  }

  for (const auto& callback : tts_engine_ready_callbacks_) {
    if (callback) {
      callback();
    }
  }

  tts_engine_ready_callbacks_.clear();
}

void TtsManager::Speak(SpeakCallback callback) {
  fuchsia::accessibility::tts::Engine_Speak_Result result;
  if (!engine_) {
    result.set_err(fuchsia::accessibility::tts::Error::BAD_STATE);
    callback(std::move(result));
  } else {
    engine_->Speak(std::move(callback));
  }
}

void TtsManager::Cancel(CancelCallback callback) {
  if (engine_) {
    engine_->Cancel(std::move(callback));
  } else {
    callback();
  }
}

void TtsManager::RegisterTTSEngineReadyCallback(TTSEngineReadyCallback callback) {
  tts_engine_ready_callbacks_.emplace_back(std::move(callback));
}
}  // namespace a11y
