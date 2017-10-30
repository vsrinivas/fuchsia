// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/suggestion_engine/windowed_subscriber.h"

namespace maxwell {

WindowedSuggestionSubscriber::WindowedSuggestionSubscriber(
    const RankedSuggestions* ranked_suggestions,
    fidl::InterfaceHandle<SuggestionListener> listener,
    size_t max_results)
    : SuggestionSubscriber(std::move(listener)),
      max_results_(max_results),
      ranked_suggestions_(ranked_suggestions) {}

WindowedSuggestionSubscriber::~WindowedSuggestionSubscriber() = default;

void WindowedSuggestionSubscriber::OnSubscribe() {
  const auto& suggestion_vector = ranked_suggestions_->Get();

  fidl::Array<SuggestionPtr> window;
  for (size_t i = 0; i < max_results_ && i < suggestion_vector.size(); i++) {
    window.push_back(CreateSuggestion(*suggestion_vector[i]));
  }

  if (window)
    listener()->OnAdd(std::move(window));
}

void WindowedSuggestionSubscriber::Invalidate() {
  listener()->OnRemoveAll();
  OnSubscribe();  // redispatches add-all
}

void WindowedSuggestionSubscriber::OnProcessingChange(bool processing) {
  listener()->OnProcessingChange(processing);
}

}  // namespace maxwell
