// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "apps/maxwell/src/bound_set.h"
#include "apps/maxwell/src/suggestion_engine/next_subscriber.h"
#include "apps/maxwell/src/suggestion_engine/suggestion_channel.h"

namespace maxwell {
namespace suggestion {

class NextChannel : public SuggestionChannel {
 public:
  void AddSubscriber(std::unique_ptr<NextSubscriber> subscriber) {
    subscribers_.emplace(std::move(subscriber));
  }

 protected:
  void DispatchOnAddSuggestion(
      const RankedSuggestion& ranked_suggestion) override;

  void DispatchOnRemoveSuggestion(
      const RankedSuggestion& ranked_suggestion) override;

 private:
  maxwell::BoundNonMovableSet<NextSubscriber> subscribers_;
};

}  // namespace suggestion
}  // namespace maxwell
