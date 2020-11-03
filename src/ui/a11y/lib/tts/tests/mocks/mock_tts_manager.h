// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_A11Y_LIB_TTS_TESTS_MOCKS_MOCK_TTS_MANAGER_H_
#define SRC_UI_A11Y_LIB_TTS_TESTS_MOCKS_MOCK_TTS_MANAGER_H_

#include <map>
#include <optional>

#include "src/ui/a11y/lib/tts/tts_manager.h"

namespace accessibility_test {

class MockTtsManager : public a11y::TtsManager {
 public:
  explicit MockTtsManager(sys::ComponentContext* context) : TtsManager(context) {}
  ~MockTtsManager() override = default;

  // |a11y::TtsManager|
  void OpenEngine(fidl::InterfaceRequest<fuchsia::accessibility::tts::Engine> engine_request,
                  OpenEngineCallback callback) override;

  // Sets value of engine_in_use_.
  void SetEngineInUse(bool engine_in_use);

  // |a11y::TtsManager|
  void RegisterEngine(fidl::InterfaceHandle<fuchsia::accessibility::tts::Engine> engine,
                      RegisterEngineCallback callback) override;

  // Sets value of engine_registered_.
  void SetEngineRegisered(bool engine_registered);

  // |a11y::TtsManager|
  void RegisterTTSEngineReadyCallback(a11y::TtsManager::TTSEngineReadyCallback callback) override;

 private:
  // Callback invoked during |RegisterEngine()|.
  TTSEngineReadyCallback tts_engine_ready_callback_;

  // Indicates whether an engine has been registered.
  bool engine_registered_ = false;

  // Indicates whether a speaker is using the registered engine.
  bool engine_in_use_ = false;
};

}  // namespace accessibility_test

#endif  // SRC_UI_A11Y_LIB_TTS_TESTS_MOCKS_MOCK_TTS_MANAGER_H_
