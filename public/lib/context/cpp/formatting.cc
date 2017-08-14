// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/maxwell/lib/context/formatting.h"

namespace maxwell {
std::ostream& operator<<(std::ostream& os, const ContextUpdateForTopics& update) {
  os << "{";
  for (auto it = update.values.cbegin(); it != update.values.cend(); ++it) {
    os << " " << it.GetKey() << ": " << it.GetValue() << ",";
  }
  return os << "}";
}

std::ostream& operator<<(std::ostream& os, const ContextQueryForTopics& query) {
  os << "{ topics: [";
  for (auto it = query.topics.begin(); it != query.topics.end(); ++it) {
    os << " " << *it << ", ";
  }
  return os << "] }";
}

}  // namespace maxwell
