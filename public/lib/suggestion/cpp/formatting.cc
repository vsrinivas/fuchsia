// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/suggestion/cpp/formatting.h>

namespace modular {

std::ostream& operator<<(std::ostream& os,
                         const fuchsia::modular::SuggestionDisplay& o) {
  return os << "{ headline: " << o.headline
            << ", subheadline: " << o.subheadline << ", details: " << o.details
            << "}";
}

std::ostream& operator<<(std::ostream& os,
                         const fuchsia::modular::Suggestion& o) {
  return os << "{ uuid: " << o.uuid << ", confidence: " << o.confidence
            << ", display: " << o.display << "}";
}

}  // namespace modular
