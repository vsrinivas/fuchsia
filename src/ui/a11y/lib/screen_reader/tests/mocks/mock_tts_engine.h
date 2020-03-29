// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_A11Y_LIB_SCREEN_READER_TESTS_MOCKS_MOCK_TTS_ENGINE_H_
#define SRC_UI_A11Y_LIB_SCREEN_READER_TESTS_MOCKS_MOCK_TTS_ENGINE_H_

#include <fuchsia/accessibility/tts/cpp/fidl.h>
#include <lib/fidl/cpp/binding_set.h>

namespace accessibility_test {
// Fake engine class to listen for incoming requests by the Tts Manager.
class MockTtsEngine : public fuchsia::accessibility::tts::Engine {
 public:
  explicit MockTtsEngine();
  ~MockTtsEngine() = default;

  fidl::InterfaceHandle<fuchsia::accessibility::tts::Engine> GetHandle() {
    return bindings_.AddBinding(this);
  }

  // Disconnects this fake Engine. All bindings are close.
  void Disconnect() { return bindings_.CloseAll(); }

  // Examine the utterances received via Enqueue() calls.
  const std::vector<fuchsia::accessibility::tts::Utterance>& ExamineUtterances() const {
    return utterances_;
  }

  // Returns true if a call to Cancel() was made to this object. False otherwise.
  bool ReceivedCancel() const { return received_cancel_; }

  // Returns true if a call to Speak() was made to this object. False otherwise.
  bool ReceivedSpeak() const { return received_speak_; }

  void set_should_fail_speak(bool value) { should_fail_speak_ = value; }
  void set_should_fail_enqueue(bool value) { should_fail_enqueue_ = value; }

 private:
  // |fuchsia.accessibility.tts.Engine|
  void Enqueue(fuchsia::accessibility::tts::Utterance utterance, EnqueueCallback callback) override;

  // |fuchsia.accessibility.tts.Engine|
  void Speak(SpeakCallback callback) override;

  // |fuchsia.accessibility.tts.Engine|
  void Cancel(CancelCallback callback) override;

  fidl::BindingSet<fuchsia::accessibility::tts::Engine> bindings_;

  // Utterances received via Enqueue() calls.
  std::vector<fuchsia::accessibility::tts::Utterance> utterances_;
  // Whether  a Cancel() call was made.
  bool received_cancel_ = false;
  // Whether a Speak() call was made.
  bool received_speak_ = false;

  // Whether calls to Enqueue() will fail.
  bool should_fail_enqueue_ = false;
  // Whether calls to Speak() will fail.
  bool should_fail_speak_ = false;
};

}  // namespace accessibility_test
#endif  // SRC_UI_A11Y_LIB_SCREEN_READER_TESTS_MOCKS_MOCK_TTS_ENGINE_H_
