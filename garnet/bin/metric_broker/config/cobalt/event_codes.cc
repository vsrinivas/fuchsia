// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/metric_broker/config/cobalt/event_codes.h"

#include <cstdint>

namespace broker_service::cobalt {
EventCodes::EventCodes(CodeEntry* entries, uint64_t num_entries) {
  if (entries == nullptr || num_entries == 0) {
    return;
  }

  for (uint64_t i = 0; i < num_entries; ++i) {
    const auto& entry = entries[i];
    if (!entry.second.has_value()) {
      continue;
    }
    if (entry.first >= kMaxDimensionsPerEvent) {
      continue;
    }

    codes[entry.first] = entry.second;
  }
}
}  // namespace broker_service::cobalt
