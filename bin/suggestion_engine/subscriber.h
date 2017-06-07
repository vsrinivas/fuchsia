// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "apps/maxwell/services/suggestion/suggestion_provider.fidl.h"
#include "apps/maxwell/src/suggestion_engine/ranked_suggestion.h"
#include "lib/fidl/cpp/bindings/binding.h"

namespace maxwell {
class Subscriber {
 public:
  Subscriber(fidl::InterfaceHandle<SuggestionListener> listener)
      : listener_(SuggestionListenerPtr::Create(std::move(listener))) {}

  virtual ~Subscriber() = default;

  virtual void OnAddSuggestion(const RankedSuggestion& ranked_suggestion) {
    DispatchAdd(ranked_suggestion);
  }

  virtual void OnRemoveSuggestion(const RankedSuggestion& ranked_suggestion) {
    DispatchRemove(ranked_suggestion);
  }

  // FIDL methods, for use with BoundSet without having to expose listener_.

  bool is_bound() const { return listener_.is_bound(); }

  void set_connection_error_handler(const ftl::Closure& error_handler) {
    listener_.set_connection_error_handler(error_handler);
  }

  // End FIDL methods.

 protected:
  static SuggestionPtr CreateSuggestion(
      const RankedSuggestion& suggestion_data) {
    auto suggestion = Suggestion::New();
    suggestion->uuid = suggestion_data.prototype->suggestion_id;
    suggestion->rank = suggestion_data.rank;
    suggestion->display = suggestion_data.prototype->proposal->display->Clone();
    return suggestion;
  }

  void DispatchAdd(const RankedSuggestion& ranked_suggestion) {
    fidl::Array<SuggestionPtr> batch;
    batch.push_back(CreateSuggestion(ranked_suggestion));
    listener()->OnAdd(std::move(batch));
  }

  void DispatchRemove(const RankedSuggestion& ranked_suggestion) {
    listener()->OnRemove(ranked_suggestion.prototype->suggestion_id);
  }

  SuggestionListener* listener() const { return listener_.get(); }

 private:
  SuggestionListenerPtr listener_;
};

}  // namespace maxwell
