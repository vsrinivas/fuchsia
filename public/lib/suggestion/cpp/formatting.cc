// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/maxwell/lib/suggestion/formatting.h"

namespace maxwell {
namespace suggestion {

std::ostream& operator<<(std::ostream& os,
                         const maxwell::suggestion::Display& o) {
  return os << "{ headline: " << o.headline
            << ", subheadline: " << o.subheadline << ", details: " << o.details
            << "}";
}

std::ostream& operator<<(std::ostream& os, const Suggestion& o) {
  return os << "{ uuid: " << o.uuid << ", rank: " << o.rank
            << ", display: " << o.display << "}";
}

}  // namespace suggestion
}  // namespace maxwell
