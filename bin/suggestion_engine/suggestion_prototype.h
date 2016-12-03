// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <strstream>
#include <unordered_map>

#include "apps/maxwell/services/suggestion/proposal.fidl.h"
#include "lib/ftl/time/time_point.h"

namespace maxwell {

class ProposalPublisherImpl;
class SuggestionChannel;
struct RankedSuggestion;

struct SuggestionPrototype {
  std::string suggestion_id;
  ProposalPublisherImpl* source;
  ftl::TimePoint timestamp;
  ProposalPtr proposal;

  // TODO(rosswang): Do we actually need to support multiple channels per
  // suggestion? Ask is better implemented as a sometime-slave channel to Next.
  // This should become clearer after we implement Interruption.
  std::unordered_map<SuggestionChannel*, RankedSuggestion*> ranks_by_channel;
};

std::string short_proposal_str(const SuggestionPrototype& prototype);

}  // maxwell
