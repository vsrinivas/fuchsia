// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "lib/suggestion/fidl/speech_to_text.fidl.h"
#include "peridot/bin/suggestion_engine/ask_dispatcher.h"
#include "peridot/bin/suggestion_engine/windowed_subscriber.h"

namespace maxwell {

// Manages a single Ask suggestion subscriber.
class AskSubscriber : public BoundWindowedSuggestionSubscriber<AskController>,
                      public TranscriptionListener {
 public:
  AskSubscriber(
      const RankedSuggestions* ranked_suggestions,
      AskDispatcher* engine,
      fidl::InterfaceRequest<TranscriptionListener> transcription_listener,
      fidl::InterfaceHandle<SuggestionListener> listener,
      fidl::InterfaceRequest<AskController> controller);

  void SetUserInput(UserInputPtr input) override;

  // |TranscriptionListener|
  void OnTranscriptUpdate(const fidl::String& spoken_text) override;

 private:
  AskDispatcher* ask_dispatcher_;
  fidl::Binding<TranscriptionListener> transcription_listener_binding_;
};

}  // namespace maxwell
