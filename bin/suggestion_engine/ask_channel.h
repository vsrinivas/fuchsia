// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <unordered_map>

#include "apps/maxwell/src/suggestion_engine/suggestion_channel.h"
#include "apps/maxwell/src/suggestion_engine/ask_subscriber.h"

namespace maxwell {
namespace suggestion {

// A suggestion channel for Ask (query-based) suggestions.
//
// Query-based suggestions are informed by a user-driven query in addition to
// context information. If such a query is not present, however, the experience
// is similar to Next.
class AskChannel : public SuggestionChannel {
 public:
  // This constructor leaks |this| to the constructed dedicated subscriber, but
  // the subscriber constructor only makes use of non-virtual members of the
  // SuggestionChannel base class.
  AskChannel(fidl::InterfaceHandle<Listener> listener,
             fidl::InterfaceRequest<AskController> controller)
      : subscriber_(this, std::move(listener), std::move(controller)) {}

  RankedSuggestion* OnAddSuggestion(
      const SuggestionPrototype* prototype) override;
  void OnChangeSuggestion(RankedSuggestion* ranked_suggestion) override;
  void OnRemoveSuggestion(const RankedSuggestion* ranked_suggestion) override;
  const RankedSuggestions* ranked_suggestions() const override;

  void SetQuery(const std::string& query);

  // FIDL methods, for use with BoundSet without having to expose subscriber_.
  // TODO(rosswang): Might it be worth making these an interface?

  bool is_bound() const { return subscriber_.is_bound(); }

  void set_connection_error_handler(const ftl::Closure& error_handler) {
    subscriber_.set_connection_error_handler(error_handler);
  }

  // End FIDL methods.

 private:
  bool IncludeSuggestion(const SuggestionPrototype* prototype) const;
  // TEMPORARY by-insertion-order ranking
  float next_rank() const {
    return include_.empty() ? 0 : include_.back()->rank + 1;
  }

  AskSubscriber subscriber_;
  std::string query_;
  RankedSuggestions include_;
  // collection of sidelined suggestions added to this channel which will not be
  // given to the subscriber.
  //
  // indexed by suggestion ID
  //
  // This would ideally be a set, but we end up passing around naked pointers
  // and there's not presently a great way to set-identify unique_ptrs and their
  // pointer counterparts.
  std::unordered_map<std::string, std::unique_ptr<RankedSuggestion>> exclude_;
};

}  // namespace suggestion
}  // namespace maxwell
