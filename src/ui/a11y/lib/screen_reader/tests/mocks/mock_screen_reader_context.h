// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_A11Y_LIB_SCREEN_READER_TESTS_MOCKS_MOCK_SCREEN_READER_CONTEXT_H_
#define SRC_UI_A11Y_LIB_SCREEN_READER_TESTS_MOCKS_MOCK_SCREEN_READER_CONTEXT_H_

#include <memory>
#include <string>
#include <vector>

#include "src/ui/a11y/lib/screen_reader/focus/tests/mocks/mock_a11y_focus_manager.h"
#include "src/ui/a11y/lib/screen_reader/screen_reader_context.h"
#include "src/ui/a11y/lib/screen_reader/speaker.h"

namespace accessibility_test {

class MockScreenReaderContext : public a11y::ScreenReaderContext {
 public:
  class MockSpeaker : public a11y::Speaker {
   public:
    // Callback that is invoked when this object is about to be destroyed.
    using OnDestructionCallback = fit::function<void(MockSpeaker*)>;

    MockSpeaker() = default;
    ~MockSpeaker() override;

    // |Speaker|
    fpromise::promise<> SpeakNodePromise(
        const fuchsia::accessibility::semantics::Node* node, Options options,
        a11y::ScreenReaderMessageGenerator::ScreenReaderMessageContext message_context = {})
        override;

    // |Speaker|
    fpromise::promise<> SpeakMessagePromise(fuchsia::accessibility::tts::Utterance utterance,
                                            Options options) override;

    // |Speaker|
    fpromise::promise<> SpeakMessageByIdPromise(fuchsia::intl::l10n::MessageIds message_id,
                                                Options options) override;

    // |Speaker|
    fpromise::promise<> SpeakNodeCanonicalizedLabelPromise(
        const fuchsia::accessibility::semantics::Node* node, Options options) override;

    // |Speaker|
    fpromise::promise<> CancelTts() override;

    // Returns true whether any speak request was done.
    bool ReceivedSpeak() const { return received_speak_; }

    // Returns if SpeakNodeCanonicalizedLabelPromise was called.
    bool ReceivedSpeakLabel() const { return received_speak_label_; }

    // Returns whether speech was cancelled.
    bool ReceivedCancel() const { return received_cancel_; }

    // The following three methods return parallel vectors of the arguments sent
    // to calls of SpeakNodePromise() or
    // SpeakNodeCanonicalizedLabelPromise().
    std::vector<uint32_t>& node_ids() { return speak_node_ids_; }
    std::vector<Options>& speak_node_options() { return speak_node_options_; }
    std::vector<a11y::ScreenReaderMessageGenerator::ScreenReaderMessageContext>&
    message_contexts() {
      return speak_node_message_contexts_;
    }

    // Returns the vector that collects all messages sent to SpeakMessagePromise().
    std::vector<std::string>& messages() { return messages_; }
    // Returns the vector that collects all message IDs to be spoken by |SpeakMessageByIdPromise|
    std::vector<fuchsia::intl::l10n::MessageIds>& message_ids() { return message_ids_; }

    // Sets a callback that will be invoked before this object is destroyed.
    void set_on_destruction_callback(OnDestructionCallback callback);

    void set_epitaph(fuchsia::intl::l10n::MessageIds epitaph) override { epitaph_ = epitaph; }
    fuchsia::intl::l10n::MessageIds epitaph() { return epitaph_; }

   private:
    std::vector<std::string> messages_;
    std::vector<fuchsia::intl::l10n::MessageIds> message_ids_;
    std::vector<uint32_t> speak_node_ids_;
    std::vector<Options> speak_node_options_;
    std::vector<a11y::ScreenReaderMessageGenerator::ScreenReaderMessageContext>
        speak_node_message_contexts_;
    bool received_speak_ = false;
    bool received_speak_label_ = false;
    bool received_cancel_ = false;
    OnDestructionCallback on_destruction_callback_;
    fuchsia::intl::l10n::MessageIds epitaph_ = fuchsia::intl::l10n::MessageIds::ROLE_HEADER;
  };

  MockScreenReaderContext();
  ~MockScreenReaderContext() override = default;

  // Pointer to the mocks, so expectations can be configured in tests.
  MockA11yFocusManager* mock_a11y_focus_manager_ptr() { return mock_a11y_focus_manager_ptr_; }
  MockSpeaker* mock_speaker_ptr() { return mock_speaker_ptr_; }

  // |ScreenReaderContext|
  a11y::A11yFocusManager* GetA11yFocusManager() override { return a11y_focus_manager_.get(); }

  // |ScreenReaderContext|
  a11y::Speaker* speaker() override { return speaker_.get(); }

  // |ScreenReaderContext|
  bool IsTextFieldFocused() const override { return false; }

  // |ScreenReaderContext|
  bool IsVirtualKeyboardFocused() const override { return virtual_keyboard_focused_; }

  void set_virtual_keyboard_focused(bool value) { virtual_keyboard_focused_ = value; }

  // |ScreenReaderContext|
  bool UpdateCacheIfDescribableA11yFocusedNodeContentChanged() override {
    return describable_content_changed_;
  }

  void set_describable_content_changed(bool value) { describable_content_changed_ = value; }

 private:
  std::unique_ptr<a11y::A11yFocusManager> a11y_focus_manager_;
  MockA11yFocusManager* mock_a11y_focus_manager_ptr_;
  std::unique_ptr<a11y::Speaker> speaker_;
  MockSpeaker* mock_speaker_ptr_;
  bool virtual_keyboard_focused_ = false;
  bool describable_content_changed_ = false;
};

class MockScreenReaderContextFactory : public a11y::ScreenReaderContextFactory {
 public:
  MockScreenReaderContextFactory() = default;
  ~MockScreenReaderContextFactory() override = default;

  std::unique_ptr<a11y::ScreenReaderContext> CreateScreenReaderContext(
      std::unique_ptr<a11y::A11yFocusManager> a11y_focus_manager, a11y::TtsManager* tts_manager,
      a11y::ViewSource* view_source, std::string locale_id) override {
    auto mock_screen_reader_context = std::make_unique<MockScreenReaderContext>();
    mock_screen_reader_context->set_locale_id(locale_id);
    mock_screen_reader_context_ = mock_screen_reader_context.get();
    return mock_screen_reader_context;
  }

  MockScreenReaderContext* mock_screen_reader_context() { return mock_screen_reader_context_; }

 private:
  MockScreenReaderContext* mock_screen_reader_context_;
};
}  // namespace accessibility_test

#endif  // SRC_UI_A11Y_LIB_SCREEN_READER_TESTS_MOCKS_MOCK_SCREEN_READER_CONTEXT_H_
