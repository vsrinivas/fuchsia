// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "peridot/bin/suggestion_engine/suggestion_prototype.h"
#include "peridot/bin/suggestion_engine/suggestion_subscriber.h"

#include <vector>

namespace maxwell {

class SuggestionChannel {
 public:
  void DispatchInvalidate();
  void DispatchOnAddSuggestion(RankedSuggestion* suggestion);
  void DispatchOnRemoveSuggestion(RankedSuggestion* suggestion);
  void DispatchOnProcessingChange(bool processing);
  void AddSubscriber(std::unique_ptr<SuggestionSubscriber> subscriber);
  void RemoveAllSubscribers();

  bool is_bound() {
    for (auto& subscriber : subscribers_) {
      if (subscriber->is_bound())
        return true;
    }
    return false;
  }

  void set_connection_error_handler(const fxl::Closure& error_handler) {
    for (auto& subscriber : subscribers_) {
      subscriber->set_connection_error_handler(error_handler);
    }
  }

 private:
  std::vector<std::unique_ptr<SuggestionSubscriber>> subscribers_;
};

}  // namespace maxwell
