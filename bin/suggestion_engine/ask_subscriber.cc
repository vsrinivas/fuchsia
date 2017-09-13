// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/suggestion_engine/ask_subscriber.h"

namespace maxwell {

AskSubscriber::AskSubscriber(
    const RankedSuggestions* ranked_suggestions,
    AskDispatcher* ask_dispatcher,
    fidl::InterfaceRequest<TranscriptionListener> transcription_listener,
    fidl::InterfaceHandle<SuggestionListener> listener,
    fidl::InterfaceRequest<AskController> controller)
    : BoundWindowedSuggestionSubscriber(ranked_suggestions,
                                        std::move(listener),
                                        std::move(controller)),
      ask_dispatcher_(ask_dispatcher),
      transcription_listener_binding_(this, std::move(transcription_listener)) {
}

void AskSubscriber::SetUserInput(UserInputPtr input) {
  ask_dispatcher_->DispatchAsk(std::move(input));
  // For now, abort speech recognition if input is changed via the controller.
  // Closing the TranscriptionListener binding tells the SpeechToText service to
  // stop transcription and stop sending us updates. We do this here to enact
  // the policy that if the user starts typing input, they are not doing speech
  // recognition.
  if (transcription_listener_binding_.is_bound())
    transcription_listener_binding_.Close();
}

void AskSubscriber::OnTranscriptUpdate(const fidl::String& spoken_text) {
  auto input = UserInput::New();
  input->set_text(spoken_text);
  ask_dispatcher_->DispatchAsk(std::move(input));
}

}  // namespace maxwell
