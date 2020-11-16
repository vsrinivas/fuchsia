// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/tts/tests/mocks/mock_tts_manager.h"

namespace accessibility_test {

void MockTtsManager::OpenEngine(
    fidl::InterfaceRequest<fuchsia::accessibility::tts::Engine> engine_request,
    a11y::TtsManager::OpenEngineCallback callback) {
  fuchsia::accessibility::tts::TtsManager_OpenEngine_Result result;
  if (engine_in_use_) {
    result.set_err(fuchsia::accessibility::tts::Error::BUSY);
  } else {
    result.set_response(fuchsia::accessibility::tts::TtsManager_OpenEngine_Response{});
  }
  callback(std::move(result));
  engine_in_use_ = true;
  CheckAndRunCallback();
}

void MockTtsManager::SetEngineInUse(bool engine_in_use) { engine_in_use_ = engine_in_use; }

void MockTtsManager::RegisterEngine(
    fidl::InterfaceHandle<fuchsia::accessibility::tts::Engine> engine,
    a11y::TtsManager::RegisterEngineCallback callback) {
  fuchsia::accessibility::tts::EngineRegistry_RegisterEngine_Result result;
  if (!engine_registered_) {
    result.set_err(fuchsia::accessibility::tts::Error::BUSY);
  } else {
    result.set_response(fuchsia::accessibility::tts::EngineRegistry_RegisterEngine_Response{});
  }
  callback(std::move(result));
  engine_registered_ = true;
  CheckAndRunCallback();
}

void MockTtsManager::SetEngineRegisered(bool engine_registered) {
  engine_registered_ = engine_registered;
}

void MockTtsManager::CheckAndRunCallback() {
  if (tts_engine_ready_callback_ && engine_in_use_ && engine_registered_) {
    tts_engine_ready_callback_();
    tts_engine_ready_callback_ = TTSEngineReadyCallback();
  }
}

void MockTtsManager::RegisterTTSEngineReadyCallback(
    a11y::TtsManager::TTSEngineReadyCallback callback) {
  tts_engine_ready_callback_ = std::move(callback);
  CheckAndRunCallback();
}

void MockTtsManager::UnregisterTTSEngineReadyCallback() {
  tts_engine_ready_callback_ = TTSEngineReadyCallback();
}

}  // namespace accessibility_test
