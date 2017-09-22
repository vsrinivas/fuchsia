// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "apps/maxwell/src/suggestion_engine/ask_dispatcher.h"
#include "apps/maxwell/src/suggestion_engine/windowed_subscriber.h"

namespace maxwell {

// Manages a single Ask suggestion subscriber.
class AskSubscriber : public BoundWindowedSuggestionSubscriber<AskController> {
 public:
  AskSubscriber(const RankedSuggestions* ranked_suggestions,
                AskDispatcher* engine,
                fidl::InterfaceHandle<SuggestionListener> listener,
                fidl::InterfaceRequest<AskController> controller);

  void SetUserInput(UserInputPtr input) override;

 private:
  AskDispatcher* ask_dispatcher_;
};

}  // namespace maxwell
