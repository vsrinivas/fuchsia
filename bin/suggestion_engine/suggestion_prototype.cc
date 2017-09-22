// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/maxwell/src/suggestion_engine/suggestion_prototype.h"

namespace maxwell {

std::string short_proposal_str(const SuggestionPrototype& prototype) {
  std::ostringstream str;
  str << "proposal " << prototype.proposal->id << " from "
      << prototype.source_url;
  return str.str();
}

}  // namespace maxwell
