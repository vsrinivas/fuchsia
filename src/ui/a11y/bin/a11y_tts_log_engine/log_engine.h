// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_A11Y_BIN_A11Y_TTS_LOG_ENGINE_H_
#define SRC_UI_A11Y_BIN_A11Y_TTS_LOG_ENGINE_H_

#include <fuchsia/accessibility/tts/cpp/fidl.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/sys/cpp/component_context.h>

namespace a11y {

// A simple Engine implementation to log incoming requests.
//
// this simple Engine after registration with Tts manager, only logs incoming
// speech requests. It is used temporary mostly for debugging purposes until a
// real Tts Engine is implemented.
class LogEngine : public fuchsia::accessibility::tts::Engine {
 public:
  // On construction, this class registers as an Engine with
  // fuchsia.accessibility.tts.TtsManager service.
  explicit LogEngine(std::unique_ptr<sys::ComponentContext> startup_context);
  ~LogEngine() = default;

 private:
  // |fuchsia.accessibility.tts.Engine|
  void Enqueue(fuchsia::accessibility::tts::Utterance utterance, EnqueueCallback callback) override;

  // |fuchsia.accessibility.tts.Engine|
  void Speak(SpeakCallback callback) override;

  // |fuchsia.accessibility.tts.Engine|
  void Cancel(CancelCallback callback) override;

  // Holds all utterances added via Enqueue(). Gets cleared whenever Speak() is
  // called.
  std::vector<fuchsia::accessibility::tts::Utterance> utterances_;
  // Bindings to the service implemented by this class.
  fidl::BindingSet<fuchsia::accessibility::tts::Engine> bindings_;

  // Client-side of the Tts registry interface.
  fuchsia::accessibility::tts::EngineRegistryPtr registry_;
};

}  // namespace a11y

#endif  // SRC_UI_A11Y_BIN_A11Y_TTS_LOG_ENGINE_H_
