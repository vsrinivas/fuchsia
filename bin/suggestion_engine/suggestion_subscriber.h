// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "lib/fidl/cpp/bindings/binding.h"
#include "lib/suggestion/fidl/suggestion_provider.fidl.h"
#include "peridot/bin/suggestion_engine/ranked_suggestion.h"

namespace maxwell {

class SuggestionSubscriber {
 public:
  SuggestionSubscriber(fidl::InterfaceHandle<SuggestionListener> listener);
  virtual ~SuggestionSubscriber();

  // Send the current initial set of suggestions
  virtual void OnSubscribe();
  virtual void OnAddSuggestion(const RankedSuggestion& ranked_suggestion) = 0;

  virtual void OnRemoveSuggestion(
      const RankedSuggestion& ranked_suggestion) = 0;

  // TODO(jwnichols): Why did we change the terminology here?  Seems like it
  // should be OnRemoveAllSuggestions().
  virtual void Invalidate() = 0;

  virtual void OnProcessingChange(bool processing) = 0;

  // FIDL methods, for use with BoundSet without having to expose listener_.

  bool is_bound() { return listener_.is_bound(); }

  void set_connection_error_handler(const fxl::Closure& error_handler) {
    listener_.set_connection_error_handler(error_handler);
  }

  // End FIDL methods.

 protected:
  static SuggestionPtr CreateSuggestion(
      const RankedSuggestion& suggestion_data);

  void DispatchAdd(const RankedSuggestion& ranked_suggestion) {
    fidl::Array<SuggestionPtr> batch;
    batch.push_back(CreateSuggestion(ranked_suggestion));
    listener()->OnAdd(std::move(batch));
  }

  void DispatchRemove(const RankedSuggestion& ranked_suggestion) {
    listener()->OnRemove(ranked_suggestion.prototype->suggestion_id);
  }

  void DispatchProcessingChange(bool processing) {
    listener()->OnProcessingChange(processing);
  }

  SuggestionListener* listener() const { return listener_.get(); }

 private:
  SuggestionListenerPtr listener_;
};

}  // namespace maxwell
