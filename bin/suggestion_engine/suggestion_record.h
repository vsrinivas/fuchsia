// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "apps/maxwell/services/suggestion/suggestion_agent_client.fidl.h"
#include "apps/maxwell/services/suggestion/suggestion_provider.fidl.h"

namespace maxwell {
namespace suggestion {

struct SuggestionRecord {
  Suggestion suggestion;
  // The proposal that created the suggestion. The display member will be
  // missing from this instance (having been moved to the suggestion).
  ProposalPtr proposal;
};

}  // suggestion
}  // maxwell
