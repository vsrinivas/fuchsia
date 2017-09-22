// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/suggestion/cpp/formatting.h"

namespace maxwell {

std::ostream& operator<<(std::ostream& os, const SuggestionDisplay& o) {
  return os << "{ headline: " << o.headline
            << ", subheadline: " << o.subheadline << ", details: " << o.details
            << "}";
}

std::ostream& operator<<(std::ostream& os, const Suggestion& o) {
  return os << "{ uuid: " << o.uuid << ", rank: " << o.rank
            << ", display: " << o.display << "}";
}

}  // namespace maxwell
