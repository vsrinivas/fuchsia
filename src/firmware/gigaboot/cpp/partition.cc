// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "partition.h"

#include <algorithm>
#include <iterator>

namespace gigaboot {

std::optional<PartitionMap> PartitionMap::GeneratePartitionMap(
    cpp20::span<const PartitionEntry> const partitions) {
  int max_count = 0;
  fbl::Vector<PartitionEntry> p;

  for (PartitionEntry const& entry : partitions) {
    max_count += entry.min_size_bytes == SIZE_MAX ? 1 : 0;
    auto match = std::find_if(p.begin(), p.end(),
                              [&entry](PartitionEntry const& e) { return entry.name == e.name; });
    if (match == p.end()) {
      p.push_back(entry);
    } else {
      match->min_size_bytes = match->min_size_bytes > entry.min_size_bytes ? match->min_size_bytes
                                                                           : entry.min_size_bytes;
    }
  }

  // At most one partition can take all remaining space.
  if (max_count > 1) {
    return std::nullopt;
  }

  // It's easier to have the partition that takes all remaining space be at the end.
  if (max_count == 1 && (*(p.end() - 1)).min_size_bytes != SIZE_MAX) {
    return std::nullopt;
  }

  return PartitionMap(std::move(p));
}

}  // namespace gigaboot
