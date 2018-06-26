// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/suggestion_engine/auto_select_first_query_listener.h"

#include <fuchsia/modular/cpp/fidl.h>

#include "peridot/bin/suggestion_engine/suggestion_engine_impl.h"

namespace modular {

AutoSelectFirstQueryListener::AutoSelectFirstQueryListener(
    SuggestionEngineImpl* suggestion_engine)
    : engine_(suggestion_engine) {}

void AutoSelectFirstQueryListener::OnQueryResults(
    fidl::VectorPtr<fuchsia::modular::Suggestion> suggestions) {
  suggestions_.reset();
  suggestions_ = std::move(suggestions);
}

void AutoSelectFirstQueryListener::OnQueryComplete() {
  if (suggestions_->empty()) {
    return;
  }
  fuchsia::modular::Interaction interaction;
  interaction.type = fuchsia::modular::InteractionType::SELECTED;
  engine_->NotifyInteraction(suggestions_->at(0).uuid, std::move(interaction));
}

}  // namespace modular
