// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "lib/suggestion/fidl/speech_to_text.fidl.h"
#include "peridot/bin/suggestion_engine/ask_dispatcher.h"
#include "peridot/bin/suggestion_engine/windowed_subscriber.h"

namespace maxwell {

// Manages a single Ask suggestion subscriber.
class AskSubscriber : public BoundWindowedSuggestionSubscriber<AskController> {
 public:
  AskSubscriber(
      const RankedSuggestions* ranked_suggestions,
      AskDispatcher* engine,
      fidl::InterfaceRequest<TranscriptionListener> transcription_listener,
      fidl::InterfaceHandle<SuggestionListener> listener,
      fidl::InterfaceRequest<AskController> controller);

  // |AskController|
  void SetUserInput(UserInputPtr input) override;
  void BeginSpeechCapture(fidl::InterfaceHandle<TranscriptionListener>
                              transcription_listener) override;
  // end |AskController|

 private:
  AskDispatcher* ask_dispatcher_;
};

}  // namespace maxwell
