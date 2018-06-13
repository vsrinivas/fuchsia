// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_SUGGESTION_ENGINE_SUGGESTION_PROTOTYPE_H_
#define PERIDOT_BIN_SUGGESTION_ENGINE_SUGGESTION_PROTOTYPE_H_

#include <strstream>

#include <fuchsia/modular/cpp/fidl.h>
#include "lib/fxl/time/time_point.h"

namespace modular {

class SuggestionPrototype {
 public:
  // (proposer ID, proposal ID) => suggestion prototype
  using SuggestionPrototypeMap = std::map<std::pair<std::string, std::string>,
                                          std::unique_ptr<SuggestionPrototype>>;

  SuggestionPrototype(std::string source_url, std::string story_id,
                      fuchsia::modular::Proposal proposal);
  ~SuggestionPrototype();

  static SuggestionPrototype* CreateInMap(
      SuggestionPrototypeMap* owner, const std::string& source_url,
      const std::string& story_id, fuchsia::modular::Proposal proposal);

  // Used for debugging and INFO logs.
  std::string ShortRepr();

  fuchsia::modular::Suggestion MakeSuggestion() const;

  const std::string suggestion_id;
  const fxl::TimePoint timestamp;
  // Story ID is set when the proposal came with a name.
  // fuchsia::modular::SuggestionEngine maps this name namespaced by the source
  // to this ID and propagates it here. If this story id was not set, it can be
  // set to the (deprecated) proposal.story_id.
  const std::string story_id;
  const std::string source_url;
  fuchsia::modular::Proposal proposal;
};

}  // namespace modular

#endif  // PERIDOT_BIN_SUGGESTION_ENGINE_SUGGESTION_PROTOTYPE_H_
