// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <strstream>

#include "lib/suggestion/fidl/proposal.fidl.h"
#include "lib/fxl/time/time_point.h"

namespace maxwell {

struct SuggestionPrototype {
  std::string suggestion_id;
  std::string source_url;
  fxl::TimePoint timestamp;
  ProposalPtr proposal;
};

struct RankedSuggestion {
  SuggestionPrototype* prototype;
  float rank;
};

std::string short_proposal_str(const SuggestionPrototype& prototype);

}  // namespace maxwell
