// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "apps/maxwell/src/suggestion_engine/agent_suggestion_record.h"

namespace maxwell {
namespace suggestion {

class SuggestionChannel {
 public:
  typedef std::vector<std::unique_ptr<RankedSuggestion>> RankedSuggestions;

  RankedSuggestion* OnAddSuggestion(SuggestionPrototype* prototype);
  void OnChangeSuggestion(RankedSuggestion* ranked_suggestion);
  void OnRemoveSuggestion(const RankedSuggestion* ranked_suggestion);

  // Returns a read-only mutable vector of suggestions in ranked order, from
  // highest to lowest relevance.
  const RankedSuggestions* ranked_suggestions() const {
    return &ranked_suggestions_;
  }

 protected:
  virtual void DispatchOnAddSuggestion(
      const RankedSuggestion& ranked_suggestion) = 0;
  virtual void DispatchOnRemoveSuggestion(
      const RankedSuggestion& ranked_suggestion) = 0;

 private:
  // Current justification: ranking is not implemented; proposals ranked in
  // insertion order.
  //
  // Eventual justification:
  //
  // Use a vector rather than set to allow dynamic reordering. Not all usages
  // take advantage of dynamic reordering, but this is sufficiently general to
  // not require a specialized impl using std::set.
  RankedSuggestions ranked_suggestions_;
};

}  // namespace suggestion
}  // namespace maxwell
