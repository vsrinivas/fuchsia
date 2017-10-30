// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/suggestion_engine/interruptions_channel.h"

#include "peridot/bin/suggestion_engine/ranking_feature.h"

namespace maxwell {

namespace {

void DispatchAdd(SuggestionListener* listener,
                 const SuggestionPrototype& prototype) {
  fidl::Array<SuggestionPtr> batch;
  SuggestionPtr suggestion = CreateSuggestion(prototype);
  suggestion->confidence = kMaxConfidence;
  batch.push_back(std::move(suggestion));
  listener->OnAdd(std::move(batch));
}

}  // namespace

bool IsInterruption(const SuggestionPrototype& prototype) {
  return prototype.proposal->display &&
         prototype.proposal->display->annoyance != AnnoyanceType::NONE;
}

void InterruptionsChannel::AddSubscriber(
    fidl::InterfaceHandle<SuggestionListener> subscriber,
    const RankedSuggestions& initial_suggestions_source) {
  auto listener = SuggestionListenerPtr::Create(std::move(subscriber));

  for (const auto& suggestion : initial_suggestions_source.Get()) {
    if (IsInterruption(*suggestion->prototype)) {
      DispatchAdd(listener.get(), *suggestion->prototype);
    }
  }

  subscribers_.AddInterfacePtr(std::move(listener));
}

void InterruptionsChannel::AddSuggestion(const SuggestionPrototype& prototype) {
  if (IsInterruption(prototype)) {
    subscribers_.ForAllPtrs([&](SuggestionListener* listener) {
      DispatchAdd(listener, prototype);
    });
  }
}

void InterruptionsChannel::RemoveSuggestion(
    const SuggestionPrototype& prototype) {
  if (IsInterruption(prototype)) {
    subscribers_.ForAllPtrs([&](SuggestionListener* listener) {
      listener->OnRemove(prototype.suggestion_id);
    });
  }
}

}  // namespace maxwell
