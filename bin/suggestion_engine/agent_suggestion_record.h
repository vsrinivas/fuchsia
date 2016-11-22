// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <unordered_map>

#include "apps/maxwell/src/suggestion_engine/ranked_suggestion.h"

namespace maxwell {
namespace suggestion {

class SuggestionChannel;

struct AgentSuggestionRecord {
  SuggestionPrototype* suggestion_prototype;
  // TODO(rosswang): Do we actually need to support multiple channels per
  // suggestion? Ask is better implemented as a sometime-slave channel to Next.
  // This should become clearer after we implement Interruption.
  std::unordered_map<SuggestionChannel*, RankedSuggestion*> ranks_by_channel;
};

}  // suggestion
}  // maxwell
