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
  std::unordered_map<SuggestionChannel*, RankedSuggestion*> ranks_by_channel;
};

}  // suggestion
}  // maxwell
